#ifndef TINYRPC_NET_TCP_RPC_CLIENT_H
#define TINYRPC_NET_TCP_RPC_CLIENT_H

#include <memory>
#include <list>
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

  RpcClient(NetAddress::sptr, \
      std::shared_ptr<RpcClientGroups>);

  ~RpcClient();

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

  NetAddress::sptr getLocalAddr() const {return conn_->getLocalAddr();}

  NetAddress::sptr getPeerAddr() const {return conn_->getPeerAddr();}

  AbstractCodeC::sptr getCodec() {return conn_->getCodec();}

  TcpBuffer *getOutBuffer() {return conn_->getOutBuffer();}

 
 private:

  void resetFd();
  
  bool updateTcpState();

  TcpConnection::sptr getConn() {return conn_;}

 private:

  int m_try_counts {3};         // max try reconnect times
  int m_max_timeout {10000};       // max connect timeout, ms
  std::string m_err_info;      // error info of client

  // peer_addr --> connection
  // 增减connection不符合多线程安全
  // rpcClient 不符合多线程安全
  // 推荐的用法是，map的key(std::string)是一个独一无二的标识
  // 单线程只用这个conn
  TcpConnection::sptr conn_;
  FdEventContainer::wptr weakFdEventPool_;
  std::weak_ptr<LightTimerPool> weakLightTimerPool_;
  std::weak_ptr<RpcClientGroups> weakRpcClientGroups_;

}; 


class RpcClientGroups {
 public:
  typedef std::shared_ptr<RpcClientGroups> sptr;
  typedef std::weak_ptr<RpcClientGroups> wptr;

  friend class RpcClient;

  RpcClientGroups(int maxFreeConns = 2, ProtocalType type = tinyrpc::ProtocalType::TinyPb_Protocal);

  ~RpcClientGroups();


 private:

  std::shared_ptr<TcpConnection> getConnection(NetAddress::sptr);
 
  void delConnection(NetAddress::sptr);

  FdEventContainer::sptr getFdEventPool() {return fdEventPool_;}

  std::shared_ptr<LightTimerPool> getLightTimerPool() {return lightTimerPool_;}
  

 private:
  
  int maxFreeConns_ = 2;
 
  std::shared_ptr<tinyrpc::NetAddress> local_addr_;
 
  bool isServerConn_ {false};
  
  Reactor* reactor_ {nullptr};
  
  AbstractCodeC::sptr codec_;

  Mutex freeConns_mutex_;
  std::map<std::string, std::list<TcpConnection::sptr>> freeConns_;
  std::map<std::string, Mutex> freeConnMutexs_;


  Mutex workConns_mutex_;
  std::map<std::string, TcpConnection::sptr> workConns_;

  FdEventContainer::sptr fdEventPool_;

  std::shared_ptr<LightTimerPool> lightTimerPool_;

}; 

}



#endif