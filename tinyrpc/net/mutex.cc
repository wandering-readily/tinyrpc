#include <pthread.h>
#include <memory>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

// this file copy form sylar

namespace tinyrpc {


CoroutineMutex::CoroutineMutex() {}

CoroutineMutex::~CoroutineMutex() {
  if (m_lock) {
    unlock();
  }
}

void CoroutineMutex::lock() {

  if (Coroutine::IsMainCoroutine()) {
    RpcErrorLog << "main coroutine can't use coroutine mutex";
    return;
  }

  Coroutine* cor = Coroutine::GetCurrentCoroutine();

  // 如果m_lock==false, 那么表明没加锁, 直接Yield()
  // 否则, 加入等待队列
  bool flag = true;
  std::size_t corsSize;
  {
  Mutex::Lock lock(m_mutex);
  if (!m_lock) {
    flag = false;
    m_lock = true;
  } else {
    m_sleep_cors.push(cor);
    corsSize = m_sleep_cors.size();
  }
  }

  if(!flag) {
    RpcDebugLog << "coroutine succ get coroutine mutex";
  } else {
    RpcDebugLog << "coroutine yield, pending coroutine mutex, current sleep queue exist ["
      << corsSize << "] coroutines";

    Coroutine::Yield();
  } 
}

void CoroutineMutex::unlock() {
  if (Coroutine::IsMainCoroutine()) {
    RpcErrorLog << "main coroutine can't use coroutine mutex";
    return;
  }

  // 如果加过锁，那么代表可能有协程在等待, 直接获取
  Coroutine* cor = nullptr;
  {
  Mutex::Lock lock(m_mutex);
  if (m_lock) {
    m_lock = false;
    if (m_sleep_cors.empty()) {
      return;
    }

    cor = m_sleep_cors.front();
    m_sleep_cors.pop();
  }
  }

  if (cor) {
    // wakeup the first cor in sleep queue
    RpcDebugLog << "coroutine unlock, now to resume coroutine[" << cor->getCorId() << "]";

    tinyrpc::Reactor::GetReactor()->addTask([cor]() {
      tinyrpc::Coroutine::Resume(cor);
    }, true);
  }
}


}