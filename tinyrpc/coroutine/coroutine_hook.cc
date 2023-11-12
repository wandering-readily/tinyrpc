#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <atomic>
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"

// name ==> read
// name##_fun_ptr_t ==> read_fun_ptr_t ==> typedef ssize_t (*read_fun_ptr_t)(int fd, void *buf, size_t count);
// name##_fun_ptr_t是函数类型
// dlsym ==> 从共享库（动态库）中获取符号（全局变量与函数符号）地址，通常用于获取函数符号地址
// RTLD_DEFAULT表示按默认的顺序搜索共享库中符号symbol第一次出现的地址
// RTLD_NEXT表示在当前库以后按默认的顺序搜索共享库中符号symbol第一次出现的地址
#define HOOK_SYS_FUNC(name) name##_fun_ptr_t g_sys_##name##_fun = (name##_fun_ptr_t)dlsym(RTLD_NEXT, #name);


HOOK_SYS_FUNC(accept);
HOOK_SYS_FUNC(read);
HOOK_SYS_FUNC(write);
HOOK_SYS_FUNC(connect);
HOOK_SYS_FUNC(sleep);

// static int g_hook_enable = false;

// static int g_max_timeout = 75000;


namespace tinyrpc {

static std::atomic_int m_max_connect_timeout = 75;
static std::atomic_bool m_max_connect_timeout_inited = false;
bool Init_m_max_connect_timeout(int max_connect_timeout) {
  if (!m_max_connect_timeout_inited) {
    m_max_connect_timeout = max_connect_timeout;
    m_max_connect_timeout_inited = true;
	return true;
  }
  return false;
}

static bool g_hook = true;

void SetHook(bool value) {
	g_hook = value;
}

// 查看监听事件
void toEpoll(tinyrpc::FdEvent::ptr fd_event, int events) {
	
	tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine() ;
	if (events & tinyrpc::IOEvent::READ) {
		RpcDebugLog << "fd:[" << fd_event->getFd() << "], register read event to epoll";
		// fd_event->setCallBack(tinyrpc::IOEvent::READ, 
		// 	[cur_cor, fd_event]() {
		// 		tinyrpc::Coroutine::Resume(cur_cor);
		// 	}
		// );
		// 监听事件要设置coroutine, 因为coroutine完成的是监听任务
		fd_event->setCoroutine(cur_cor);
		fd_event->addListenEvents(tinyrpc::IOEvent::READ);
	}
	if (events & tinyrpc::IOEvent::WRITE) {
		RpcDebugLog << "fd:[" << fd_event->getFd() << "], register write event to epoll";
		// fd_event->setCallBack(tinyrpc::IOEvent::WRITE, 
		// 	[cur_cor]() {
		// 		tinyrpc::Coroutine::Resume(cur_cor);
		// 	}
		// );
		fd_event->setCoroutine(cur_cor);
		fd_event->addListenEvents(tinyrpc::IOEvent::WRITE);
	}
	// fd_event->updateToReactor();
}

ssize_t read_hook(tinyrpc::FdEvent::ptr fd_event, void *buf, size_t count) {
	RpcDebugLog << "this is hook read";
  // 主协程直接调用系统函数
  // 否则要在reactor上监听
  int fd = fd_event->getFd();
  if (tinyrpc::Coroutine::IsMainCoroutine()) {
    RpcDebugLog << "hook disable, call sys read func";
    return g_sys_read_fun(fd, buf, count);
  }

	tinyrpc::Reactor::GetReactor();
	// assert(reactor != nullptr);

  if(fd_event->getReactor() == nullptr) {
    fd_event->setReactor(tinyrpc::Reactor::GetReactor());  
  }

	// if (fd_event->isNonBlock()) {
		// RpcDebugLog << "user set nonblock, call sys func";
		// return g_sys_read_fun(fd, buf, count);
	// }

	fd_event->setNonBlock();

	// must fitst register read event on epoll
	// because reactor should always care read event when a connection sockfd was created
	// so if first call sys read, and read return success, this fucntion will not register read event and return
	// for this connection sockfd, reactor will never care read event
  ssize_t n = g_sys_read_fun(fd, buf, count);
  if (n > 0) {
    return n;
  } 

	// 设置当前协程事件
	toEpoll(fd_event, tinyrpc::IOEvent::READ);

	RpcDebugLog << "read func to yield";
	// 切换主协程, 从这里退出
	tinyrpc::Coroutine::Yield();
	// 换函数栈，等再一次Resume(*Coroutine)，再次进入这个位置

	// 取消关注的事件类型
	fd_event->delListenEvents(tinyrpc::IOEvent::READ);
	// 取消事件关联协程
	fd_event->clearCoroutine();
	// fd_event->updateToReactor();

	RpcDebugLog << "read func yield back, now to call sys read";
	return g_sys_read_fun(fd, buf, count);

}

// server主线程调用的accept_hook
int accept_hook(tinyrpc::FdEvent::ptr fd_event, struct sockaddr *addr, socklen_t *addrlen) {
	RpcDebugLog << "this is hook accept";
  int sockfd = fd_event->getFd();
  if (tinyrpc::Coroutine::IsMainCoroutine()) {
    RpcDebugLog << "hook disable, call sys accept func";
    return g_sys_accept_fun(sockfd, addr, addrlen);
  }
	tinyrpc::Reactor::GetReactor();
	// assert(reactor != nullptr);

  if(fd_event->getReactor() == nullptr) {
    fd_event->setReactor(tinyrpc::Reactor::GetReactor());  
  }

	// if (fd_event->isNonBlock()) {
		// RpcDebugLog << "user set nonblock, call sys func";
		// return g_sys_accept_fun(sockfd, addr, addrlen);
	// }

	fd_event->setNonBlock();

  int n = g_sys_accept_fun(sockfd, addr, addrlen);
  if (n > 0) {
    return n;
  } 

	toEpoll(fd_event, tinyrpc::IOEvent::READ);
	
	RpcDebugLog << "accept func to yield";
	tinyrpc::Coroutine::Yield();

	// 取消关注的事件类型
	fd_event->delListenEvents(tinyrpc::IOEvent::READ);
	// 取消事件关联协程
	fd_event->clearCoroutine();
	// fd_event->updateToReactor();

	RpcDebugLog << "accept func yield back, now to call sys accept";
	return g_sys_accept_fun(sockfd, addr, addrlen);

}

ssize_t write_hook(tinyrpc::FdEvent::ptr fd_event, const void *buf, size_t count) {
	RpcDebugLog << "this is hook write";
  int fd = fd_event->getFd();
  if (tinyrpc::Coroutine::IsMainCoroutine()) {
    RpcDebugLog << "hook disable, call sys write func";
    return g_sys_write_fun(fd, buf, count);
  }
	tinyrpc::Reactor::GetReactor();
	// assert(reactor != nullptr);

  if(fd_event->getReactor() == nullptr) {
    fd_event->setReactor(tinyrpc::Reactor::GetReactor());  
  }

	// if (fd_event->isNonBlock()) {
		// RpcDebugLog << "user set nonblock, call sys func";
		// return g_sys_write_fun(fd, buf, count);
	// }

	fd_event->setNonBlock();

  ssize_t n = g_sys_write_fun(fd, buf, count);
  if (n > 0) {
    return n;
  }

	toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

	RpcDebugLog << "write func to yield";
	tinyrpc::Coroutine::Yield();

	// 取消关注的事件类型
	fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
	// 取消事件关联协程
	fd_event->clearCoroutine();
	// fd_event->updateToReactor();

	RpcDebugLog << "write func yield back, now to call sys write";
	return g_sys_write_fun(fd, buf, count);

}

// client调用, 那么tinyrpc::Coroutine::IsMainCoroutine()==true, 即不进入协程圈
int connect_hook(tinyrpc::FdEvent::ptr fd_event, const struct sockaddr *addr, socklen_t addrlen) {
	RpcDebugLog << "this is hook connect";
  int sockfd = fd_event->getFd();
  if (tinyrpc::Coroutine::IsMainCoroutine()) {
    RpcDebugLog << "hook disable, call sys connect func";
    return g_sys_connect_fun(sockfd, addr, addrlen);
  }
	tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
	// assert(reactor != nullptr);

  if(fd_event->getReactor() == nullptr) {
    fd_event->setReactor(reactor);  
  }
	tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();

	// if (fd_event->isNonBlock()) {
		// RpcDebugLog << "user set nonblock, call sys func";
    // return g_sys_connect_fun(sockfd, addr, addrlen);
	// }
	
	fd_event->setNonBlock();
  int n = g_sys_connect_fun(sockfd, addr, addrlen);
  if (n == 0) {
    RpcDebugLog << "direct connect succ, return";
    return n;
  } else if (errno != EINPROGRESS) {
		RpcDebugLog << "connect error and errno is't EINPROGRESS, errno=" << errno <<  ",error=" << strerror(errno);
    return n;
  }

	RpcDebugLog << "errno == EINPROGRESS";

  toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

	bool is_timeout = false;		// 是否超时

	// 超时函数句柄
  auto timeout_cb = [&is_timeout, cur_cor](){
		// 设置超时标志，然后唤醒协程
		is_timeout = true;
		tinyrpc::Coroutine::Resume(cur_cor);
  };

  tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(m_max_connect_timeout, false, timeout_cb);
  
  tinyrpc::Timer* timer = reactor->getTimer();  
  timer->addTimerEvent(event);

  tinyrpc::Coroutine::Yield();

	// 取消关注的事件类型
	fd_event->delListenEvents(tinyrpc::IOEvent::WRITE); 
	// 取消事件关联协程
	fd_event->clearCoroutine();
	// fd_event->updateToReactor();

	// 定时器也需要删除
	timer->delTimerEvent(event);

	n = g_sys_connect_fun(sockfd, addr, addrlen);
	if ((n < 0 && errno == EISCONN) || n == 0) {
		RpcDebugLog << "connect succ";
		return 0;
	}

	if (is_timeout) {
    RpcErrorLog << "connect error,  timeout[ " << m_max_connect_timeout << "ms]";
		errno = ETIMEDOUT;
	} 

	RpcDebugLog << "connect error and errno=" << errno <<  ", error=" << strerror(errno);
	return -1;

}

unsigned int sleep_hook(unsigned int seconds) {

	RpcDebugLog << "this is hook sleep";
  if (tinyrpc::Coroutine::IsMainCoroutine()) {
    RpcDebugLog << "hook disable, call sys sleep func";
    return g_sys_sleep_fun(seconds);
  }

	tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();

	bool is_timeout = false;
	auto timeout_cb = [cur_cor, &is_timeout](){
		RpcDebugLog << "onTime, now resume sleep cor";
		is_timeout = true;
		// 设置超时标志，然后唤醒协程
		tinyrpc::Coroutine::Resume(cur_cor);
  };

  tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(1000 * seconds, false, timeout_cb);
  
  // 进入非主线程的reactor(执行函数流的线程), 然后可以切换协程
  tinyrpc::Reactor::GetReactor()->getTimer()->addTimerEvent(event);

	RpcDebugLog << "now to yield sleep";
	// beacuse read or wirte maybe resume this coroutine, so when this cor be resumed, must check is timeout, otherwise should yield again
	while (!is_timeout) {
		tinyrpc::Coroutine::Yield();
	}

	// 定时器也需要删除
	// tinyrpc::Reactor::GetReactor()->getTimer()->delTimerEvent(event);

	return 0;

}


}


// extern "C" {


// // 如果设置了钩子，那么使用accept_hook，否则使用g_sys_fun=>从共享库中调用系统函数
// int accept(tinyrpc::FdEvent::ptr fd_event, struct sockaddr *addr, socklen_t *addrlen) {
	// if (!tinyrpc::g_hook) {
		// return g_sys_accept_fun(fd_event->getFd(), addr, addrlen);
	// } else {
		// return tinyrpc::accept_hook(fd_event, addr, addrlen);
	// }
// }

// ssize_t read(tinyrpc::FdEvent::ptr fd_event, void *buf, size_t count) {
	// if (!tinyrpc::g_hook) {
		// return g_sys_read_fun(fd_event->getFd(), buf, count);
	// } else {
		// return tinyrpc::read_hook(fd_event, buf, count);
	// }
// }

// ssize_t write(tinyrpc::FdEvent::ptr fd_event, const void *buf, size_t count) {
	// if (!tinyrpc::g_hook) {
		// return g_sys_write_fun(fd_event->getFd(), buf, count);
	// } else {
		// return tinyrpc::write_hook(fd_event, buf, count);
	// }
// }

// int connect(tinyrpc::FdEvent::ptr fd_event, const struct sockaddr *addr, socklen_t addrlen) {
	// if (!tinyrpc::g_hook) {
		// return g_sys_connect_fun(fd_event->getFd(), addr, addrlen);
	// } else {
		// return tinyrpc::connect_hook(fd_event, addr, addrlen);
	// }
// }

// unsigned int sleep(unsigned int seconds) {
	// if (!tinyrpc::g_hook) {
		// return g_sys_sleep_fun(seconds);
	// } else {
		// return tinyrpc::sleep_hook(seconds);
	// }
// }

// }
