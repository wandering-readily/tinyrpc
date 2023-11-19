#ifndef TINYRPC_NET_TCP_LIGHTTIMER_H
#define TINYRPC_NET_TCP_LIGHTTIMER_H

#include <memory>
#include <google/protobuf/service.h>
#include <semaphore.h>
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {

class LightTimerPool;

class LightTimer : public std::enable_shared_from_this<LightTimer> {

public:
  typedef std::shared_ptr<LightTimer> sptr;
  typedef std::weak_ptr<LightTimerPool> wptr;

public:
  friend class LightTimerPool;

public:
  // interval是毫秒
  LightTimer(int, std::function<void(void)>, std::shared_ptr<LightTimerPool>);
  ~LightTimer();

  bool registerInLoop();

  bool resetTimer(std::function<void(void)>);

  void cancelCB() {
    cb_ = [](){};
    called = true;;
  }

private:

  int getFd() const {return fd_;}

  void cancel();

  void callback() {
    cb_();
    cancelCB();
    cancel();
  }

  sem_t *getwaitAddInLoopSem() {return &sem_waitAddInLoop;}

private:
  bool called = false;
  int fd_;
  sem_t sem_waitAddInLoop;
  itimerspec value_;
  std::function<void(void)> cb_;
  std::weak_ptr<LightTimerPool> weakLightTimerPool_;

};

class LightTimerPool {

public:
  typedef std::shared_ptr<LightTimerPool> sptr;
  typedef std::weak_ptr<LightTimerPool> wptr;

private:
  using PISP = std::pair<int, LightTimer::sptr>;

public:
  LightTimerPool();
  ~LightTimerPool();

  bool addLightTimer(LightTimer::sptr);
  bool delLightTimer(int);

private:

  void addWakeupFd();
  void wakeup();

  bool addLightTimerInLoop(int, LightTimer::sptr);
  bool delLightTimerInLoop(int);

  static void *Loop(void *);
  
private:
  const int MAX_EVENTS = 10;
  int epfd_;
  int wake_fd_;
  bool stop_ = false;
  pthread_t poolThread_;

  tinyrpc::Mutex mutex_;
  std::map<int, std::weak_ptr<LightTimer>> lightTimers_;

  std::vector<PISP> pending_add_fds_;
  std::vector<PISP> pending_del_fds_;

};

};

#endif