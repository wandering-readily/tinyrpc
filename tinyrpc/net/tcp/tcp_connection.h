#ifndef TINYRPC_NET_TCP_TCP_CONNECTION_H
#define TINYRPC_NET_TCP_TCP_CONNECTION_H

#include <memory>
#include <vector>
#include <queue>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/tcp/tcp_buffer.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/http/http_request.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {

class TcpServer;
class TcpClient;
class IOThread;

enum TcpConnectionState {
	NotConnected = 1,		  // can do io
	Connected = 2,		    // can do io
	HalfClosing = 3,			// server call shutdown, write half close. can read,but can't write
	Closed = 4,				    // can't do io
};


class TcpConnection : public std::enable_shared_from_this<TcpConnection> {

 public:
 	typedef std::shared_ptr<TcpConnection> sptr;
 	typedef std::weak_ptr<TcpConnection> wptr;

  friend class RpcClient;

	TcpConnection(tinyrpc::TcpServer *, tinyrpc::IOThread *, \
    int, int, NetAddress::sptr, \
    std::weak_ptr<CoroutinePool>, std::weak_ptr<FdEventContainer>);

	// TcpConnection(tinyrpc::TcpClient* tcp_cli, tinyrpc::Reactor* reactor, 
	TcpConnection(AbstractCodeC::sptr, tinyrpc::Reactor*, \
    int, int, NetAddress::sptr, NetAddress::sptr, \
    FdEventContainer::wptr);

  void setUpClient();

	~TcpConnection();

  void initBuffer(int size);

  enum class ConnectionType {
    ServerConnection = 1,     // owned by tcp_server
    ClientConnection = 2,     // owned by tcp_client
  };

 public:
 

  void shutdownConnection();

  TcpConnectionState getState();

  void setState(const TcpConnectionState& state);

  TcpBuffer* getInBuffer();

  TcpBuffer* getOutBuffer();

  AbstractCodeC::sptr getCodec() const;

  bool getResPackageData(const std::string& msg_req, TinyPbStruct::pb_sptr& pb_struct);

  void registerToTimeWheel();

  Coroutine::sptr getCoroutine();

  int64_t getServerCloseConnTime() const {return serverCloseConnTime_;}

  void resetServerCloseConnTime(); 

 public:
  void MainServerLoopCorFunc();

  void input();

  void execute(); 

	void output();

  void setOverTimeFlag(bool value);

  bool getOverTimerFlag();

  void initServer();

  int getFd() const {return m_fd;}

  void setFd(int fd) {m_fd = fd;}

  void setWeakSlot(std::weak_ptr<AbstractSlot<TcpConnection>> slot) {
    m_weak_slot = slot;
  }

  NetAddress::sptr getLocalAddr() {return m_local_addr;}
  NetAddress::sptr getPeerAddr() {return m_peer_addr;}

  bool isServerConn() {return m_connection_type == ConnectionType::ServerConnection;}

  void freshTcpConnection (std::shared_ptr<AbstractSlot<TcpConnection>>);

 private:
  void clearClient();

 private:
  TcpServer* m_tcp_svr {nullptr};
  // 解耦TcpClient* 
  // TCPClient 输入TCPClient*变成输入AbstractCodeC::sptr m_codec
  // TcpClient* m_tcp_cli {nullptr};

  IOThread* m_io_thread {nullptr};
  Reactor* m_reactor {nullptr};

  int m_fd {-1};
  TcpConnectionState m_state {TcpConnectionState::Connected};
  ConnectionType m_connection_type {ConnectionType::ServerConnection};

  NetAddress::sptr m_peer_addr;
  NetAddress::sptr m_local_addr;

	TcpBuffer::sptr m_read_buffer;
	TcpBuffer::sptr m_write_buffer;

  Coroutine::sptr m_loop_cor;

  AbstractCodeC::sptr m_codec;

  FdEvent::sptr m_fd_event;

  bool m_stop {false};

  bool m_is_over_time {false};

  std::map<std::string, TinyPbStruct::pb_sptr> m_reply_datas;

  std::weak_ptr<AbstractSlot<TcpConnection>> m_weak_slot;

  RWMutex m_mutex;

  std::weak_ptr<CoroutinePool> weakCorPool_;

  FdEventContainer::wptr weakFdEventPool_;

  std::atomic_int64_t serverCloseConnTime_;
};

}

#endif
