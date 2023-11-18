#ifndef TINYRPC_NET_FD_EVNET_H
#define TINYRPC_NET_FD_EVNET_H

#include <functional>
#include <memory>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <assert.h>
#include "reactor.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "mutex.h"

namespace tinyrpc {

class Reactor;

enum IOEvent {
  READ = EPOLLIN,	
  WRITE = EPOLLOUT,  
  ETModel = EPOLLET,
};

/*
 * FdEvent:
 *          Reactor* m_reactor {nullptr};
 *          Coroutine* m_coroutine {nullptr};
 * 那么FdEvent直接持有这俩指针
 * 
 * Reactor* 可以从当前线程获取reactor指针
 * Coroutine* 则是直接设定，一般是线程正在使用的Coroutine*
 * 
 */
class FdEvent : public std::enable_shared_from_this<FdEvent> {
 public:

  typedef std::shared_ptr<FdEvent> sptr;
  
  FdEvent(tinyrpc::Reactor* reactor, int fd = -1);

  FdEvent(int fd);

  virtual ~FdEvent();

  void handleEvent(int flag);

  void setCallBack(IOEvent flag, std::function<void()> cb);

  std::function<void()> getCallBack(IOEvent flag) const;

  void addListenEvents(IOEvent event);

  void delListenEvents(IOEvent event);

  void updateToReactor();

  void unregisterFromReactor ();

  int getFd() const;

  void setFd(const int fd);

  int getListenEvents() const;

	Reactor* getReactor() const;

  void setReactor(Reactor* r);

  void setNonBlock();
  
  bool isNonBlock();

  void setCoroutine(Coroutine* cor);

  Coroutine* getCoroutine();

  void clearCoroutine();

 public:
	Mutex m_mutex;

 protected:
  int m_fd {-1};
  std::function<void()> m_read_callback;
  std::function<void()> m_write_callback;
  
  int m_listen_events {0};

  Reactor* m_reactor {nullptr};

  Coroutine* m_coroutine {nullptr};

  // 指示fd是否已经注册
  // 便于serverConnection, clientConnection用不用reactor
  // 为什么不用atomic_bool呢，因为这框架一个fd只用在一个thread上，因此是线程安全的
  bool fdInReactor {false};

};


class FdEventContainer {

 public:
  typedef std::shared_ptr<FdEventContainer> sptr;
  typedef std::weak_ptr<FdEventContainer> wptr;

 public:
  FdEventContainer(int size);

  FdEvent::sptr getFdEvent(int fd); 

 public:

 private:
  // 用哈希表是不是性能更好?
  RWMutex m_mutex;
  std::vector<FdEvent::sptr> m_fds;

};

}

#endif
