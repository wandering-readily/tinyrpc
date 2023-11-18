#include <queue>
#include <vector>
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {

TcpTimeWheel::TcpTimeWheel(Reactor* reactor, int bucket_count, int inteval /*= 10*/) 
  : m_reactor(reactor)
  , m_bucket_count(bucket_count)
  , m_inteval(inteval) {

  for (int i = 0; i < bucket_count; ++i) {
    std::vector<TcpConnectionSlot::sptr> tmp;
    m_wheel.push(tmp);
  }

  m_event = std::make_shared<TimerEvent>(m_inteval * 1000, true, std::bind(&TcpTimeWheel::loopFunc, this));
  m_reactor->getTimer()->addTimerEvent(m_event);
}


TcpTimeWheel::~TcpTimeWheel() {
  m_reactor->getTimer()->delTimerEvent(m_event);
}

void TcpTimeWheel::loopFunc() {
  // RpcDebugLog << "pop src bucket";
  // timer循环事件
  // 将front()的std::vector<TcpConnectionSlot::sptr> 退出
  // 那么AbstractSlot<TcpConnectionSlot>析构时，执行回调函数
  m_wheel.pop();
  std::vector<TcpConnectionSlot::sptr> tmp;
  m_wheel.push(tmp);
  // RpcDebugLog << "push new bucket";
}

void TcpTimeWheel::fresh(TcpConnectionSlot::sptr slot) {
  RpcDebugLog << "fresh connection";
  m_wheel.back().emplace_back(slot);
}


}