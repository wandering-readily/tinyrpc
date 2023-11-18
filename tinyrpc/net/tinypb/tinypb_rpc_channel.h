#ifndef TINYRPC_NET_TINYPB_TINYPB_RPC_CHANNEL_H
#define TINYRPC_NET_TINYPB_TINYPB_RPC_CHANNEL_H 

#include <memory>
#include <google/protobuf/service.h>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/rpc_client.h"

namespace tinyrpc {

class TinyPbRpcChannel : public google::protobuf::RpcChannel {

 public:
  typedef std::shared_ptr<TinyPbRpcChannel> sptr;
  TinyPbRpcChannel(NetAddress::sptr addr);
  ~TinyPbRpcChannel() = default;

void CallMethod(const google::protobuf::MethodDescriptor* method, 
    google::protobuf::RpcController* controller, 
    const google::protobuf::Message* request, 
    google::protobuf::Message* response, 
    google::protobuf::Closure* done);
 
 private:
  NetAddress::sptr m_addr;
  // TcpClient::sptr m_client;

};

class TinyPbRpcClientChannel : public google::protobuf::RpcChannel {

 public:
  typedef std::shared_ptr<TinyPbRpcClientChannel> sptr;
  TinyPbRpcClientChannel(NetAddress::sptr, RpcClient::wptr);
  ~TinyPbRpcClientChannel() = default;

void CallMethod(const google::protobuf::MethodDescriptor* method, 
    google::protobuf::RpcController* controller, 
    const google::protobuf::Message* request, 
    google::protobuf::Message* response, 
    google::protobuf::Closure* done);
 
 private:
  NetAddress::sptr addr_;
  RpcClient::wptr weakRpcClient_;

};

}



#endif