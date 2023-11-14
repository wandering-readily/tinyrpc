#ifndef TINYRPC_COMM_CLIENT_H
#define TINYRPC_COMM_CLIENT_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include <any>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"


namespace details {
  template <typename T, typename = void>
  struct has_Stub : std::false_type {
    using type = T;
  };
  template <typename T>
  struct has_Stub<T, std::void_t<typename T::Stub>> : std::true_type {
    using type = typename T::Stub;
  };

  template <typename T>
  using has_Stub_t = typename has_Stub<T>::type;

  template <typename T>
  using has_Stub_v = typename has_Stub<T>::value;

};

namespace tinyrpc {

struct UnixDomainAddressFlag {};

template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T> || std::is_same_v<UnixDomainAddress, T>>>
    // requires (std::is_same_v<IPAddress, T> || std::is_same_v<UnixDomainAddress, T>)
class TinyrpcClient {

public:

  // requires (std::is_same_v<IPAddress, T>)
  TinyrpcClient(std::string ip, uint16_t port) \
    : addr_(std::make_shared<T> (ip, port)) {}

  TinyrpcClient(std::string addr) \
    : addr_(std::make_shared<T> (addr)) {}

  TinyrpcClient(uint16_t port) \
    : addr_(std::make_shared<T> (port)) {}

  TinyrpcClient(sockaddr_in addr) \
    : addr_(std::make_shared<T> (addr)) {}

  TinyrpcClient(std::string path, UnixDomainAddressFlag dummy) \
    : addr_(std::make_shared<T> (path)) {}

	TinyrpcClient(sockaddr_un addr, UnixDomainAddressFlag dummy) \
    : addr_(std::make_shared<T> (addr)) {}


  void setTimeOut(int timeout) {
    timeout_ =  timeout;
  }

public:
  
  template <typename S,
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, S>>>
  int Call(const std::string &method_name, \
      google::protobuf::Message *request, \
      google::protobuf::Message *response) {

    tinyrpc::TinyPbRpcChannel channel(addr_);
    auto stub = std::make_unique<details::has_Stub_t<S>> (&channel);

    TinyPbRpcController rpc_controller;
    rpc_controller.SetTimeout(timeout_);

    const google::protobuf::MethodDescriptor* method = 
      S::descriptor()->FindMethodByName(method_name);
    
    std::function<void()> reply_package_func = [](){};
    TinyPbRpcClosure closure(reply_package_func);

    stub->CallMethod(method, &rpc_controller, request, response, &closure);
    return rpc_controller.ErrorCode();
  }

 protected:
  tinyrpc::NetAddress::ptr addr_;
  int timeout_ = 5000;
};

}; // namespace tinyrpc


#endif