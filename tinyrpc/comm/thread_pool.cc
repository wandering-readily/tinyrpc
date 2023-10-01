#include <pthread.h>
#include <queue>
#include <functional>
#include "tinyrpc/comm/thread_pool.h"

namespace tinyrpc {

void* ThreadPool::MainFunction(void* ptr) {
  ThreadPool* pool = reinterpret_cast<ThreadPool*>(ptr);
  // ???
  // 这里m_condition为什么还要初始化呢?
  // pthread_cond_init(&pool->m_condition, NULL);

  while (!pool->m_is_stop) {
    std::function<void()> cb;
    {
    Mutex::Lock lock(pool->m_mutex);

    while (pool->m_tasks.empty()) {
      pthread_cond_wait(&(pool->m_condition), pool->m_mutex.getMutex());
    }
    cb = pool->m_tasks.front();
    pool->m_tasks.pop();
    }

    cb();
  }
  return nullptr;

}


ThreadPool::ThreadPool(int size) : m_size(size) {
  for (int i = 0; i < m_size; ++i) {
    pthread_t thread;
    m_threads.emplace_back(thread);
  }
  pthread_cond_init(&m_condition, nullptr);

}

void ThreadPool::start() {
  for (int i = 0; i < m_size; ++i) {
    pthread_create(&m_threads[i], nullptr, &ThreadPool::MainFunction, this);
  }
  
}

void ThreadPool::stop() {
  m_is_stop = true;
}


void ThreadPool::addTask(std::function<void()> cb) {
  Mutex::Lock lock(m_mutex);
  m_tasks.push(cb);
  pthread_cond_signal(&m_condition);
}

ThreadPool::~ThreadPool() {
  // for (int i = 0; i < m_size; ++i) {
  //   pthread_join(m_threads[i], nullptr);
  // }

  // ???
  // 为什么删掉呢？
  for (int i = 0; i < m_size; ++i) {
    pthread_join(m_threads[i], nullptr);
  }
  pthread_cond_destroy(&m_condition);
}

}