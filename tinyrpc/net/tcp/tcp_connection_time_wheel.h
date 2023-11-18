#ifndef TINYRPC_NET_TCP_TCPCONNECTIONTIMEWHEEL_H
#define TINYRPC_NET_TCP_TCPCONNECTIONTIMEWHEEL_H

#include <queue>
#include <vector>
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {

class TcpConnection;

class TcpTimeWheel {

 public:
  typedef std::shared_ptr<TcpTimeWheel> sptr;

  typedef AbstractSlot<TcpConnection> TcpConnectionSlot;

  TcpTimeWheel(Reactor* reactor, int bucket_count, int invetal = 10);

  ~TcpTimeWheel();

  void fresh(TcpConnectionSlot::sptr slot);

  void loopFunc();


 private:
  Reactor* m_reactor {nullptr};
  int m_bucket_count {0};
  int m_inteval {0};    // second

  TimerEvent::sptr m_event;
  std::queue<std::vector<TcpConnectionSlot::sptr>> m_wheel;
};


}





#endif