#ifndef TINYRPC_NET_TCP_RPC_CLIENT_H
#define TINYRPC_NET_TCP_RPC_CLIENT_H

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
class RpcClientGroups;

//
// You should use TcpClient in a coroutine(not main coroutine)
//
class RpcClient {
 public:
  typedef std::shared_ptr<RpcClient> sptr;
  typedef std::weak_ptr<RpcClient> wptr;

  RpcClient(int, const std::string &, NetAddress::sptr, \
      FdEventContainer::wptr, \
      std::weak_ptr<LightTimerPool>, \
      std::shared_ptr<RpcClientGroups>);

  ~RpcClient();

  void resetFd();

  int sendAndRecvTinyPb(const std::string&, TinyPbStruct::pb_sptr&);

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
    return conn_->getLocalAddr();
  }

  NetAddress::sptr getPeerAddr() const {
    return conn_->getPeerAddr();
  }

  AbstractCodeC::sptr getCodec() {return conn_->getCodec();}

  TcpConnection::sptr getConn() {return conn_;}

 private:

  int m_try_counts {3};         // max try reconnect times
  int m_max_timeout {10000};       // max connect timeout, ms
  std::string m_err_info;      // error info of client
  int m_family = -1;

  // peer_addr --> connection
  // 增减connection不符合多线程安全
  // rpcClient 不符合多线程安全
  // 推荐的用法是，map的key(std::string)是一个独一无二的标识
  // 单线程只用这个conn
  std::string conn_flag;
  TcpConnection::sptr conn_;
  FdEventContainer::wptr weakFdEventPool_;
  std::weak_ptr<LightTimerPool> weakLightTimerPool_;
  std::weak_ptr<RpcClientGroups> weakRpcClientGroups_;

}; 


class RpcClientGroups {
 public:
  typedef std::shared_ptr<RpcClient> sptr;
  typedef std::weak_ptr<RpcClient> wptr;

  RpcClientGroups(ProtocalType type = tinyrpc::ProtocalType::TinyPb_Protocal);

  ~RpcClientGroups();

  std::shared_ptr<TcpConnection> getConnection(const std::string &, NetAddress::sptr);
 
  bool delConnection(const std::string &);

 private:
 
  std::shared_ptr<tinyrpc::NetAddress> local_addr_ = \
    std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);
 
  bool isServerConn_ {false};
  
  Reactor* reactor_ {nullptr};
  
  AbstractCodeC::sptr codec_;

  RWMutex conns_mutex_;
  std::map<std::string, TcpConnection::sptr> conns_;

  FdEventContainer::sptr fdEventPool_;

  std::shared_ptr<LightTimerPool> lightTimerPool_;

}; 

}



#endif