#ifndef TINYRPC_COMM_START_H
#define TINYRPC_COMM_START_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include <any>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/coroutine/coroutine_pool.h"

namespace tinyrpc {

class CoroutinePool;

class TinyrpcServer final {

public:
  TinyrpcServer(const char *);
  TinyrpcServer(const std::string &);

  ~TinyrpcServer()=default;

  TinyrpcServer(const TinyrpcServer &)=delete;
  TinyrpcServer(TinyrpcServer &&)=delete;
  TinyrpcServer &operator=(const TinyrpcServer &)=delete;
  TinyrpcServer &operator=(TinyrpcServer &&)=delete;


  template <typename T, 
    typename=std::enable_if_t<std::is_base_of_v<HttpServlet, T>>>
  // requires (std::is_base_of_v<HttpServlet, T>)
  void RegisterHttpServlet(const std::string &url_path) {
    if constexpr (std::is_base_of_v<AsyncHttpServlet, T>) {
      if(!gRpcServer_->registerHttpServlet(url_path, 
          std::make_shared<T>(CoroutinePool::wptr (corPool_), \
          std::weak_ptr<IOThreadPool> (gRpcServer_->getSharedIOThreadPool())))) {
        printf("Start TinyRPC server error, because register http servelt error, \
          please look up rpc log get more details!\n"); \
        locateErrorExit
      }
    } else {
      if(!gRpcServer_->registerHttpServlet(url_path, std::make_shared<T>())) {
        printf("Start TinyRPC server error, because register http servelt error, \
          please look up rpc log get more details!\n"); \
        locateErrorExit
      }
    }
  }


  template <typename T, 
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, T>>>
  // requires (std::is_base_of_v<google::protobuf::Service, T>)
  void RegisterService() {
    if(!gRpcServer_->registerService(std::make_shared<T>())) {
      printf("Start TinyRPC server error, because register http servelt error, \
        please look up rpc log get more details!\n"); \
      locateErrorExit
    }
  }

  TcpServer::sptr GetServer();
  Config::sptr  GetConfig();

  void StartRpcServer();

private:
  void InitServiceConfig();
  void InitConfig();
  // void InitLogger(std::shared_ptr<Logger> &);
  void InitLogger(Logger::sptr &);
  void InitServer();


private:
  // 构造顺序按照声明顺序
  // 析构顺序按照反声明顺序
  std::string configName_;

  Config::sptr  gRpcConfig_;

  CoroutinePool::sptr corPool_;
  FdEventContainer::sptr fdEventPool_;
  CoroutineTaskQueue::sptr coroutine_task_queue_;

  TcpServer::sptr gRpcServer_;
};

}; // namespace tinyrpc

#endif