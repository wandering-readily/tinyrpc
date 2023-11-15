#ifndef TINYRPC_COMM_ASYNCCORCLIENT_H
#define TINYRPC_COMM_ASYNCCORCLIENT_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_async_channel.h"
#include "tinyrpc/coroutine/coroutine_pool.h"


namespace tinyrpc {

// AsyncCor_TinyrpcClientWaiter 会持有AsyncChannel shared_ptr指针
// AsyncChannel 会持有rpc_req prc_res shared_ptr指针
// 最佳使用方法是 将AsyncCor_TinyrpcClientWaiter 放入局部作用域{}
class AsyncCor_TinyrpcClientWaiter : public AsyncCor_Waiter {

public:
  typedef std::shared_ptr<tinyrpc::TinyPbRpcAsyncChannel> ptr;

public:

  AsyncCor_TinyrpcClientWaiter(AsyncCor_TinyrpcClientWaiter::ptr channel) \
    : channel_(channel){}

  ~AsyncCor_TinyrpcClientWaiter() = default;

  void wait() {
    if (waited) {
      return;
    }
    waited = true;
    channel_->wait();
  }

private:
  // 只适合单线程wait
  bool waited = false;
  std::shared_ptr<tinyrpc::TinyPbRpcAsyncChannel> channel_;
  
};

// 适用于大范围使用Async, 需要线程池和协程池
template <typename T, 
    typename=std::enable_if_t<std::is_same_v<IPAddress, T> || std::is_same_v<UnixDomainAddress, T>>>
class AsyncCor_TinyrpcClient final : public TinyrpcClient<T> {

public:

  AsyncCor_TinyrpcClient(std::string ip, uint16_t port, \
      int pool_size, int stack_size) \
      : TinyrpcClient<T>(ip, port) {
    ResourcePoolSet(pool_size, stack_size);
  }

  AsyncCor_TinyrpcClient(std::string addr, \
      int pool_size, int stack_size) \
      : TinyrpcClient<T>(addr) {
    ResourcePoolSet(pool_size, stack_size);
  }

  AsyncCor_TinyrpcClient(uint16_t port, \
      int pool_size, int stack_size) \
      : TinyrpcClient<T>(port) {
    ResourcePoolSet(pool_size, stack_size);
  }

  AsyncCor_TinyrpcClient(sockaddr_in addr, \
      int pool_size, int stack_size) \
      : TinyrpcClient<T>(addr) {
    ResourcePoolSet(pool_size, stack_size);
  }

  AsyncCor_TinyrpcClient(std::string path, UnixDomainAddressFlag dummy, \
      int pool_size, int stack_size) \
      : TinyrpcClient<T>(path) {
    ResourcePoolSet(pool_size, stack_size);
  }

	AsyncCor_TinyrpcClient(sockaddr_un addr, UnixDomainAddressFlag dummy, \
      int pool_size, int stack_size) \
      : TinyrpcClient<T>(addr) {
    ResourcePoolSet(pool_size, stack_size);
  }

private:
  void ResourcePoolSet(int pool_size, int stack_size) {
    if (cor_pool_size_ < pool_size) {
      cor_pool_size_ = pool_size;
    }
    if (cor_stack_size_ < stack_size * 1024) {
      cor_stack_size_ = stack_size * 1024;
    }

    CorPool_ = std::make_shared<CoroutinePool>(cor_pool_size_, cor_stack_size_);

    coroutine_task_queue_ = std::make_shared<CoroutineTaskQueue>();

    IOThreadPool_ = std::make_shared<IOThreadPool>(1, CorPool_);
	  IOThreadPool_->beginThreadPool(coroutine_task_queue_);
    IOThreadPool_->start();
  }

public:
  
  template <typename S,
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, S>>>
  std::unique_ptr<AsyncCor_TinyrpcClientWaiter> Async_Call(const std::string &method_name, \
      std::shared_ptr<google::protobuf::Message> request, \
      std::shared_ptr<google::protobuf::Message> response) {

    auto rpc_controller = std::make_shared<TinyPbRpcController>();
    rpc_controller->SetTimeout(this->timeout_);
    
    std::function<void()> reply_package_func = [](){};
    auto closure = std::make_shared<TinyPbRpcClosure>(reply_package_func);

    auto async_channel = std::make_shared<tinyrpc::TinyPbRpcAsyncChannel>(this->addr_);
    async_channel->saveCallee( 
      rpc_controller, request, response, closure, 
      CorPool_, IOThreadPool_->getRandomThread(true).get());
    auto stub = std::make_unique<details::has_Stub_t<S>> (async_channel.get());

    const google::protobuf::MethodDescriptor* method = 
      S::descriptor()->FindMethodByName(method_name);
    stub->CallMethod(method, rpc_controller.get(), request.get(), response.get(), nullptr);

    // 返回的async_channel会持有request, response 智能指针
    return std::make_unique<AsyncCor_TinyrpcClientWaiter> (async_channel);
  }

private:
  int cor_pool_size_ = 100;
  int cor_stack_size_ = 256 * 1024;
  std::shared_ptr<CoroutinePool> CorPool_;
  std::shared_ptr<CoroutineTaskQueue> coroutine_task_queue_;
  std::shared_ptr<tinyrpc::IOThreadPool> IOThreadPool_;
};

}; // namespace tinyrpc


#endif