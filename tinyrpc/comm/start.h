#ifndef TINYRPC_COMM_START_H
#define TINYRPC_COMM_START_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/net_address.h"

namespace tinyrpc {

class CoroutinePool;

class TinyrpcRunner final {

public:
  TinyrpcRunner(const char *);
  TinyrpcRunner(const std::string &);

  ~TinyrpcRunner()=default;

  TinyrpcRunner(const TinyrpcRunner &)=delete;
  TinyrpcRunner(TinyrpcRunner &&)=delete;
  TinyrpcRunner &operator=(const TinyrpcRunner &)=delete;
  TinyrpcRunner &operator=(TinyrpcRunner &&)=delete;


  template<typename T, 
    typename=std::enable_if_t<std::is_base_of_v<HttpServlet, T>>>
  // requires (std::is_base_of_v<HttpServlet, T>)
  void RegisterHttpServlet(const std::string &url_path) {
    if constexpr (std::is_base_of_v<AsyncHttpServlet, T>) {
      if(!gRpcServer_->registerHttpServlet(url_path, 
          std::make_shared<T>(std::weak_ptr<CoroutinePool> (corPool_)))) {
        printf("Start TinyRPC server error, because register http servelt error, \
          please look up rpc log get more details!\n"); \
        tinyrpc::Exit(0);
      }
    } else {
      if(!gRpcServer_->registerHttpServlet(url_path, std::make_shared<T>())) {
        printf("Start TinyRPC server error, because register http servelt error, \
          please look up rpc log get more details!\n"); \
        tinyrpc::Exit(0);
      }
    }
  }


  template<typename T, 
    typename=std::enable_if_t<std::is_base_of_v<google::protobuf::Service, T>>>
  // requires (std::is_base_of_v<google::protobuf::Service, T>)
  void RegisterService() {
    if(!gRpcServer_->registerService(std::make_shared<T>())) {
      printf("Start TinyRPC server error, because register http servelt error, \
        please look up rpc log get more details!\n"); \
      tinyrpc::Exit(0);
    }
  }

  TcpServer::ptr GetServer();
  Config::ptr GetConfig();

  void StartRpcServer();

  void AddTimerEvent(TimerEvent::ptr);

private:
  void InitServiceConfig();
  void InitConfig();
  void InitLogger(std::shared_ptr<Logger> &);
  void InitServer();
  void addReactorPerThread();


private:
  // 构造顺序按照声明顺序
  // 析构顺序按照反声明顺序
  std::string configName_;

  Config::ptr gRpcConfig_;

  std::shared_ptr<CoroutinePool> corPool_;
  std::shared_ptr<FdEventContainer> fdEventPool_;
  std::shared_ptr<CoroutineTaskQueue> couroutine_task_queue_;

  TcpServer::ptr gRpcServer_;
};

}; // namespace tinyrpc

#endif