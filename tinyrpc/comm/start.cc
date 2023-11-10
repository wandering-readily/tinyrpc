#include <google/protobuf/service.h>
#include "tinyrpc/comm/start.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

// 额外增加的#define
// 只是为了看到MYSQL的操作
// #define DECLARE_MYSQL_PULGIN 1

namespace tinyrpc {

tinyrpc::Logger::ptr gRpcLogger;

void TinyrpcRunner::RegisterHttpServlet(const std::string &url_path, HttpServlet::ptr servlet) {
  do { 
  if(!gRpcServer_->registerHttpServlet(url_path, servlet)) {
    printf("Start TinyRPC server error, because register http servelt error, \ 
      please look up rpc log get more details!\n"); \
    tinyrpc::Exit(0);
  }
 } while(0);
}

void TinyrpcRunner::RegisterService(std::shared_ptr<google::protobuf::Service> service) {
  do {
  if (!gRpcServer_->registerService(service)) {
    printf("Start TinyRPC server error, because register protobuf service error, \
      please look up rpc log get more details!\n");
    tinyrpc::Exit(0);
  }
 } while(0);
}


void TinyrpcRunner::StartService() {
  InitConfig();
  InitLogger(gRpcLogger);
  InitServer();
  StartRpcServer(gRpcLogger);
}

TcpServer::ptr TinyrpcRunner::GetServer() {
  return gRpcServer_;
}
Config::ptr TinyrpcRunner::GetConfig() {
  return gRpcConfig_;
}

int TinyrpcRunner::GetIOThreadPoolSize() {
  return gRpcServer_->getIOThreadPool()->getIOThreadPoolSize();
}


void TinyrpcRunner::InitConfig() {
  tinyrpc::SetHook(false);

  #ifdef DECLARE_MYSQL_PULGIN
  // sing libmysqld or libmysqlclient
  int rt = mysql_library_init(0, NULL, NULL);
  if (rt != 0) {
    printf("Start TinyRPC server error, call mysql_library_init error\n");
    mysql_library_end();
    exit(0);
  }
  #endif

  tinyrpc::SetHook(true);

  gRpcConfig_ = std::make_shared<tinyrpc::Config>(configName_.data());
  gRpcConfig_->readConf();
}

void TinyrpcRunner::InitLogger(std::shared_ptr<Logger> &logger) {
  if (logger){
    return ;
  }
  logger = std::make_shared<Logger>();
  logger->init(gRpcConfig_->m_log_prefix.c_str(), gRpcConfig_->m_log_path.c_str(), 
                    gRpcConfig_->m_log_max_size, gRpcConfig_->m_log_sync_inteval);
};
void TinyrpcRunner::InitServer() {
  gRpcServer_ = std::make_shared<TcpServer>(gRpcConfig_.get());
}

void TinyrpcRunner::StartRpcServer(std::shared_ptr<Logger> &logger) {
  logger->start();
  gRpcServer_->start();
}


void TinyrpcRunner::AddTimerEvent(TimerEvent::ptr event) {}

} // namespace tinyrpc