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

// 定义的single gloabl instance
tinyrpc::Config::ptr gRpcConfig;
tinyrpc::Logger::ptr gRpcLogger;
// ???
// 待验证
tinyrpc::TcpServer::ptr gRpcServer;

// ???
// 这个g_init_config应该不应用在多线程服务下
static int g_init_config = 0;

void InitConfig(const char* file) {
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

  if (g_init_config == 0) {
    gRpcConfig = std::make_shared<tinyrpc::Config>(file);
    gRpcConfig->readConf();
    g_init_config = 1;
  }
}

// void RegisterService(google::protobuf::Service* service) {
//   gRpcServer->registerService(service);
// }

TcpServer::ptr GetServer() {
  return gRpcServer;
}

void StartRpcServer() {
  gRpcLogger->start();
  gRpcServer->start();
}

int GetIOThreadPoolSize() {
  return gRpcServer->getIOThreadPool()->getIOThreadPoolSize();
}

Config::ptr GetConfig() {
  return gRpcConfig;
}

void AddTimerEvent(TimerEvent::ptr event) {

}

}