#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include "tinyrpc/comm/log.h"
#include "reactor.h"
#include "mutex.h"
#include "fd_event.h"
#include "timer.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"


extern read_fun_ptr_t g_sys_read_fun;  // sys read func
extern write_fun_ptr_t g_sys_write_fun;  // sys write func

namespace tinyrpc {

// 每个线程的reactor
static thread_local Reactor* t_reactor_ptr = nullptr;

static thread_local int t_max_epoll_timeout = 10000;     // ms

static CoroutineTaskQueue* t_couroutine_task_queue = nullptr;


Reactor::Reactor() {
  
  // one thread can't create more than one reactor object!!
  // assert(t_reactor_ptr == nullptr);
	if (t_reactor_ptr != nullptr) {
		RpcErrorLog << "this thread has already create a reactor";
		Exit(0);
	}

  m_tid = gettid();

  RpcDebugLog << "thread[" << m_tid << "] succ create a reactor object";
  t_reactor_ptr = this;

  // epollfd创建
  if((m_epfd = epoll_create(1)) <= 0 ) {
		RpcErrorLog << "start server error. epoll_create error, sys error=" << strerror(errno);
		Exit(0);
	} else {
		RpcDebugLog << "m_epfd = " << m_epfd;
	}
  // assert(m_epfd > 0);

  // eventfd唤醒事件
	if((m_wake_fd = eventfd(0, EFD_NONBLOCK)) <= 0 ) {
		RpcErrorLog << "start server error. event_fd error, sys error=" << strerror(errno);
		Exit(0);
	}
	RpcDebugLog << "wakefd = " << m_wake_fd;
  // assert(m_wake_fd > 0);	
	addWakeupFd();

}

Reactor::~Reactor() {
  RpcDebugLog << "~Reactor";
	close(m_epfd);
  if (m_timer != nullptr) {
    delete m_timer;
    m_timer = nullptr;
  }
  t_reactor_ptr = nullptr;
}

// 获得当前线程的reactor
Reactor* Reactor::GetReactor() {
  if (t_reactor_ptr == nullptr) {
		RpcDebugLog << "Create new Reactor";
    t_reactor_ptr = new Reactor();
  }
	// RpcDebugLog << "t_reactor_ptr = " << t_reactor_ptr;
  return t_reactor_ptr; 
}

/*
 * 
 * !!!
 * 事件池操作  ==>  一个线程一个reactor，而reactor的操作可能会由其它线程调用
 * 添加事件    ==>  addEvent()
 * 删除事件    ==>  delEvent()
 * 如果在本线程，那么直接epoll_ctl()
 * 否则，将根据事件内容放入m_pending_add_fds, m_pending_del_fds
 * 
 */
// call by other threads, need lock
void Reactor::addEvent(int fd, epoll_event event, bool is_wakeup/*=true*/) {
  if (fd == -1) {
    RpcErrorLog << "add error. fd invalid, fd = -1";
    return;
  }
  // 如果在本线程，添加事件后退出
  if (isLoopThread()) {
    addEventInLoopThread(fd, event);
    return;
  }
  // 如果不在本线程内，那么将该事件添加到延迟表内
	{
 		Mutex::Lock lock(m_mutex);
		m_pending_add_fds.insert(std::pair<int, epoll_event>(fd, event));
	}
	if (is_wakeup) {
		wakeup();
	}
}

// call by other threads, need lock
void Reactor::delEvent(int fd, bool is_wakeup/*=true*/) {

  if (fd == -1) {
    RpcErrorLog << "add error. fd invalid, fd = -1";
    return;
  }

  // 如果在本线程，删除事件后退出
  if (isLoopThread()) {
    delEventInLoopThread(fd);
    return;
  }

  // 如果不在本线程内，那么将该事件添加到删除延迟表内
	{
    Mutex::Lock lock(m_mutex);
		m_pending_del_fds.push_back(fd);
	}
	if (is_wakeup) {
		wakeup();
	}
}

// wakeup()往wakeupfd写入
void Reactor::wakeup() {

  if (!m_is_looping) {
    return;
  }

	uint64_t tmp = 1;
	uint64_t* p = &tmp; 
	if(g_sys_write_fun(m_wake_fd, p, 8) != 8) {
		RpcErrorLog << "write wakeupfd[" << m_wake_fd <<"] error";
	}
}

// m_tid only can be writed in Reactor::Reactor, so it needn't to lock 
bool Reactor::isLoopThread() const {
	if (m_tid == gettid()) {
		// RpcDebugLog << "return true";
		return true;
	}
	// RpcDebugLog << "m_tid = "<< m_tid << ", getttid = " << gettid() <<"return false";
	return false;
}


void Reactor::addWakeupFd() {
	int op = EPOLL_CTL_ADD;
	epoll_event event;
  // ???
  // 不设置事件ptr? 竟然设置事件fd?
	event.data.fd = m_wake_fd;
	event.events = EPOLLIN;
	if ((epoll_ctl(m_epfd, op, m_wake_fd, &event)) != 0) {
		RpcErrorLog << "epoo_ctl error, fd[" << m_wake_fd << "], errno=" << errno << ", err=" << strerror(errno) ;
	}
	m_fds.push_back(m_wake_fd);

}

// need't mutex, only this thread call
void Reactor::addEventInLoopThread(int fd, epoll_event event) {

  assert(isLoopThread());

	int op = EPOLL_CTL_ADD;
	bool is_add = true;
	// int tmp_fd = event;
	auto it = find(m_fds.begin(), m_fds.end(), fd);
	if (it != m_fds.end()) {
		is_add = false;
		op = EPOLL_CTL_MOD;
	}
	
	// epoll_event event;
	// event.data.ptr = fd_event.get();
	// event.events = fd_event->getListenEvents();

	if (epoll_ctl(m_epfd, op, fd, &event) != 0) {
		RpcErrorLog << "epoo_ctl error, fd[" << fd << "], sys errinfo = " << strerror(errno);
		return;
	}
	if (is_add) {
		m_fds.push_back(fd);
	}
	RpcDebugLog << "epoll_ctl add succ, fd[" << fd << "]"; 

}


// need't mutex, only this thread call
void Reactor::delEventInLoopThread(int fd) {

  assert(isLoopThread());

	auto it = find(m_fds.begin(), m_fds.end(), fd);
	if (it == m_fds.end()) {
		RpcDebugLog << "fd[" << fd << "] not in this loop";
		return;
	}
	int op = EPOLL_CTL_DEL;

	if ((epoll_ctl(m_epfd, op, fd, nullptr)) != 0) {
		RpcErrorLog << "epoo_ctl error, fd[" << fd << "], sys errinfo = " << strerror(errno);
	}

	m_fds.erase(it);
	RpcDebugLog << "del succ, fd[" << fd << "]"; 
	
}


/*
 * !!!
 * 当前reactor.loop()
 * 一个线程一个reactor，reactor监听线程内的事件
 */

void Reactor::loop() {

  assert(isLoopThread());
  if (m_is_looping) {
    // RpcDebugLog << "this reactor is looping!";
    return;
  }
  
  m_is_looping = true;
	m_stop_flag = false;

  Coroutine* first_coroutine = nullptr;

	while(!m_stop_flag) {
		const int MAX_EVENTS = 10;
		epoll_event re_events[MAX_EVENTS + 1];

    /*
     * first_coroutine表示的是当前线程的第一个排队的coroutine
     * 处理当前线程获取的coroutine任务
     */
    if (first_coroutine) {
      tinyrpc::Coroutine::Resume(first_coroutine);
      first_coroutine = NULL;
    }

    // main reactor need't to resume coroutine in global CoroutineTaskQueue, only io thread do this work
    /*
     * IO线程从coroutinePool获取coroutine任务
     * 这里应该算是这个循环执行的第二个任务 
     */
    if (m_reactor_type != MainReactor) {
      FdEvent* ptr = NULL;
      // ptr->setReactor(NULL);
      while(1) {
        ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop();
        if (ptr) {
          // reactor和IOThread绑定
          // 事件取出时, 要绑定与之相关的IOThread和协程
          ptr->setReactor(this);
          // 然后将切换FdEvent所属的协程
          // ???
          // 然后呢，会在哪里处理协程的任务?
          // !!!
          // 从作者知乎内容找到答案
          // 会返回协程hook的Yield()位置，继续执行
          tinyrpc::Coroutine::Resume(ptr->getCoroutine()); 
          // 然后返回这个iothread_mainCoroutine位置
          // 继续从当前reactor出任务，再到协程位置执行
        } else {
          // 如果没有FdEvent，那么退出
          break;
        }
      }
    }


		// RpcDebugLog << "task";
		// excute tasks
    /*
     * 
     * !!!
     * 1. 处理当前任务
     * 
     */
    std::vector<std::function<void()>> tmp_tasks;
    {
    Mutex::Lock lock(m_mutex);
    tmp_tasks.swap(m_pending_tasks);
    }

    // 执行任务
		for (size_t i = 0; i < tmp_tasks.size(); ++i) {
			// RpcDebugLog << "begin to excute task[" << i << "]";
			if (tmp_tasks[i]) {
				tmp_tasks[i]();
			}
			// RpcDebugLog << "end excute tasks[" << i << "]";
		}
		// RpcDebugLog << "to epoll_wait";
    /*
     * 
     * !!!
     * 2. epoll处理正在监听的fd
     * fd添加/删除方法是addEventInLoopThread()/delEventInLoopThread()  
     *                          <==addEvent()/delEvent()
     * 
     */
		int rt = epoll_wait(m_epfd, re_events, MAX_EVENTS, t_max_epoll_timeout);

		// RpcDebugLog << "epoll_wait back";

		if (rt < 0) {
			RpcErrorLog << "epoll_wait error, skip, errno=" << strerror(errno);
		} else {
			// RpcDebugLog << "epoll_wait back, rt = " << rt;
			for (int i = 0; i < rt; ++i) {
				epoll_event one_event = re_events[i];	

        /*
         * 
         * !!!
         * 3. 处理wakeup_fd
         * 在当前线程直接添加的事件
         * 在epoll()监听到事件后直接接受
         * 
         */
				if (one_event.data.fd == m_wake_fd && (one_event.events & READ)) {
					// wakeup
					// RpcDebugLog << "epoll wakeup, fd=[" << m_wake_fd << "]";
					char buf[8];
					while(1) {
						if((g_sys_read_fun(m_wake_fd, buf, 8) == -1) && errno == EAGAIN) {
							break;
						}
					}

				} else {
          // wakeup()以外的事件是我们要处理的事件
          // 如果当前事件已经放置在FdEvent上了
					tinyrpc::FdEvent* ptr = (tinyrpc::FdEvent*)one_event.data.ptr;
          if (ptr != nullptr) {
            int fd = ptr->getFd();

            if ((!(one_event.events & EPOLLIN)) && (!(one_event.events & EPOLLOUT))){
              // ???
              // 不知道如何处理的事件?
              RpcErrorLog << "socket [" << fd << "] occur other unknow event:[" << one_event.events << "], need unregister this socket";
              delEventInLoopThread(fd);
            } else {
              // if register coroutine, pengding coroutine to common coroutine_tasks

              // 如果FdEvent关联了Croutine
              // read/write_hook()函数管理coroutine，
              // 实际在调用的toEpoll()函数中fd_event->setCoroutine(cur_cor)设置
              // 在fd_event->clearCoroutine()清除异步标志
              // 这样就能产生异步效果
              if (ptr->getCoroutine()) {
                // the first one coroutine when epoll_wait back, just directly resume by this thread, not add to global CoroutineTaskQueue
                // because every operate CoroutineTaskQueue should add mutex lock
                // !!!
                /* 
                 * 该线程保留的本地任务
                 * 避免多个任务放入协程任务池（锁的使用会降低效率）
                 * 如果还想优化，其实可以建立一个本地协程队列
                 */
                if (!first_coroutine) {
                  first_coroutine = ptr->getCoroutine();
                  continue;
                }
                if (m_reactor_type == SubReactor) {
                  // !!!
                  // 从作者知乎找到答案
                  // 如果是子reactor的内容, 从当前reactor删除当前事件
                  // 如果当前SubReactor完成事件后，应该在EPOLL取消事件
                  delEventInLoopThread(fd);
                  // reactor和IOThread绑定
                  // 事件要放入协程池时, 要消除关联的IOThread
                  ptr->setReactor(NULL);
                  // 接着把事件ptr放入处理事件的协程池
                  // 每个线程的reactor会在自己的reactor的Loop()中
                  //    ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop(); 
                  //    取出事件切换到协程内容执行，并等待返回
                  CoroutineTaskQueue::GetCoroutineTaskQueue()->push(ptr);
                } else {
                  // main reactor, just resume this coroutine. it is accept coroutine. and Main Reactor only have this coroutine
                  // 这是TcpServer的专用协程acceptCoroutine切换
                  // 主协程接收到EPOLL事件后，直接切换到子协程accept coroutine
                  // 在accept coroutine完成该事件
                  tinyrpc::Coroutine::Resume(ptr->getCoroutine());
                  // mainReactor所在的main线程，持有main_coroutine
                  // 只在这里一个一个完成任务，自然不关心本地协程任务和协程池任务
                  if (first_coroutine) {
                    first_coroutine = NULL;
                  }
                }

              } else {
                // 如果FdEvent没有设置Coroutine
                // 例如timer(继承自FdEvent)触发的事件
                /*
                 * 
                 * !!!
                 * 4. 处理没有设置协程的事件
                 * 如果是timer事件  ==>  read()
                 * 否则放入m_pending_tasks  ==>   下次循环直接处理
                 * 
                 */
                std::function<void()> read_cb;
                std::function<void()> write_cb;
                read_cb = ptr->getCallBack(READ);
                write_cb = ptr->getCallBack(WRITE);
                // if timer event, direct excute
                if (fd == m_timer_fd) {
                  // 如果是timer ==> read事件回调函数执行
                  // m_read_callback = std::bind(&Timer::onTimer, this);
                  // 那么timer回调Timer::onTimer函数
                  read_cb();
                  continue;
                }

                // 其他 没有设置coroutine, 非timer 事件
                // 将执行事件放入m_pending_tasks
                if (one_event.events & EPOLLIN) {
                  // RpcDebugLog << "socket [" << fd << "] occur read event";
                  Mutex::Lock lock(m_mutex);
                  m_pending_tasks.push_back(read_cb);						
                }
                if (one_event.events & EPOLLOUT) {
                  // RpcDebugLog << "socket [" << fd << "] occur write event";
                  Mutex::Lock lock(m_mutex);
                  m_pending_tasks.push_back(write_cb);						
                }

              }

            }
          }

				}
				
			}

			std::map<int, epoll_event> tmp_add;
			std::vector<int> tmp_del;

      /*
       * 
       * !!!
       * 5. 其它线程添加的事件 m_pending_add_fds
       * 先取出事件，再加入线程的的epoll监听当中
       * 
       */
			{
        Mutex::Lock lock(m_mutex);
				tmp_add.swap(m_pending_add_fds);
				m_pending_add_fds.clear();

				tmp_del.swap(m_pending_del_fds);
				m_pending_del_fds.clear();

			}
      // 从m_pending_tasks取出事件，然后放入thread
			for (auto i = tmp_add.begin(); i != tmp_add.end(); ++i) {
				// RpcDebugLog << "fd[" << (*i).first <<"] need to add";
				addEventInLoopThread((*i).first, (*i).second);	
			}
			for (auto i = tmp_del.begin(); i != tmp_del.end(); ++i) {
				// RpcDebugLog << "fd[" << (*i) <<"] need to del";
				delEventInLoopThread((*i));	
			}
		}
	}
  RpcDebugLog << "reactor loop end";
  m_is_looping = false;
}

// 终止reactor循环
void Reactor::stop() {
  if (!m_stop_flag && m_is_looping) {
    m_stop_flag = true;
    wakeup();
  }
}


// addTask()  ==>  不需要监听类的事件
// mutex的保护下放入m_pending_tasks即可
// 添加事件到reactor当中
void Reactor::addTask(std::function<void()> task, bool is_wakeup /*=true*/) {

  {
    Mutex::Lock lock(m_mutex);
    m_pending_tasks.push_back(task);
  }
  if (is_wakeup) {
    wakeup();
  }
}

void Reactor::addTask(std::vector<std::function<void()>> task, bool is_wakeup /* =true*/) {

  if (task.size() == 0) {
    return;
  }

  {
    Mutex::Lock lock(m_mutex);
    m_pending_tasks.insert(m_pending_tasks.end(), task.begin(), task.end());
  }
  if (is_wakeup) {
    wakeup();
  }
}

// !!!
// 添加协程  ==>  把当前主协程切换到目的协程的事件打包
// 会在线程中取出tasks，task()执行函数，那么会进入cor的回调函数
// 如果在cor中也有read/write_hook()等利用coroutine的异步函数
// 接着toEpoll()会继续注册任务，再回调执行
void Reactor::addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup /*=true*/) {

  auto func = [cor](){
    tinyrpc::Coroutine::Resume(cor.get());
  };
  // ???
  // m_pending_tasks添加事件
  // 添加的事件放在副协程cor当中
  addTask(func, is_wakeup);
}

Timer* Reactor::getTimer() {
  // 为reactor添加timer, timer_fd
	if (!m_timer) {
		m_timer = new Timer(this);
		m_timer_fd = m_timer->getFd();
	}
	return m_timer;
}

pid_t Reactor::getTid() {
  return m_tid;
}

// 设置reactor类型
void Reactor::setReactorType(ReactorType type) {
  m_reactor_type = type;
}


/*
 * GetCoroutineTaskQueue()复用了static CoroutineTaskQueue *t_couroutine_task_queue
 * 因此增加pthread_once()函数在多线程环境中只创建一次
 */

static pthread_once_t once_CoroutineTaskQueue = PTHREAD_ONCE_INIT;
void allocateOneCoroutineTaskQueue() {
  t_couroutine_task_queue = new CoroutineTaskQueue();
}


CoroutineTaskQueue* CoroutineTaskQueue::GetCoroutineTaskQueue() {
  if (!t_couroutine_task_queue) {
    // t_couroutine_task_queue = new CoroutineTaskQueue();
    pthread_once(&once_CoroutineTaskQueue, allocateOneCoroutineTaskQueue);
  }
  return t_couroutine_task_queue;

}

void CoroutineTaskQueue::push(FdEvent* cor) {
  Mutex::Lock lock(m_mutex);
  m_task.push(cor);
}

FdEvent* CoroutineTaskQueue::pop() {
  FdEvent* re = nullptr;
  {
  Mutex::Lock lock(m_mutex);
  if (m_task.size() >= 1) {
    re = m_task.front();
    m_task.pop();
  }
  }

  return re;
}

}
