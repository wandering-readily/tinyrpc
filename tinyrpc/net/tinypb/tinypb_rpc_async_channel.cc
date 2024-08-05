#include <memory>
#include <future>
#include <google/protobuf/service.h>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/comm/error_code.h"
#include "tinyrpc/net/tcp/tcp_client.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_async_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_controller.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/start.h"
#include "tinyrpc/comm/run_time.h"
#include "tinyrpc/comm/msg_req.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/coroutine/coroutine.h"

#include "tinyrpc/net/reactor.h"

namespace tinyrpc {

TinyPbRpcAsyncChannel::TinyPbRpcAsyncChannel(NetAddress::sptr addr) {
  m_rpc_channel = std::make_shared<TinyPbRpcChannel>(addr);
  // m_current_iothread设置为当前IO线程
  m_current_iothread = IOThread::GetCurrentIOThread();
  // m_current_cor先设置当前mainCoroutine
  m_current_cor = Coroutine::GetCurrentCoroutine();

  if (Coroutine::IsMainCoroutine() || !m_current_iothread) {
    // 如果是mainCoroutine rpc call() 或者当前为进程的主线程，那么就用sem通知
    mainCorSet = true;
    int rt = sem_init(getMainCorSemaphorePtr(), 0, 0);
    assert(rt==0);
  }
}

TinyPbRpcAsyncChannel::~TinyPbRpcAsyncChannel() {
  // RpcDebugLog << "~TinyPbRpcAsyncChannel(), return coroutine";
  if (m_pending_cor) {
    tinyrpc::CoroutinePool::sptr corPool = weakCorPool_.lock();
    assert(corPool != nullptr && "corPool had released");
    corPool->returnCoroutine(m_pending_cor);
  }

  if (mainCorSet) {
    sem_destroy(&mainCor_semaphore);
  }
}

TinyPbRpcChannel* TinyPbRpcAsyncChannel::getRpcChannel() {
  return m_rpc_channel.get();
}

/*
 * 在 RPC 调用前必须调用 TinyPbRpcAsyncChannel::saveCallee(), 提前预留资源的引用计数
 */
void TinyPbRpcAsyncChannel::saveCallee(con_sptr controller, \
    msg_sptr req, msg_sptr res, clo_sptr closure, \
    CoroutinePool::wptr corPool, 
    IOThread *thread) {
  m_controller = controller;
  m_req = req;
  m_res = res;
  m_closure = closure;
  m_is_pre_set = true;
  weakCorPool_ = corPool;
  m_chosed_iothread = thread;
}

void TinyPbRpcAsyncChannel::CallMethod(const google::protobuf::MethodDescriptor* method, 
    google::protobuf::RpcController* controller, 
    const google::protobuf::Message* request, 
    google::protobuf::Message* response, 
    google::protobuf::Closure* done) {
  
  TinyPbRpcController* rpc_controller = dynamic_cast<TinyPbRpcController*>(controller);
  if (!m_is_pre_set) {
    RpcErrorLog << "Error! must call [saveCallee()] function before [CallMethod()]"; 
    TinyPbRpcController* rpc_controller = dynamic_cast<TinyPbRpcController*>(controller);
    rpc_controller->SetError(ERROR_NOT_SET_ASYNC_PRE_CALL, "Error! must call [saveCallee()] function before [CallMethod()];");
    m_is_finished = true;
    return;
  }
  RunTime* run_time = getCurrentRunTime();
  if (run_time) {
    rpc_controller->SetMsgReq(run_time->m_msg_no);
    RpcDebugLog << "get from RunTime succ, msgno=" << run_time->m_msg_no;
  } else {
    // 调用rpc_channel时，从这生成msg_req_no
    rpc_controller->SetMsgReq(MsgReqUtil::genMsgNumber());
    RpcDebugLog << "get from RunTime error, generate new msgno=" << rpc_controller->MsgSeq();
  }

  TinyPbRpcAsyncChannel::sptr s_ptr = shared_from_this();

  auto cb = [s_ptr, method]() mutable {
    // 1. 完成rpcChannel的callMethod()任务
    RpcDebugLog << "now excute rpc call method by this thread";
    s_ptr->getRpcChannel()->CallMethod(method, s_ptr->getControllerPtr(), s_ptr->getRequestPtr(), s_ptr->getResponsePtr(), NULL);

    RpcDebugLog << "excute rpc call method by this thread finish";

    // 2. 回调任务
    // 本质上是ClosurePtr()任务，finished设置任务，mainCorourine恢复任务
    // 这写任务必须
    auto call_back = [s_ptr]() mutable {
      RpcDebugLog << "async excute rpc call method back old thread";
      // callback function excute in origin thread
      if (s_ptr->getClosurePtr() != nullptr) {
        s_ptr->getClosurePtr()->Run();
      }
      // rpcChannel()任务完成后 直接作用于wait()函数
      s_ptr->setFinished(true);

      // 这里是为了设置wait()异步等待结果
      // 从wait()的Yield()地方回去
      if (s_ptr->getNeedResume()) {
        RpcDebugLog << "async excute rpc call method back old thread, need resume";
        Coroutine::Resume(s_ptr->getCurrentCoroutine());
      }
      s_ptr.reset();
    };

    // 本IO线程curIOThread承担还原callback任务
    if (s_ptr->isWaitedOnThread_semNotify()) {
      // 如果是client，在我们的设置中进程的主线程没有IOThread*, 
      // 因此s_ptr->getIOThread()将会是nullptr
      // 这样会引发错误
      // 所以采用sem_t通知
      sem_post(s_ptr->getMainCorSemaphorePtr());
    } else {

      // 本IO线程的mainCoroutine执行callBack的任务
      // 不能在执行协程m_pending_cor中执行该任务
      s_ptr->getIOThread()->getReactor()->addTask(call_back, true);
    }
    s_ptr.reset();
  };
  // m_pending_cor是寻找的新coroutine(cb函数是cb)
  // 转换进去m_pending_cor 将作为cb放入任一线程(但是不能在本线程当中)
  // m_pending_cor = GetServer()->getIOThreadPool()->addCoroutineToRandomThread(cb, false);

  // 自己选定线程异步执行该协程任务
  tinyrpc::CoroutinePool::sptr corPool = weakCorPool_.lock();
  assert(corPool != nullptr && "corPool had released");
  m_pending_cor = corPool->getCoroutineInstanse();
  m_pending_cor->setCallBack(cb);
  m_chosed_iothread->getReactor()->addCoroutine(m_pending_cor, true);
}

void TinyPbRpcAsyncChannel::wait() {
  if (m_is_finished) {
    return;
  }
  if (isWaitedOnThread_semNotify()) {
    sem_wait(getMainCorSemaphorePtr());
    if (getClosurePtr() != nullptr) {
      RpcDebugLog << "async excute rpc call method back old thread";
      getClosurePtr()->Run();
    }
  } else {
    m_need_resume = true;
    Coroutine::Yield();
  }
  m_is_finished = true;
}

void TinyPbRpcAsyncChannel::setFinished(bool value) {
  m_is_finished = true;
}


Coroutine* TinyPbRpcAsyncChannel::getCurrentCoroutine() {
  return m_current_cor;
}

IOThread* TinyPbRpcAsyncChannel::getIOThread() {
  return m_current_iothread;
}

bool TinyPbRpcAsyncChannel::getNeedResume() {
  return m_need_resume;
}

google::protobuf::RpcController* TinyPbRpcAsyncChannel::getControllerPtr() {
  return m_controller.get();
}

google::protobuf::Message* TinyPbRpcAsyncChannel::getRequestPtr() {
  return m_req.get();
}

google::protobuf::Message* TinyPbRpcAsyncChannel::getResponsePtr() {
  return m_res.get();
}

google::protobuf::Closure* TinyPbRpcAsyncChannel::getClosurePtr() {
  return m_closure.get();
}

}