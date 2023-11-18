#ifndef TINYRPC_COROUTINE_COUROUTINE_POOL_H
#define TINYRPC_COROUTINE_COUROUTINE_POOL_H

#include <vector>
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/coroutine/memory.h"

namespace tinyrpc {

class CoroutinePool {

public:
  typedef std::shared_ptr<CoroutinePool> sptr;
  typedef std::weak_ptr<CoroutinePool> wptr;

 public:
  CoroutinePool(int pool_size, int stack_size = 1024 * 128);
  ~CoroutinePool();

  Coroutine::sptr getCoroutineInstanse();

  void returnCoroutine(Coroutine::sptr cor);

 private:
  int m_pool_size {0};
  int m_stack_size {0};

  // first--ptr of cor
  // second
  //    false -- can be dispatched
  //    true -- can't be dispatched
  std::vector<std::pair<Coroutine::sptr, bool>> m_free_cors;

  Mutex m_mutex;

  std::vector<Memory::sptr> m_memory_pool;
};


}


#endif