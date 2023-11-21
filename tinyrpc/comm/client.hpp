#ifndef TINYRPC_COMM_CLIENT_H
#define TINYRPC_COMM_CLIENT_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include <any>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"

#include "tinyrpc/net/tcp/rpc_client.h"
#include "tinyrpc/comm/error_code.h"

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


// 不是多线程安全的类
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

    TinyPbRpcChannel channel(addr_);
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
  NetAddress::sptr addr_;
  int timeout_ = 5000;
};



/*
 * 同步client的 TCP连接复用
 * TCP connection的复用
*/

class LongLiveSupClient;
// 多线程安全的类
class TinyrpcLongLiveClient : public std::enable_shared_from_this<TinyrpcLongLiveClient> {
public:
  typedef std::shared_ptr<TinyrpcLongLiveClient> sptr;
  typedef std::weak_ptr<TinyrpcLongLiveClient> wptr;

  friend class LongLiveSupClient;

  TinyrpcLongLiveClient(int maxFreeConns = 2, \
      ProtocalType type = ProtocalType::TinyPb_Protocal, \
      int timeout=5000) : timeout_(timeout) {

    clientGroups_ = std::make_shared<RpcClientGroups> (maxFreeConns, type);
  }

  ~TinyrpcLongLiveClient() = default;

  // 这里得到一个NetAddress::sptr，最好一直使用，减少构造
  // 如果使用新的也没问题，因为 Call()函数RpcClient 优先选取freeConns
  // 那么这个NetAddress::sptr不会参与构造TcpConnction，一会被释放
  template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T>>>
  // requires (std::is_same_v<IPAddress, T>)
  NetAddress::sptr addRpcClientAddr(std::string ip, uint16_t port) {
    return (std::make_shared<T> (ip, port));
  }

  template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T>>>
  NetAddress::sptr addRpcClientAddr(std::string addr) {
    return (std::make_shared<T> (addr));
  }

  template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T>>>
  NetAddress::sptr addRpcClientAddr(uint16_t port) {
    return (std::make_shared<T> (port));
  }

  template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T>>>
  NetAddress::sptr addRpcClientAddr(sockaddr_in addr) {
    return (std::make_shared<T> (addr));
  }

  template <typename T, 
    typename=std::enable_if_t<std::is_same_v<UnixDomainAddress, T>>>
  NetAddress::sptr addRpcClientAddr(std::string path, UnixDomainAddressFlag dummy) {
    return (std::make_shared<T> (path));
  }

  template <typename T, 
    typename=std::enable_if_t<std::is_same_v<UnixDomainAddress, T>>>
	NetAddress::sptr addRpcClientAddr(sockaddr_un addr, UnixDomainAddressFlag dummy) {
    return (std::make_shared<T> (addr));
  }


public:
  std::unique_ptr<LongLiveSupClient> newLongLiveSupClient(NetAddress::sptr addr) {
    return std::make_unique<LongLiveSupClient> (shared_from_this(), addr);
  }


private:
  
  // 一个不立即释放的rpcClient
  // getByID更加节省开销
  RpcClient::wptr getRpcClient(NetAddress::sptr addr) {
    auto client = std::make_shared<RpcClient> (addr, clientGroups_);
    {
      Mutex::Lock lock(clientsMutex_);
      // 这里桶std::set不认为是线程安全函数
      clients_.insert(client);
    }
    return client;
  }

  void returnRpcClient(RpcClient::wptr client) {
    RpcClient::sptr sClient = client.lock();
    if (sClient != nullptr) [[likely]] {
      // 这里桶std::set不认为是线程安全函数
      Mutex::Lock lock(clientsMutex_);
      clients_.erase(sClient);
    }
  }

public:
  
  template <typename S,
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, S>>>
  int CallByAddr(const std::string &method_name, \
      google::protobuf::Message *request, \
      google::protobuf::Message *response, \
      NetAddress::sptr addr) {

    return CallByRpcClient<S> (method_name, request, response, \
        std::make_shared<RpcClient> (addr, clientGroups_));
  }

 private:

  template <typename S,
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, S>>>
  int CallByRpcClient(const std::string &method_name, \
      google::protobuf::Message *request, \
      google::protobuf::Message *response, \
      RpcClient::sptr client) {

    TinyPbRpcClientChannel channel(client);
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

  int timeout_ = 5000;

  Mutex clientsMutex_;
  std::set<RpcClient::sptr> clients_;

  RpcClientGroups::sptr clientGroups_;
};

TinyrpcLongLiveClient::sptr newTinyrpcLongLiveClient(int maxFreeConns = 2, \
      ProtocalType type = ProtocalType::TinyPb_Protocal, \
      int timeout=5000) {
  return std::make_shared<TinyrpcLongLiveClient> (maxFreeConns, type, timeout);
}


// 不是多线程安全的类
class LongLiveSupClient {
public:
  typedef std::shared_ptr<LongLiveSupClient> sptr;
  typedef std::weak_ptr<LongLiveSupClient> wptr;

public:
  LongLiveSupClient(std::shared_ptr<TinyrpcLongLiveClient> longLiveClient, \
      NetAddress::sptr addr) \
      : clientGroups_(longLiveClient) {

    client_ = longLiveClient->getRpcClient(addr);
  }

  ~LongLiveSupClient() {
    std::shared_ptr<TinyrpcLongLiveClient> clientGroups = clientGroups_.lock();
    if (clientGroups != nullptr) {
      clientGroups->returnRpcClient(client_);
    }
  }

  LongLiveSupClient(const LongLiveSupClient&) = delete;
  LongLiveSupClient(LongLiveSupClient&&) = delete;
  LongLiveSupClient& operator=(const LongLiveSupClient&) = delete;
  LongLiveSupClient& operator=(LongLiveSupClient&&) = delete;


  template <typename S,
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, S>>>
  int Call(const std::string &method_name, \
      google::protobuf::Message *request, \
      google::protobuf::Message *response) {
    
    auto clientGroups = clientGroups_.lock();
    auto client = client_.lock();
    assert(clientGroups != nullptr && client != nullptr);
    return clientGroups->CallByRpcClient<S> (method_name, \
      request, response, client);
  }

private:

  std::weak_ptr<TinyrpcLongLiveClient> clientGroups_;
  RpcClient::wptr client_;

};



}; // namespace tinyrpc


#endif