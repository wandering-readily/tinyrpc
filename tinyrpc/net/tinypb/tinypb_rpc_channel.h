#ifndef TINYRPC_NET_TINYPB_TINYPB_RPC_CHANNEL_H
#define TINYRPC_NET_TINYPB_TINYPB_RPC_CHANNEL_H 

#include <memory>
#include <google/protobuf/service.h>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/rpc_client.h"

namespace tinyrpc {

// client rpc调用该CallMethod()代理调用
// 需要完成的任务:
//    1. 调用RPC方法将Request结构体内容解码加入发送缓存
//    2. 设置msg_req_number设置当前client的消息代号
//    3. 将发送缓存内容发送
//    4. 接收rpc返回消息(陷入阻塞)
//    5. 将返回消息编码回Response
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
  TinyPbRpcClientChannel(std::shared_ptr<RpcClient>);
  ~TinyPbRpcClientChannel() = default;

void CallMethod(const google::protobuf::MethodDescriptor* method, 
    google::protobuf::RpcController* controller, 
    const google::protobuf::Message* request, 
    google::protobuf::Message* response, 
    google::protobuf::Closure* done);
 
 private:
  std::shared_ptr<RpcClient> rpc_client_;

};

}



#endif