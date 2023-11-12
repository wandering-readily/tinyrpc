#ifndef TINYRPC_NET_EVENT_LOOP_H
#define TINYRPC_NET_EVENT_LOOP_H

#include <sys/socket.h>
#include <sys/types.h>
#include <vector>
#include <atomic>
#include <map>
#include <functional>
#include <queue>
#include "tinyrpc/coroutine/coroutine.h"
#include "fd_event.h"
#include "mutex.h"

namespace tinyrpc {

/*
 * 对于 MainReactor: 只会注册 listenfd 的可读事件。
 * 也就是说，不会注册任何 clientfd 到MainReactor 上。
 * MainReactor 只负责 accept 新连接，而不关心数据交互。
 * 每 accept 一个fd 后，就把他交给某个 SubReactor.
 * 
 * 对于 SubReactor，它注册的是 clientfd，因此它负责跟客户端进行数据交互。
 */
enum class ReactorType {
  MainReactor = 1,    // main rewactor, only set this by main thread.
  SubReactor = 2      // child reactor, every io thread is this type
};

class FdEvent;
class Timer;
class CoroutineTaskQueue;

// typedef std::shared_ptr<Timer> TimerPtr;

class Reactor {

 public:

  typedef std::shared_ptr<Reactor> ptr;

  explicit Reactor();

  ~Reactor();

  void addEvent(int fd, epoll_event event, bool is_wakeup = true);

  void delEvent(int fd, bool is_wakeup = true);

  void addTask(std::function<void()> task, bool is_wakeup = true);

  void addTask(std::vector<std::function<void()>> task, bool is_wakeup = true);
  
  void addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup = true);

  void wakeup();
  
  void loop();

  void stop();

  Timer* getTimer();

  pid_t getTid();

  void setReactorType(ReactorType type);
 
 public:
  static Reactor* GetReactor();
  

 private:

  void addWakeupFd();

  bool isLoopThread() const;

  void addEventInLoopThread(int fd, epoll_event event);

  void delEventInLoopThread(int fd);
  
// reactor添加epoll_fd, wake_fd, timer, tasks
 private:
  int m_epfd {-1};
  int m_wake_fd {-1};         // wakeup fd
  int m_timer_fd {-1};        // timer fd
  bool m_stop_flag {false};
  bool m_is_looping {false};
  bool m_is_init_timer {false};
  pid_t m_tid {0};        // thread id

  Mutex m_mutex;                    // mutex
  
  std::vector<int> m_fds;              // alrady care events
  std::atomic<int> m_fd_size; 

  // fds that wait for operate
  // 1 -- to add to loop
  // 2 -- to del from loop
  std::map<int, epoll_event> m_pending_add_fds;
  std::vector<int> m_pending_del_fds;
  std::vector<std::function<void()>> m_pending_tasks;

  Timer* m_timer {nullptr};

  ReactorType m_reactor_type {ReactorType::SubReactor};

  std::weak_ptr<CoroutineTaskQueue> weakCorTaskQueue_;

};


class CoroutineTaskQueue {
 public:

  void push(FdEvent* fd);

  FdEvent* pop();

 private:
  std::queue<FdEvent*> m_task;
  Mutex m_mutex;                    // mutex
};


}


#endif
