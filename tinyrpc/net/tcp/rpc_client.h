#ifndef TINYRPC_NET_RPC_TCP_CLIENT_H
#define TINYRPC_NET_RPC_TCP_CLIENT_H

#include <memory>
#include <google/protobuf/service.h>
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/abstract_codec.h"

namespace tinyrpc {


//
// You should use TcpClient in a coroutine(not main coroutine)
//
class RpcClient {
 public:
  typedef std::shared_ptr<TcpClient> ptr;

  RpcClient(ProtocalType type = tinyrpc::ProtocalType::TinyPb_Protocal);

  ~RpcClient();

  bool connect(NetAddress::ptr);

  int resetFd(int);

  int sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_ptr& res);

  TcpConnection* getConnection(NetAddress::ptr);
  TcpConnection* getConnection(const std::string &);

  int sendAndRecvTinyPb(NetAddress::ptr, \
    const std::string&, TinyPbStruct::pb_ptr&);

  void setTimeout(const int v) {
    m_max_timeout = v;
  }

  void setTryCounts(const int v) {
    m_try_counts = v;
  }

  const std::string& getErrInfo() {
    return m_err_info;
  }

  NetAddress::ptr getLocalAddr() const {
    return local_addr_;
  }

  AbstractCodeC::ptr getCodeC() {
    return codec_;
  }


 private:

  int m_try_counts {3};         // max try reconnect times
  int m_max_timeout {10000};       // max connect timeout, ms
  bool m_is_stop {false};
  std::string m_err_info;      // error info of client

  NetAddress::ptr local_addr_ {nullptr};
  Reactor* reactor_ {nullptr};

  // peer_addr --> connection
  std::map<std::string, TcpConnection::ptr> connections_;

  AbstractCodeC::ptr codec_ {nullptr};

  std::shared_ptr<FdEventContainer> fdEventPool_;

}; 

}



#endif