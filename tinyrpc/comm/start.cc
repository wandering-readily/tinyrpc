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


extern bool Init_t_msg_req_len(int);
extern bool Init_m_max_connect_timeout(int);

tinyrpc::Logger::sptr gRpcLogger;

TinyrpcServer::TinyrpcServer(const char *configName) : configName_(configName) {
  InitServiceConfig();
}
TinyrpcServer::TinyrpcServer(const std::string &configName) : configName_(configName) {
  InitServiceConfig();
}

void TinyrpcServer::InitServiceConfig() {
  InitConfig();

  corPool_ = std::make_shared<CoroutinePool>(
      gRpcConfig_->m_cor_pool_size, gRpcConfig_->m_cor_stack_size);
  fdEventPool_ = std::make_shared<FdEventContainer>(1000);
  coroutine_task_queue_ = std::make_shared<CoroutineTaskQueue>();

  InitLogger(gRpcLogger);
  InitServer();
  gRpcLogger->start();
}

void TinyrpcServer::StartRpcServer() {
  gRpcServer_->start();
}

TcpServer::sptr TinyrpcServer::GetServer() {
  return gRpcServer_;
}
Config::sptr  TinyrpcServer::GetConfig() {
  return gRpcConfig_;
}


void TinyrpcServer::InitConfig() {
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

  // 根据config设置两个static变量
  Init_t_msg_req_len(gRpcConfig_->m_msg_req_len);
  Init_m_max_connect_timeout(gRpcConfig_->m_max_connect_timeout);
}

void TinyrpcServer::InitLogger(Logger::sptr &logger) {
  if (logger){
    return ;
  }
  logger = std::make_shared<Logger>();
  logger->init(gRpcConfig_->m_log_prefix.c_str(), gRpcConfig_->m_log_path.c_str(), 
                    gRpcConfig_->m_log_max_size, gRpcConfig_->m_log_sync_inteval);
};
void TinyrpcServer::InitServer() {
  gRpcServer_ = std::make_shared<TcpServer>(gRpcConfig_.get(), \
    corPool_, fdEventPool_, coroutine_task_queue_);
}


} // namespace tinyrpc