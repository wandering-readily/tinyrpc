#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "light_timer.h"
#include "tinyrpc/comm/log.h"

namespace tinyrpc {

// interval是毫秒
LightTimer::LightTimer(int interval, std::function<void(void)> cb, \
    std::shared_ptr<LightTimerPool> timerPool) \
    : cb_(cb), weakLightTimerPool_(timerPool) {

  sem_init(&sem_waitAddInLoop, 0, 0);
  fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);

  memset(&value_, 0, sizeof(value_));
  timespec ts;
  memset(&ts, 0, sizeof(ts));
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
  value_.it_value = ts;

}

bool LightTimer::registerInLoop() {
  std::shared_ptr<LightTimerPool> timerPool = weakLightTimerPool_.lock();
  if (!timerPool) [[unlikely]] {
    return false;
  }
  if (!timerPool->addLightTimer(shared_from_this())) [[unlikely]] {
    throw "can't add LightTimer";
    tinyrpc::Exit(0);
  }
  sem_wait(getwaitAddInLoopSem());
  if (timerfd_settime(fd_, 0, &value_, nullptr) != 0) [[unlikely]] {
    throw "can't set timer settime";
    tinyrpc::Exit(0);
  }
  return true;
}

LightTimer::~LightTimer() {
  std::shared_ptr<LightTimerPool> timerPool = weakLightTimerPool_.lock();
  if (timerPool) {
    timerPool->delLightTimer(fd_);
  }
  sem_destroy(getwaitAddInLoopSem());
  close(fd_);
}

bool LightTimer::resetTimer(std::function<void(void)> cb) {
  if (!called) {
    return false;
  }
  std::shared_ptr<LightTimerPool> lightTimerPool = weakLightTimerPool_.lock();
  if (!lightTimerPool) {
    return false;
  }
  cb_ = cb;
  called = false;
  if (!lightTimerPool->addLightTimer(shared_from_this())) [[unlikely]] {
    throw "can't add LightTimer";
    tinyrpc::Exit(0);
  }
  if (timerfd_settime(fd_, 0, &value_, nullptr) != 0) [[unlikely]] {
    throw "can't timer settime";
    tinyrpc::Exit(0);
  }
  return true;

}

LightTimerPool::LightTimerPool() {
  if((wake_fd_ = eventfd(0, EFD_NONBLOCK)) <= 0 ) [[unlikely]] {
    throw "can't lightTimerPool create eventfd";
    tinyrpc::Exit(0);
  }
  if((epfd_ = epoll_create(1)) <= 0 ) [[unlikely]] {
    throw "can't create epoll";
    tinyrpc::Exit(0);
  }
  addWakeupFd();
  pthread_create(&poolThread_, nullptr, &LightTimerPool::Loop, this);
}

bool LightTimerPool::addLightTimer(std::shared_ptr<LightTimer> timer) {
  {
    PISP event{timer->getFd(), timer};
    tinyrpc::Mutex::Lock lock(mutex_);
    pending_add_fds_.push_back(std::move(event));
  }
  wakeup();
  return true;
}

bool LightTimerPool::LightTimerPool::delLightTimer(int fd) {
  {
    tinyrpc::Mutex::Lock lock(mutex_);
    if (lightTimers_.find(fd) == lightTimers_.end()) {
      return true;
    }
  }
  {
    PISP event{fd, nullptr};
    tinyrpc::Mutex::Lock lock(mutex_);
    pending_del_fds_.push_back(std::move(event));
  }
  wakeup();

  return true;
}

LightTimerPool::~LightTimerPool() {
  stop_ = true;
  pthread_join(poolThread_, nullptr);
  close(wake_fd_);
  close(epfd_);
}

void LightTimerPool::addWakeupFd() {
  int op = EPOLL_CTL_ADD;
  epoll_event event;
  event.data.fd = wake_fd_;
  event.events = EPOLLIN;
  if ((epoll_ctl(epfd_, op, wake_fd_, &event)) != 0) [[unlikely]] {
    throw "can't lightTimerPool epoll add wakefd";
    tinyrpc::Exit(0);
  }
}

void LightTimerPool::wakeup() {
  if (stop_) {
    return;
  }
  uint64_t tmp = 1;
  uint64_t* p = &tmp; 
  if(write(wake_fd_, p, 8) != 8) [[unlikely]] {
    throw "can't lightTimerPool write wake_fd 8 bytes";
    tinyrpc::Exit(0);
  }
}

bool LightTimerPool::addLightTimerInLoop(int fd, std::shared_ptr<LightTimer> timer) {
  int op = EPOLL_CTL_ADD;
  bool is_add = true;
  if (lightTimers_.find(fd) != lightTimers_.end()) {
    // 还可能再次启动timer
    is_add = false;
    op = EPOLL_CTL_MOD;
  }

  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN;

  if (epoll_ctl(epfd_, op, fd, &event) != 0) {
    return false;
  }
  if (is_add) {
    lightTimers_[fd] = timer;
  }
  sem_post(timer->getwaitAddInLoopSem());
  return true;

}

bool LightTimerPool::delLightTimerInLoop(int fd) {
  int op = EPOLL_CTL_DEL;
  
  if (epoll_ctl(epfd_, op, fd, nullptr) != 0) [[unlikely]] {
    return false;
  }
  return true;

}

void *LightTimerPool::Loop(void *arg) {
  LightTimerPool *loop= static_cast<LightTimerPool*> (arg);
  epoll_event re_events[loop->MAX_EVENTS + 1];
  
  while (!loop->stop_) {
    int rt = epoll_wait(loop->epfd_, re_events, loop->MAX_EVENTS, 0);
    if (rt < 0) [[unlikely]] {
      printf("errno %d, %s\n", errno, strerror(errno));
      tinyrpc::Exit(0);
    } else {

      auto &timers = loop->lightTimers_;
      for (int i = 0; i < rt; ++i) {
        epoll_event one_event = re_events[i];	

        if (one_event.data.fd == loop->wake_fd_ && (one_event.events & EPOLLIN)) {
          char buf[8];
          while(1) {
            if((read(loop->wake_fd_, buf, 8) == -1) && errno == EAGAIN) {
              break;
            }
          }
          continue;
        }

        if (one_event.events & EPOLLIN) [[likely]] {
          int fd = one_event.data.fd;
          if (timers.find(fd) == timers.end()) {
            continue;
          } 

          char buf[8];
          while(1) {
            if((read(loop->wake_fd_, buf, 8) == -1) && errno == EAGAIN) {
              break;
            }
          }

          std::shared_ptr<LightTimer> timer = timers[fd].lock();
          if (timer) [[likely]] {
            timer->callback();
          }

        } else [[unlikely]] {
          // 排除eventfd, timerfd之后监听到的事件
          throw "can't lightTimerPool use not registered timerfd";
          tinyrpc::Exit(0);
        }
      }

      // 处理timer所在线程发来的事件
      std::vector<PISP> tmpAddArr;
      {
        tinyrpc::Mutex::Lock lock(loop->mutex_);
        tmpAddArr.swap(loop->pending_add_fds_);
      }
      for (size_t i = 0; i < tmpAddArr.size(); i++) {
        loop->addLightTimerInLoop(tmpAddArr[i].first, tmpAddArr[i].second);
      }

      std::vector<PISP> tmpDelArr;
      {
        tinyrpc::Mutex::Lock lock(loop->mutex_);
        tmpDelArr.swap(loop->pending_del_fds_);
      }
      for (size_t i = 0; i < tmpDelArr.size(); i++) {
        loop->delLightTimerInLoop(tmpDelArr[i].first);
      }
    }
  }

  return nullptr;
}
  
};
