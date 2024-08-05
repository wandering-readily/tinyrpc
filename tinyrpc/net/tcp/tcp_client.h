#ifndef TINYRPC_NET_TCP_TCP_CLIENT_H
#define TINYRPC_NET_TCP_TCP_CLIENT_H

#include <memory>
#include <google/protobuf/service.h>
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/net/net_address.h"
// #include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/abstract_codec.h"

namespace tinyrpc {

class LightTimerPool;

class simpleTimeNotifier {
 public:
  virtual void registerTimer(int, std::function<void()>) = 0;
  virtual void count() = 0;
  virtual ~simpleTimeNotifier() = default;
};


class simpleTimeNotifier_LightTimerPool : public simpleTimeNotifier {
 public:
  simpleTimeNotifier_LightTimerPool();

  void registerTimer(int, std::function<void()>) override;
  void count() override;

 private:
  std::shared_ptr<LightTimerPool> lightTimerPool_;
};


class simpleTimeNotifier_retryLimit : public simpleTimeNotifier {
 public:
  simpleTimeNotifier_retryLimit(int retryLimits = 64) : retryLimits_(retryLimits) {}

  void registerTimer(int, std::function<void()>) override;
  void count() override;


 private:
  int basic_time_ = 1000;
  int retryLimits_ = 64;
  int retries_ = 0;
  std::function<void()> timer_cb;
};


//
// You should use TcpClient in a coroutine(not main coroutine)
//
class TcpClient {
 public:
  typedef std::shared_ptr<TcpClient> sptr;

  TcpClient(NetAddress::sptr addr, \
    ProtocalType type = tinyrpc::ProtocalType::TinyPb_Protocal);

  ~TcpClient();

  void resetFd();

  int sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_sptr& res);

  void stop();

  TcpConnection* getConnection();

  void setTimeout(const int v) {
    m_max_timeout = v;
  }

  void setTryCounts(const int v) {
    m_try_counts = v;
  }

  const std::string& getErrInfo() {
    return m_err_info;
  }

  NetAddress::sptr getPeerAddr() const {return m_peer_addr;}

  NetAddress::sptr getLocalAddr() const {return m_local_addr;}

  AbstractCodeC::sptr getCodeC() {
    return m_codec;
  }


 private:

  bool isServerConn_ {false};
  Reactor* m_reactor {nullptr};

  int m_family {0};
  int m_fd {-1};
  int m_try_counts {3};         // max try reconnect times
  int m_max_timeout {10000};       // max connect timeout, ms
  bool m_is_stop {false};
  std::string m_err_info;      // error info of client

  NetAddress::sptr m_local_addr {nullptr};
  NetAddress::sptr m_peer_addr {nullptr};
  // Reactor* m_reactor {nullptr};
  TcpConnection::sptr m_connection {nullptr};

  AbstractCodeC::sptr m_codec {nullptr};

  bool m_connect_succ {false};
  
  FdEventContainer::sptr fdEventPool_;

  std::shared_ptr<LightTimerPool> lightTimerPool_;

}; 

}



#endif