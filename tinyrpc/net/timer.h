#ifndef TINYRPC_NET_TIMER_H
#define TINYRPC_NET_TIMER_H

#include <time.h>
#include <memory>
#include <map>
#include <functional>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/comm/log.h"


namespace details {

int64_t getNowMs();

};


namespace tinyrpc {


class TimerEvent {

 public:

  typedef std::shared_ptr<TimerEvent> sptr;
  TimerEvent(int64_t interval, bool is_repeated, std::function<void()>task)
    : m_interval(interval), m_is_repeated(is_repeated), m_task(task) {
    m_arrive_time = details::getNowMs() + m_interval;  	
    RpcDebugLog << "timeevent will occur at " << m_arrive_time;
  }

  // 设置下次到达时间
  void resetTime() {
    RpcDebugLog << "reset tiemrevent, origin arrivetime=" << m_arrive_time;
    m_arrive_time = details::getNowMs() + m_interval;  	
    RpcDebugLog << "reset tiemrevent, now arrivetime=" << m_arrive_time;
    m_is_cancled = false;
  }

  void wake() {
    m_is_cancled = false;
  }

  void cancle () {
    m_is_cancled = true;
  }

  void cancleRepeated () {
    m_is_repeated = false;
  }

 public:
  int64_t m_arrive_time;   // when to excute task, ms
  int64_t m_interval;     // interval between two tasks, ms
  bool m_is_repeated {false};
	bool m_is_cancled {false};
  std::function<void()> m_task;

};

class FdEvent;

// Timer继承自FdEvent
class Timer : public tinyrpc::FdEvent {

 public:

  typedef std::shared_ptr<Timer> sptr;
  
  Timer(Reactor* reactor);

	~Timer();

	void addTimerEvent(TimerEvent::sptr event, bool need_reset = true);

	void delTimerEvent(TimerEvent::sptr event);

	void resetArriveTime();

  void onTimer();

 private:

 	std::multimap<int64_t, TimerEvent::sptr> m_pending_events;
  RWMutex m_event_mutex;


};



}

#endif
