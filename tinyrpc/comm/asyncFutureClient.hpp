#ifndef TINYRPC_COMM_ASYNCFUTURECLIENT_H
#define TINYRPC_COMM_ASYNCFUTURECLIENT_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include <thread>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_async_channel.h"


namespace tinyrpc {


// 适用于小范围使用Async, 用完线程直接归还
template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T> || std::is_same_v<UnixDomainAddress, T>>>
class AsyncFuture_TinyrpcClient final : public TinyrpcClient<T> {

public:

  AsyncFuture_TinyrpcClient(std::string ip, uint16_t port) \
      : TinyrpcClient<T>(ip, port) {}

  AsyncFuture_TinyrpcClient(std::string addr) \
      : TinyrpcClient<T>(addr) {}

  AsyncFuture_TinyrpcClient(uint16_t port) \
      : TinyrpcClient<T>(port) {}

  AsyncFuture_TinyrpcClient(sockaddr_in addr) \
      : TinyrpcClient<T>(addr) {}

  AsyncFuture_TinyrpcClient(std::string path, UnixDomainAddressFlag dummy) \
      : TinyrpcClient<T>(path) {}

	AsyncFuture_TinyrpcClient(sockaddr_un addr, UnixDomainAddressFlag dummy) \
      : TinyrpcClient<T>(addr) {}


public:
  
  template <typename S,
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, S>>>
  std::future<void> AsyncFuture_Call(const std::string &method_name, \
      std::shared_ptr<google::protobuf::Message> request, \
      std::shared_ptr<google::protobuf::Message> response) {

    std::packaged_task<void(void)> task{
      [=] () {
        this->template Call<S>(method_name, request.get(), response.get());
      }
    };

    auto future = task.get_future();
    std::thread(std::move(task)).detach();
    return future;
  }

};


}; // namespace tinyrpc


#endif