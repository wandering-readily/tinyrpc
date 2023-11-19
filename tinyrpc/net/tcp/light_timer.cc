#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "light_timer.h"
#include "tinyrpc/comm/log.h"

namespace tinyrpc {

// interval是毫秒
LightTimer::LightTimer(int interval, std::function<void(void)> cb, \
    LightTimerPool::sptr timerPool) \
    : cb_(cb), weakLightTimerPool_(timerPool) {

  sem_init(&sem_waitAddInLoop, 0, 0);
  sem_init(&sem_waitDelInLoop, 0, 0);
  fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);

  memset(&value_, 0, sizeof(value_));
  timespec ts;
  memset(&ts, 0, sizeof(ts));
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
  value_.it_value = ts;

}

bool LightTimer::registerInLoop() {
  LightTimerPool::sptr timerPool = weakLightTimerPool_.lock();
  assert(timerPool != nullptr && "timerPool had released");

  bool rt = timerPool->addLightTimer(shared_from_this());
  assert(rt && "timerfd_settime can't fail");
  if (!rt) [[unlikely]] {
    locateErrorExit
  }

  sem_wait(getwaitAddInLoopSem());
  added = true;

  int rs = timerfd_settime(fd_, 0, &value_, nullptr);
  if (rs != 0) [[unlikely]] {
    locateErrorExit
  }
  return true;
}

LightTimer::~LightTimer() {
  cancelCB();
  LightTimerPool::sptr timerPool = weakLightTimerPool_.lock();
  if (timerPool) {
    timerPool->delLightTimer(fd_);
  }
  if (added) {
    sem_wait(getwaitDelInLoopSem());
  }

  sem_destroy(getwaitAddInLoopSem());
  sem_destroy(getwaitDelInLoopSem());
  close(fd_);
}

bool LightTimer::resetTimer(std::function<void(void)> cb) {
  if (!called) {
    return false;
  }
  LightTimerPool::sptr lightTimerPool = weakLightTimerPool_.lock();
  if (!lightTimerPool) {
    return false;
  }
  cb_ = cb;
  called = false;

  int rs = timerfd_settime(fd_, 0, &value_, nullptr);
  if (rs != 0) [[unlikely]] {
    locateErrorExit
  }
  return true;

}

void LightTimer::cancel() {
  struct itimerspec new_value = {};
  memset(&value_, 0, sizeof(new_value));
  int rt = timerfd_settime(fd_, 0, &new_value, NULL);
  assert(rt != -1 && "timerfd_settime can't fail");
}

LightTimerPool::LightTimerPool() {
  wake_fd_ = eventfd(0, EFD_NONBLOCK);
  if(wake_fd_ <= 0 ) [[unlikely]] {
    locateErrorExit
  }

  epfd_ = epoll_create(1);
  if(epfd_ <= 0 ) [[unlikely]] {
    locateErrorExit
  }

  addWakeupFd();
  pthread_create(&poolThread_, nullptr, &LightTimerPool::Loop, this);
}

bool LightTimerPool::addLightTimer(LightTimer::sptr timer) {
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
  int rs = epoll_ctl(epfd_, op, wake_fd_, &event);
  if (rs != 0) [[unlikely]] {
    locateErrorExit
  }
}

void LightTimerPool::wakeup() {
  if (stop_) {
    return;
  }
  uint64_t tmp = 1;
  uint64_t* p = &tmp; 
  int writedBytes = write(wake_fd_, p, 8);
  if(writedBytes != 8) [[unlikely]] {
    locateErrorExit
  }
}

bool LightTimerPool::addLightTimerInLoop(int fd, LightTimer::sptr timer) {
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
    locateErrorExit
  }
  if (is_add) {
    lightTimers_[fd] = timer;
  }
  timeCancelChannels[fd] = timer->getwaitDelInLoopSem();

  sem_post(timer->getwaitAddInLoopSem());
  return true;

}

bool LightTimerPool::delLightTimerInLoop(int fd) {
  int op = EPOLL_CTL_DEL;
  
  int rs = epoll_ctl(epfd_, op, fd, nullptr);
  if (rs != 0) [[unlikely]] {
    locateErrorExit
  }

  lightTimers_.erase(fd);
  sem_t *cancelChannel = timeCancelChannels[fd];
  timeCancelChannels.erase(fd);
  sem_post(cancelChannel);
  return true;

}

void *LightTimerPool::Loop(void *arg) {
  LightTimerPool *loop= static_cast<LightTimerPool*> (arg);
  epoll_event re_events[loop->MAX_EVENTS + 1];
  
  while (!loop->stop_) {
    int rt = epoll_wait(loop->epfd_, re_events, loop->MAX_EVENTS, 0);
    int savedErrno = errno;
    if (rt < 0) [[unlikely]] {
      printf("errno %d, %s\n", errno, strerror(errno));
      if (savedErrno != EINTR) {
        locateErrorExit
      }

    } else {
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
          LightTimer::sptr timer;
          {
            tinyrpc::Mutex::Lock lock(loop->mutex_);
            if (loop->lightTimers_.find(fd) == loop->lightTimers_.end()) {
              continue;
            }
            timer = loop->lightTimers_[fd].lock();
          } 

          char buf[8];
          while(1) {
            if((read(loop->wake_fd_, buf, 8) == -1) && errno == EAGAIN) {
              break;
            }
          }

          if (timer) [[likely]] {
            timer->callback();
          }

        } else [[unlikely]] {
          // 排除eventfd, timerfd之后监听到的事件
          locateErrorExit
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
