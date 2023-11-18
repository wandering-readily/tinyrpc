#include <sys/timerfd.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <vector>
#include <sys/time.h>
#include <functional>
#include <map>
// #include "../comm/log.h"
#include "tinyrpc/comm/log.h"
#include "timer.h"
#include "mutex.h"
#include "fd_event.h"
// #include "../coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_hook.h"


extern read_fun_ptr_t g_sys_read_fun;  // sys read func

namespace details {

// 线程安全函数
int64_t getNowMs() {
  timeval val;
  gettimeofday(&val, nullptr);
  int64_t re = val.tv_sec * 1000 + val.tv_usec / 1000;
  return re;
}

};

namespace tinyrpc {

Timer::Timer(Reactor* reactor) : FdEvent(reactor) {

  m_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
  RpcDebugLog << "m_timer fd = " << m_fd;
  if (m_fd == -1) {
    RpcDebugLog << "timerfd_create error";  
  }
  // RpcDebugLog << "timerfd is [" << m_fd << "]";
	m_read_callback = std::bind(&Timer::onTimer, this);
  addListenEvents(READ);
  // updateToReactor();

}

Timer::~Timer() {
  unregisterFromReactor();
	close(m_fd);
}


void Timer::addTimerEvent(TimerEvent::sptr event, bool need_reset /*=true*/) {
  bool is_reset = false;
  {
  RWMutex::WriteLock lock(m_event_mutex);
  if (m_pending_events.empty()) {
    is_reset = true;
  } else {
		auto it = m_pending_events.begin();
    if (event->m_arrive_time < (*it).second->m_arrive_time) {
      is_reset = true;
    }
  }
  m_pending_events.emplace(event->m_arrive_time, event);
  }

  if (is_reset && need_reset) {
    RpcDebugLog << "need reset timer";
    resetArriveTime();
  }
  // RpcDebugLog << "add timer event succ";
}

// ???
// timerEvent需不需要加锁确保多线程安全?
void Timer::delTimerEvent(TimerEvent::sptr event) {
  // 在timer中删除事件
  // 同时也要在事件上标记删除
  // event->m_is_cancled = true;
  event->cancle();
  // ???
  // 这段代码原作没有
  // 但是如果一个timerevent删除，那么也应该不重复了
  // event->m_is_repeated = false;
  // event->cancleRepeated();
  // 不应该啊
  // 因为cancled, repeated不应该相互影响?

  {
  RWMutex::WriteLock lock(m_event_mutex);
  auto begin = m_pending_events.lower_bound(event->m_arrive_time);
  auto end = m_pending_events.upper_bound(event->m_arrive_time);
  auto it = begin;
  for (it = begin; it != end; it++) {
    if (it->second == event) {
      RpcDebugLog << "find timer event, now delete it. src arrive time=" << event->m_arrive_time;
      break;
    }
  }
  // 可能这个事件先设置m_is_cancled，等ontimer()可能会从multimap中取出来了
  if (it != m_pending_events.end()) {
    m_pending_events.erase(it);
  }
  }
  RpcDebugLog << "del timer event succ, origin arrvite time=" << event->m_arrive_time;
}

void Timer::resetArriveTime() {
  int64_t now = details::getNowMs();
  // 如果所有timer事务超时，那么1ms之后再执行
  int64_t interval = 100;
  {
  RWMutex::ReadLock lock(m_event_mutex);
  // 没有处理的timer事务直接离开
  if (m_pending_events.empty()) {
    return;
  }
  auto it = m_pending_events.upper_bound(now);
  if (it != m_pending_events.end()) {
    interval = it->first - now;
  }
  }

  itimerspec new_value;
  memset(&new_value, 0, sizeof(new_value));
  
  timespec ts;
  memset(&ts, 0, sizeof(ts));
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
  new_value.it_value = ts;

  // 设置下次的timerfd提醒时间
  int rt = timerfd_settime(m_fd, 0, &new_value, nullptr);

  if (rt != 0) {
    RpcErrorLog << "tiemr_settime error, interval=" << interval;
  } else {
    // RpcDebugLog << "reset timer succ, next occur time=" << (*it).first;
  }

}

// timerfd设置的回调函数
void Timer::onTimer() {

  // RpcDebugLog << "onTimer, first read data";
  // 接受timerfd的通知信息
  char buf[8];
  while(1) {
    if((g_sys_read_fun(m_fd, buf, 8) == -1) && errno == EAGAIN) {
      break;
    }
  }

  int64_t now = details::getNowMs();
  std::vector<TimerEvent::sptr> tmps;
  std::vector<std::pair<int64_t, std::function<void()>>> tasks;
  {
  // 找到未超时的event
  RWMutex::WriteLock lock(m_event_mutex);
	auto it = m_pending_events.begin();
	for (it = m_pending_events.begin(); it != m_pending_events.end(); ++it) {
    // 如果按原操作，在m_is_cancled位置停止，那么有一个timerEvent可能标记了但没从multimap删除
    // RWMutex::WriteLock lock(m_event_mutex)可能会一直重复进，而delTimerEvent()没有进去过
		// if ((*it).first <= now && !((*it).second->m_is_cancled)) {
		if ((*it).first <= now) {
      // 这样确保所有的超时event全部取出
      if (!((*it).second->m_is_cancled)) {
        tmps.push_back((*it).second);
        tasks.push_back(std::make_pair((*it).second->m_arrive_time, (*it).second->m_task));
      }
		}	else {
			break;
		}
	}

	m_pending_events.erase(m_pending_events.begin(), it);
  }

  // 将事件拿出
	for (auto i = tmps.begin(); i != tmps.end(); ++i) {
    // RpcDebugLog << "excute timer event on " << (*i)->m_arrive_time;
    // 一个timerEvent只有既允许重复，又没有被cancled才可以继续放进m_pending_events
		if ((*i)->m_is_repeated && (!(*i)->m_is_cancled)) {
			(*i)->resetTime();
			addTimerEvent(*i, false);
		}
	}

	resetArriveTime();

	// m_reactor->addTask(tasks);
  for (auto i : tasks) {
    // RpcDebugLog << "excute timeevent:" << i.first;
    i.second();
  }
}

}


