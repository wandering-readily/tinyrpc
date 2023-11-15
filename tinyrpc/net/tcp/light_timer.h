#ifndef TINYRPC_NET_LIGHTTIMER_H
#define TINYRPC_NET_LIGHTTIMER_H

#include <memory>
#include <google/protobuf/service.h>
#include <semaphore.h>
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {

class LightTimerPool;

class LightTimer : public std::enable_shared_from_this<LightTimer> {

public:
  friend class LightTimerPool;

public:
  // interval是毫秒
  LightTimer(int, std::function<void(void)>, std::shared_ptr<LightTimerPool>);
  ~LightTimer();

  bool registerInLoop();

  bool resetTimer(std::function<void(void)>);

private:

  int getFd() const {return fd_;}

  void callback() {
    cb_();
    called = true;
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

private:
  using PISP = std::pair<int, std::shared_ptr<LightTimer>>;

public:
  LightTimerPool();
  ~LightTimerPool();

  bool addLightTimer(std::shared_ptr<LightTimer>);
  bool delLightTimer(int);

private:

  void addWakeupFd();
  void wakeup();

  bool addLightTimerInLoop(int, std::shared_ptr<LightTimer>);
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