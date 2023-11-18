#ifndef TINYRPC_NET_TCP_IO_THREAD_H
#define TINYRPC_NET_TCP_IO_THREAD_H

#include <memory>
#include <map>
#include <atomic>
#include <functional>
#include <semaphore.h>
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/coroutine/coroutine.h"


namespace tinyrpc {

class TcpServer;
class CoroutinePool;

/*
 *  IO 线程就是一个 SubReactor
 */
class IOThread {

 public:
  
  typedef std::shared_ptr<IOThread> sptr;
 	IOThread(CoroutineTaskQueue::wptr corTaskQueue);

  CoroutineTaskQueue::wptr getweakCorTaskQueue() {return weakCorTaskQueue_;}

	~IOThread();  

  Reactor* getReactor();

  pthread_t getPthreadId();

  void setThreadIndex(const int index);

  int getThreadIndex();

  sem_t* getStartSemaphore();


 public:
  static IOThread* GetCurrentIOThread();

 private:
 	static void* main(void* arg);

 private:

  sem_t m_init_semaphore;
  sem_t m_start_semaphore;

 	Reactor* m_reactor {nullptr};
	pthread_t m_thread {0};
	pid_t m_tid {-1};
  TimerEvent::sptr m_timer_event {nullptr};
  int m_index {-1};


private:
  CoroutineTaskQueue::wptr weakCorTaskQueue_;

};

class IOThreadPool : public std::enable_shared_from_this<IOThreadPool> {

 public:
  typedef std::shared_ptr<IOThreadPool> sptr;
  typedef std::weak_ptr<IOThreadPool> wptr;

  IOThreadPool(int size, std::weak_ptr<CoroutinePool>);
  ~IOThreadPool() = default;

  void beginThreadPool(CoroutineTaskQueue::wptr);

  void start();

  IOThread* getIOThread();

  int getIOThreadPoolSize();

  void broadcastTask(std::function<void()> cb);

  void addTaskByIndex(int index, std::function<void()> cb);

  void addCoroutineToRandomThread(Coroutine::sptr cor, bool self = false);

  // add a coroutine to random thread in io thread pool
  // self = false, means random thread cann't be current thread
  // please free cor, or causes memory leak
  // call returnCoroutine(cor) to free coroutine
  tinyrpc::IOThread::sptr getRandomThread(bool);
  Coroutine::sptr addCoroutineToRandomThread(std::function<void()> cb, bool self = false);

  Coroutine::sptr addCoroutineToThreadByIndex(int index, std::function<void()> cb, bool self = false);

  void addCoroutineToEachThread(std::function<void()> cb);

 private:
  int m_size {0};

  std::atomic<int> m_index {-1};

  std::vector<IOThread::sptr> m_io_threads;
  
  std::weak_ptr<CoroutinePool> weakCorPool_;

};


}

#endif
