#include <memory>
#include <map>
#include <time.h>
#include <stdlib.h>
#include <semaphore.h>
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/comm/config.h"


namespace tinyrpc {

// thread_local类型变量
static thread_local Reactor* t_reactor_ptr = nullptr;

static thread_local IOThread* t_cur_io_thread = nullptr;


IOThread::IOThread(std::weak_ptr<CoroutineTaskQueue> corTaskQueue)
    : weakCorTaskQueue_(corTaskQueue) {

  int rt = sem_init(&m_init_semaphore, 0, 0);
  assert(rt==0);

  rt = sem_init(&m_start_semaphore, 0, 0);
  assert(rt==0);

  pthread_create(&m_thread, nullptr, &IOThread::main, this);

  RpcDebugLog << "semaphore begin to wait until new thread frinish IOThread::main() to init";
  // wait until new thread finish IOThread::main() func to init 
  rt = sem_wait(&m_init_semaphore);
  assert(rt == 0);
  RpcDebugLog << "semaphore wait end, finish create io thread";

  sem_destroy(&m_init_semaphore);
}

IOThread::~IOThread() {
  m_reactor->stop();
  pthread_join(m_thread, nullptr);

  if (m_reactor != nullptr) {

    delete m_reactor;
    m_reactor = nullptr;
  }
}

IOThread* IOThread::GetCurrentIOThread() {
  return t_cur_io_thread;
}

sem_t* IOThread::getStartSemaphore() {
  return &m_start_semaphore;
}

Reactor* IOThread::getReactor() {
  return m_reactor;
}

pthread_t IOThread::getPthreadId() {
  return m_thread;
}

void IOThread::setThreadIndex(const int index) {
  m_index = index;
}

int IOThread::getThreadIndex() {
  return m_index;
}

void* IOThread::main(void* arg) {
  // assert(t_reactor_ptr == nullptr);

  t_reactor_ptr = new Reactor();
  assert(t_reactor_ptr != NULL);

  IOThread* thread = static_cast<IOThread*>(arg);
  t_cur_io_thread = thread;
  thread->m_reactor = t_reactor_ptr;
  // IO thread reactor设置为SubReactor
  thread->m_reactor->setReactorType(ReactorType::SubReactor);
  thread->m_tid = gettid();

  // 这里至关重要
  // 创建当前线程的t_main_coroutine
  Coroutine::GetCurrentCoroutine();

  RpcDebugLog << "finish iothread init, now post semaphore";
  // IOThread()构造函数会等待m_init_semaphore
  sem_post(&thread->m_init_semaphore);

  // wait for main thread post m_start_semaphore to start iothread loop
  // 等待IOThreadPool释放信息 ThreadPool().start()
  sem_wait(&thread->m_start_semaphore);

  sem_destroy(&thread->m_start_semaphore);

  RpcDebugLog << "IOThread " << thread->m_tid << " begin to loop";
  // !!!
  // 开始循环
  t_reactor_ptr->loop();

  return nullptr;
}


IOThreadPool::IOThreadPool(int size, std::weak_ptr<CoroutinePool> corPool) 
    : m_size(size), weakCorPool_(corPool) {
  m_io_threads.resize(size);
}

void IOThreadPool::beginThreadPool(std::weak_ptr<CoroutineTaskQueue> corTaskQueue) {
  for (int i = 0; i < m_size; ++i) {
    m_io_threads[i] = std::make_shared<IOThread>(corTaskQueue);
    m_io_threads[i]->setThreadIndex(i);
  }
}

void IOThreadPool::start() {
  // 关联IOThrea::main()的m_start_semaphore
  for (int i = 0; i < m_size; ++i) {
    int rt = sem_post(m_io_threads[i]->getStartSemaphore());
    assert(rt == 0);
  }
}

IOThread* IOThreadPool::getIOThread() {
  // 轮询获得IOThread
  // -1是std::atomic<int>超界之后的选择
  if (m_index == m_size || m_index == -1) {
    m_index = 0;
  }
  return m_io_threads[m_index++].get();
}


int IOThreadPool::getIOThreadPoolSize() {
  return m_size;
}

void IOThreadPool::broadcastTask(std::function<void()> cb) {
  // ???
  // 每个IOThread添加cb?
  for (auto i : m_io_threads) {
    i->getReactor()->addTask(cb, true);
  }
}

void IOThreadPool::addTaskByIndex(int index, std::function<void()> cb) {
  // 特定线程添加cb
  if (index >= 0 && index < m_size) {
    m_io_threads[index]->getReactor()->addTask(cb, true);
  }
}

tinyrpc::IOThread::ptr IOThreadPool::getRandomThread(bool self /* = false*/) {
  if (m_size == 1) {
    return m_io_threads[0];
  }
  srand(time(0));
  int i = 0;
  while (1) {
    i = rand() % (m_size);
    if (!self && m_io_threads[i]->getPthreadId() == t_cur_io_thread->getPthreadId()) {
      i++;
      if (i == m_size) {
        i -= 2;
      }
    }
    break;
  }
  return m_io_threads[i];
}

// 添加Coroutine
void IOThreadPool::addCoroutineToRandomThread(Coroutine::ptr cor, bool self /* = false*/) {
  if (m_size == 1) {
    m_io_threads[0]->getReactor()->addCoroutine(cor, true);
    return;
  }
  srand(time(0));
  tinyrpc::IOThread::ptr thread = getRandomThread(self);
  thread->getReactor()->addCoroutine(cor, true);
}


Coroutine::ptr IOThreadPool::addCoroutineToRandomThread(std::function<void()> cb, bool self/* = false*/) {
  // 获取一个Coroutine
  std::shared_ptr<tinyrpc::CoroutinePool> corPool = weakCorPool_.lock();
	if (!corPool) [[unlikely]]
	{
		Exit(0);
	}
  Coroutine::ptr cor = corPool->getCoroutineInstanse();
  // 启用协程
  cor->setCallBack(cb);
  // 协程内容添加到random thread，而这个self决定是否一定要发生在curIOThread
  addCoroutineToRandomThread(cor, self);
  return cor;
}

// 根据IOThread index添加协程内容
Coroutine::ptr IOThreadPool::addCoroutineToThreadByIndex(int index, std::function<void()> cb, bool self/* = false*/) {
  if (index >= (int)m_io_threads.size() || index < 0) {
    RpcErrorLog << "addCoroutineToThreadByIndex error, invalid iothread index[" << index << "]";
    return nullptr;
  }

  std::shared_ptr<tinyrpc::CoroutinePool> corPool = weakCorPool_.lock();
	if (!corPool) [[unlikely]]
	{
		Exit(0);
	}
  Coroutine::ptr cor = corPool->getCoroutineInstanse();
  cor->setCallBack(cb);
  m_io_threads[index]->getReactor()->addCoroutine(cor, true);
  return cor;

}

// 每个线程添加协程内容
void IOThreadPool::addCoroutineToEachThread(std::function<void()> cb) {
  std::shared_ptr<tinyrpc::CoroutinePool> corPool = weakCorPool_.lock();
	if (!corPool) [[unlikely]]
	{
		Exit(0);
	}
  for (auto i : m_io_threads) {
    Coroutine::ptr cor = corPool->getCoroutineInstanse();
    cor->setCallBack(cb);
    i->getReactor()->addCoroutine(cor, true);
  }
}


}