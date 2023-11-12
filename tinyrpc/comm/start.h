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


class TinyrpcRunner {

public:
  TinyrpcRunner(const char *configName) : configName_(configName) {}
  TinyrpcRunner(const std::string &configName) : configName_(configName) {}
  TinyrpcRunner(std::string &&configName) : configName_(std::move(configName)) {}

  ~TinyrpcRunner()=default;

  TinyrpcRunner(const TinyrpcRunner &)=delete;
  TinyrpcRunner(TinyrpcRunner &&)=delete;
  TinyrpcRunner &operator=(const TinyrpcRunner &)=delete;
  TinyrpcRunner &operator=(TinyrpcRunner &&)=delete;


  void RegisterHttpServlet(const std::string &, HttpServlet::ptr);
  void RegisterService(std::shared_ptr<google::protobuf::Service>);


  TcpServer::ptr GetServer();
  Config::ptr GetConfig();
  int GetIOThreadPoolSize();

  void InitServiceConfig();
  void StartRpcServer();

  void AddTimerEvent(TimerEvent::ptr);

private:
  void InitConfig();
  void InitLogger(std::shared_ptr<Logger> &);
  void InitServer();

private:
  int g_init_config = 0;
  std::string configName_;

  Config::ptr gRpcConfig_;
  TcpServer::ptr gRpcServer_;
};

}; // namespace tinyrpc

#endif