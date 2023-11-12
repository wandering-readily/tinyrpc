#include <vector>
#include <sys/mman.h>
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/mutex.h"


namespace tinyrpc {



CoroutinePool::CoroutinePool(int pool_size, int stack_size /*= 1024 * 128 B*/) \
    : m_pool_size(pool_size), m_stack_size(stack_size) {
  // set main coroutine first
  // 先设主协程
  Coroutine::GetCurrentCoroutine();
  
  // 从分配的Memory设置内存
  m_memory_pool.push_back(std::make_shared<Memory>(stack_size, pool_size));

  Memory::ptr tmp = m_memory_pool[0];

  for (int i = 0; i < pool_size; ++i) {
    // 需要得到非NULL的stack_ptr
    // 协程shared_ptr
    Coroutine::ptr cor = std::make_shared<Coroutine>(stack_size, tmp->getBlock());
    cor->setIndex(i);
    m_free_cors.push_back(std::make_pair(cor, false));
  }

}

CoroutinePool::~CoroutinePool() {}

Coroutine::ptr CoroutinePool::getCoroutineInstanse() {

  // from 0 to find first free coroutine which: 1. it.second = false, 2. getIsInCoFunc() is false
  // try our best to reuse used corroutine, and try our best not to choose unused coroutine
  // beacuse used couroutine which used has already write bytes into physical memory, 
  // but unused coroutine no physical memory yet. we just call mmap get virtual address, but not write yet. 
  // so linux will alloc physical when we realy write, that casuse page fault interrupt

  Mutex::Lock lock(m_mutex);
  for (int i = 0; i < m_pool_size; ++i) {
    // 如果一个协程没有在使用
    if (!m_free_cors[i].first->getIsInCoFunc() && !m_free_cors[i].second) {
      m_free_cors[i].second = true;
      Coroutine::ptr cor = m_free_cors[i].first;
      return cor;
    }
  }

  // 因为m_memory_pool[0]全用来分配m_free_cors
  // 先检查已有的内存池
  for (size_t i = 1; i < m_memory_pool.size(); ++i) {
    char* tmp = m_memory_pool[i]->getBlock();
    if(tmp) {
      Coroutine::ptr cor = std::make_shared<Coroutine>(m_stack_size, tmp);
      return cor;
    }    
  }
  // 如果内存池不够，再去开辟一个内存池
  m_memory_pool.push_back(std::make_shared<Memory>(m_stack_size, m_pool_size));
  return std::make_shared<Coroutine>(m_stack_size, m_memory_pool[m_memory_pool.size() - 1]->getBlock());
}

// 返回协程
void CoroutinePool::returnCoroutine(Coroutine::ptr cor) {
  int i = cor->getIndex();
  if (i >= 0 && i < m_pool_size) {
    // 如果是固定分配时，直接不使用便可
    m_free_cors[i].second = false;
  } else {
    // 否则要返回m_memory_pool内存
    for (size_t i = 1; i < m_memory_pool.size(); ++i) {
      // 识别协程的m_stack_sp在的区间
      if (m_memory_pool[i]->hasBlock(cor->getStackPtr())) {
        m_memory_pool[i]->backBlock(cor->getStackPtr());
      }
    }
  }
}



}