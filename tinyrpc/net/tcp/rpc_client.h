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

class LightTimerPool;
class LightTimer;

//
// You should use TcpClient in a coroutine(not main coroutine)
//
class RpcClient {
 public:
  typedef std::shared_ptr<RpcClient> sptr;
  typedef std::weak_ptr<RpcClient> wptr;

  RpcClient(ProtocalType type = tinyrpc::ProtocalType::TinyPb_Protocal);

  ~RpcClient();

  bool connect(NetAddress::sptr);

  int resetFd(int);

  TcpConnection::sptr getConnection(NetAddress::sptr);

  int sendAndRecvTinyPb(NetAddress::sptr, \
    const std::string&, TinyPbStruct::pb_sptr&);

  void setTimeout(const int v) {
    m_max_timeout = v;
  }

  void setTryCounts(const int v) {
    m_try_counts = v;
  }

  const std::string& getErrInfo() {
    return m_err_info;
  }

  NetAddress::sptr getLocalAddr() const {
    return local_addr_;
  }

  bool delConnection(NetAddress::sptr);

  AbstractCodeC::sptr getCodec() {return codec_;}

 private:

  int m_try_counts {3};         // max try reconnect times
  int m_max_timeout {10000};       // max connect timeout, ms
  std::string m_err_info;      // error info of client

  NetAddress::sptr local_addr_ {nullptr};

  // peer_addr --> connection
  // 增减connection不符合多线程安全
  // rpcClient 不符合多线程安全
  std::map<std::string, TcpConnection::sptr> connections_;

  AbstractCodeC::sptr codec_ {nullptr};

  FdEventContainer::sptr fdEventPool_;

  std::shared_ptr<LightTimerPool> lightTimerPool_;

}; 

}



#endif