#ifndef TINYRPC_NET_TINYPB_TINYPB_RPC_ASYNC_CHANNEL_H
#define TINYRPC_NET_TINYPB_TINYPB_RPC_ASYNC_CHANNEL_H 

#include <google/protobuf/service.h>
#include "tinyrpc/net/tinypb/tinypb_data.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_controller.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/tcp_client.h"
#include "tinyrpc/coroutine/coroutine.h"

namespace tinyrpc {

class AsyncCor_Waiter {
 
  
 public:
  typedef std::shared_ptr<AsyncCor_Waiter> sptr;

 public:

  virtual void wait() = 0;
  
  virtual ~AsyncCor_Waiter() = default;
};

class TinyPbRpcAsyncChannel \
    : public google::protobuf::RpcChannel, 
    public std::enable_shared_from_this<TinyPbRpcAsyncChannel>, 
    public AsyncCor_Waiter {

 public:
  typedef std::shared_ptr<TinyPbRpcAsyncChannel> sptr;
  typedef std::shared_ptr<google::protobuf::RpcController> con_sptr;
  typedef std::shared_ptr<google::protobuf::Message> msg_sptr;
  typedef std::shared_ptr<google::protobuf::Closure> clo_sptr;

  TinyPbRpcAsyncChannel(NetAddress::sptr);
  ~TinyPbRpcAsyncChannel();

  void CallMethod(const google::protobuf::MethodDescriptor* method, 
      google::protobuf::RpcController* controller, 
      const google::protobuf::Message* request, 
      google::protobuf::Message* response, 
      google::protobuf::Closure* done);


  TinyPbRpcChannel* getRpcChannel();

  // must call saveCallee before CallMethod
  // in order to save shared_ptr count of req res controller
  void saveCallee(con_sptr controller, msg_sptr req, msg_sptr res, clo_sptr closure, \
    std::weak_ptr<CoroutinePool>, IOThread *);

  virtual void wait();

  void setFinished(bool value);

  bool getNeedResume();

  IOThread* getIOThread();

  Coroutine* getCurrentCoroutine();

  sem_t *getMainCorSemaphorePtr() {return &mainCor_semaphore;}

  bool isMainCorSet() {return mainCorSet;}

  google::protobuf::RpcController* getControllerPtr();

  google::protobuf::Message* getRequestPtr();

  google::protobuf::Message* getResponsePtr();

  google::protobuf::Closure* getClosurePtr();


 private:
  TinyPbRpcChannel::sptr m_rpc_channel;
  Coroutine::sptr m_pending_cor;
  Coroutine* m_current_cor {NULL};
  IOThread* m_current_iothread {NULL};
  IOThread* m_chosed_iothread {NULL};
  // 不同的线程读写m_is_finished, m_need_resume
  // 所以改为std::atomic_bool
  std::atomic_bool m_is_finished {false};
  std::atomic_bool m_need_resume {false};
  bool m_is_pre_set {false};

 private:
  con_sptr m_controller;
  msg_sptr m_req;
  msg_sptr m_res;
  clo_sptr m_closure;

  std::weak_ptr<CoroutinePool> weakCorPool_;

  bool mainCorSet = false;
  sem_t mainCor_semaphore;
};

}



#endif