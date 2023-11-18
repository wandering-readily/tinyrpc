#ifndef TINYRPC_NET_TCP_TCP_SERVER_H
#define TINYRPC_NET_TCP_TCP_SERVER_H

#include <map>
#include <google/protobuf/service.h>
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/abstract_codec.h"
#include "tinyrpc/net/abstract_dispatcher.h"
#include "tinyrpc/net/http/http_dispatcher.h"
#include "tinyrpc/net/http/http_servlet.h"


namespace tinyrpc {

class TcpAcceptor {

 public:

  typedef std::shared_ptr<TcpAcceptor> sptr;
  TcpAcceptor(NetAddress::sptr, FdEventContainer::wptr);

  void init();

  int toAccept();

  ~TcpAcceptor();

  NetAddress::sptr getPeerAddr() {
    return m_peer_addr;
  }

  NetAddress::sptr geLocalAddr() {
    return m_local_addr;
  }
 
 private:
  int m_family {-1};
  int m_fd {-1};

  NetAddress::sptr m_local_addr {nullptr};
  NetAddress::sptr m_peer_addr {nullptr};

  FdEventContainer::wptr weakFdEventPool_;

};


class TcpServer {

 public:

  typedef std::shared_ptr<TcpServer> sptr;

	// TcpServer(NetAddress::sptr addr, ProtocalType type = TinyPb_Protocal);
	TcpServer(Config *config, \
    std::weak_ptr<CoroutinePool>, \
    FdEventContainer::wptr, \
    CoroutineTaskQueue::wptr);

  ~TcpServer();

  void start();

  void addCoroutine(tinyrpc::Coroutine::sptr cor);

  bool registerService(std::shared_ptr<google::protobuf::Service> service);

  bool registerHttpServlet(const std::string& url_path, HttpServlet::sptr servlet);

  TcpConnection::sptr addClient(IOThread* io_thread, int fd);

  void freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::sptr slot);

  IOThreadPool::sptr getSharedIOThreadPool() {return m_io_pool;}

  int64_t getConnectAliveTime() const {return connectAliveTime_;}


 public:
  AbstractDispatcher::sptr getDispatcher();

  AbstractCodeC::sptr getCodec();

  NetAddress::sptr getPeerAddr();

  NetAddress::sptr getLocalAddr();

  IOThreadPool::sptr getIOThreadPool();

  TcpTimeWheel::sptr getTimeWheel();


 private:
  void MainAcceptCorFunc();

  void ClearClientTimerFunc();

 private:
  
  NetAddress::sptr m_addr;

  TcpAcceptor::sptr m_acceptor;

  int m_tcp_counts {0};

  Reactor* m_main_reactor {nullptr};

  bool m_is_stop_accept {false};

  Coroutine::sptr m_accept_cor;
  
  AbstractDispatcher::sptr m_dispatcher;

  AbstractCodeC::sptr m_codec;

  IOThreadPool::sptr m_io_pool;

  ProtocalType m_protocal_type {tinyrpc::ProtocalType::TinyPb_Protocal};

  TcpTimeWheel::sptr m_time_wheel;

  std::map<int, TcpConnection::sptr> m_clients;

  TimerEvent::sptr m_clear_clent_timer_event {nullptr};

  std::weak_ptr<CoroutinePool> weakCorPool_;

  FdEventContainer::wptr weakFdEventPool_;

  int64_t connectAliveTime_ = 10 * 10 * 1000;

};

}

#endif
