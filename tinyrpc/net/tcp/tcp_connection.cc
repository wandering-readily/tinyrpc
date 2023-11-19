#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/tcp/tcp_client.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"
#include "tinyrpc/net/tinypb/tinypb_data.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/net/tcp/abstract_slot.h"
#include "tinyrpc/net/timer.h"



namespace details {


void connKeepAlive(tinyrpc::TcpConnection::sptr conn) {
  // 保活机制
  int64_t now = details::getNowMs();
  // 误差10ms
  if (conn->getServerCloseConnTime() > now + 10) {
    // 这里已经没到断开连接时间，注定不会onn->m_weak_slot.lock()不会析构
    // 避免多道shutdown命令影响性能，同时确定mainReacotr的timeWheel还有没有连接
    // m_weak_slot对应的shared_ptr
    std::function<void(tinyrpc::TcpConnection::sptr)> cb = \
      std::bind(connKeepAlive, std::placeholders::_1);
    tinyrpc::TcpTimeWheel::TcpConnectionSlot::sptr tmp = \
      std::make_shared<tinyrpc::AbstractSlot<tinyrpc::TcpConnection>>(conn->shared_from_this(), cb);
    conn->setWeakSlot(tmp);
    conn->freshTcpConnection(tmp);
    
  } else {
    conn->shutdownConnection();
  }

  // conn->shutdownConnection();
}

};

namespace tinyrpc {

TcpConnection::TcpConnection(tinyrpc::TcpServer* tcp_svr, tinyrpc::IOThread* io_thread, \
    int fd, int buff_size, NetAddress::sptr peer_addr, \
    CoroutinePool::wptr corPool, FdEventContainer::wptr fdEventPool) \
    : m_io_thread(io_thread), m_fd(fd), m_state(Connected), \
    m_connection_type(ConnectionType::ServerConnection), \
    m_peer_addr(peer_addr), \
    weakCorPool_(corPool), \
    weakFdEventPool_(fdEventPool), \
    serverCloseConnTime_(0) {	

  m_reactor = m_io_thread->getReactor();

  // RpcDebugLog << "m_state=[" << m_state << "], =" << fd;
  m_tcp_svr = tcp_svr;
  m_local_addr = tcp_svr->getLocalAddr();

  m_codec = m_tcp_svr->getCodec();

  tinyrpc::FdEventContainer::sptr sharedFdEventPool = weakFdEventPool_.lock();
  assert(sharedFdEventPool != nullptr && "sharedFdEventPool had released");
  m_fd_event = sharedFdEventPool->getFdEvent(fd);
  m_fd_event->setReactor(m_reactor);
  initBuffer(buff_size); 
  // IO线程自己在创建的时候已经建立
  //  void* IOThread::main(void* arg) 
  //    Coroutine* Coroutine::GetCurrentCoroutine()
  // 而要完成任务的协程从协程池中获取，这些协程都需要归还
  tinyrpc::CoroutinePool::sptr sharedCorPool = weakCorPool_.lock();
  assert(sharedCorPool != nullptr && "sharedCorPool had released");
  m_loop_cor = sharedCorPool->getCoroutineInstanse();

  m_state = Connected;
  RpcDebugLog << "succ create tcp connection[" << m_state << "], fd=" << fd;
}

TcpConnection::TcpConnection(AbstractCodeC::sptr codec, tinyrpc::Reactor* reactor, \
    int fd, int buff_size, NetAddress::sptr peer_addr,  NetAddress::sptr local_addr, \
    FdEventContainer::wptr fdEventPool)
    :  m_fd(fd),  m_state(NotConnected), \
    m_connection_type(ConnectionType::ClientConnection), \
    m_peer_addr(peer_addr), m_local_addr(local_addr), \
    weakFdEventPool_(fdEventPool) {

  // m_reactor = reactor;
  // m_tcp_cli = tcp_cli;
  m_codec = codec;

  tinyrpc::FdEventContainer::sptr sharedFdEventPool = weakFdEventPool_.lock();
  assert(sharedFdEventPool != nullptr && "sharedFdEventPool had released");
  // tcp_connection中包含了server coonection 和 client 转发异步connection(coroutine实现)
  // 因此开启reactor相关设置
  // 但是client 同步connection(单线程)已经设置为nullptr
  m_fd_event = sharedFdEventPool->getFdEvent(fd);
  m_fd_event->setReactor(m_reactor);
  initBuffer(buff_size); 

  RpcDebugLog << "succ create tcp connection[NotConnected]";

}

void TcpConnection::initServer() {
  resetServerCloseConnTime();
  registerToTimeWheel();
  m_loop_cor->setCallBack(std::bind(&TcpConnection::MainServerLoopCorFunc, this));
}

void TcpConnection::freshTcpConnection (std::shared_ptr<AbstractSlot<TcpConnection>> slot) {
  this->m_tcp_svr->freshTcpConnection(slot);
}

void TcpConnection::registerToTimeWheel() {
  // cb回调函数不持有conn shared_ptr指针，因为参数传入
  std::function<void(TcpConnection::sptr)> cb = \
      std::bind(details::connKeepAlive, std::placeholders::_1);
  // shared_from_this()传入shared_ptr指针
  // 但是 在AbstractSlot<TcpConnection>中将shared_ptr转化为weak_ptr
  // 因此仍不持有shared_ptr指针
  // 如果ConnectionPtr自己释放，那么将不会执行回调函数cb，也就不会shutdownConnection
  TcpTimeWheel::TcpConnectionSlot::sptr tmp = 
      std::make_shared<AbstractSlot<TcpConnection>>(shared_from_this(), cb);
  m_weak_slot = tmp;
  // 将回调函数放入timewheel
  // 服务器将调用shuown(SHUT_RDWR)，优雅地结束服务器的单方向连接
  // 查阅资料SHUT_RDWR会在tcp发送窗口缓存全部发送完毕后，发送FIN报文
  this->freshTcpConnection(tmp);

}

void TcpConnection::setUpClient() {
  setState(Connected);
}

// 也就是TcpConnection结束，才释放coroutine
TcpConnection::~TcpConnection() {
  if (m_connection_type == ConnectionType::ServerConnection) {
    tinyrpc::CoroutinePool::sptr sharedCorPool = weakCorPool_.lock();
    assert(sharedCorPool != nullptr && "sharedFdEventPool had released");
    sharedCorPool->returnCoroutine(m_loop_cor);

    // std::cout << "server conn " << m_fd << " out" << "\n\n";
  // } else {

    // std::cout << "client conn " << m_fd << " out" << "\n\n";
  }

  // serverCloseConnTime_ = 0;
  RpcDebugLog << "~TcpConnection, fd=" << m_fd;
}

void TcpConnection::initBuffer(int size) {

  // 初始化缓冲区大小
  m_write_buffer = std::make_shared<TcpBuffer>(size);
  m_read_buffer = std::make_shared<TcpBuffer>(size);

}

void TcpConnection::MainServerLoopCorFunc() {

  while (!m_stop) {
    input();

    execute();

    output();
  }
  RpcInfoLog << "this connection has already end loop";
}

void TcpConnection::input() {
  if (m_is_over_time) {
    RpcInfoLog << "over timer, skip input progress";
    return;
  }
  TcpConnectionState state = getState();
  if (state == Closed || state == NotConnected) {
    return;
  }
  bool read_all = false;
  bool close_flag = false;
  int count = 0;

  while (!read_all) {

    if (m_read_buffer->writeAble() == 0) {
      m_read_buffer->resizeBuffer(2 * m_read_buffer->getSize());
    }

    int read_count = m_read_buffer->writeAble();
    int write_index = m_read_buffer->writeIndex();

    RpcDebugLog << "m_read_buffer size=" << m_read_buffer->getBufferVector().size() << "rd=" << m_read_buffer->readIndex() << "wd=" << m_read_buffer->writeIndex();
    tinyrpc::FdEventContainer::sptr fdEventPool = weakFdEventPool_.lock();
    assert(fdEventPool != nullptr && "fdEventPool had released");
    int rt = read_hook(fdEventPool->getFdEvent(m_fd), &(m_read_buffer->m_buffer[write_index]), read_count);
    int savedErrno = errno;
    if (rt > 0) {
      count += rt;
      m_read_buffer->recycleWrite(rt);
    }
    // RpcDebugLog << "m_read_buffer size=" << m_read_buffer->getBufferVector().size() << "rd=" << m_read_buffer->readIndex() << "wd=" << m_read_buffer->writeIndex();
    // RpcDebugLog << "read data back, fd=" << m_fd;

    if (m_is_over_time) {
      RpcInfoLog << "over timer, now break read function";
      break;
    }
    
    // rt <= 0 server和client需要调用此段代码
    // server shutdown后, 发送FIN
    // client接收到Fin, read() ==> 0
    // client关闭连接, 发送FIN
    // server接收到FIN, read() ==> 0, server关闭连接
    // 这样就避免直接close, 有些数据没有及时读取和发送
    if (rt <= 0) {
      if (rt == 0 || savedErrno == EAGAIN) {
        // 如果是mainCor，那么直接关闭连接 ==> client 同步异步连接都必须在mainCor
        // 如果不在mainCor，那么在IOThread，就已经读完所有数据
        read_all = true;
        if (isServerConn()) {
          close_flag = true;
        }
        break;

      } else if (savedErrno == EINTR) {
        continue;

      } else {
        locateErrorExit
      }
      // RpcDebugLog << "rt < 0";
      // RpcErrorLog << "read empty while occur read event, because of peer close, fd= " << m_fd << ", sys error=" << strerror(errno) << ", now to clear tcp connection";
    } else {
      if (rt == read_count) {
        // RpcDebugLog << "read_count == rt";
        // is is possible read more data, should continue read
        // 这里必读入字节，而且packageLen初始值为0
          /*
           * ???
           * !!!
           * if (count == packageLen) 
           * 这里有一个问题，
           *    如果rt == read_count 且 package全部传入完
           *    这会让read_hook会在EPOLL_WAIT等待
           * 
           * 在tinypb_codec方法 可以分析packageLen，那么很好避免这一点
           *
           * 而HTTP方法 header['Content-Type']不好分析packageLen
           */
        continue;
      } else if (rt < read_count) {
        // RpcDebugLog << "read_count > rt";
        // read all data in socket buffer, skip out loop
        read_all = true;
        break;
      }
    }
  }

  if (close_flag) {
    // 关闭服务器对客户的设置
    clearClient();
    RpcDebugLog << "peer close, now yield current coroutine, wait main thread clear this TcpConnection";
    Coroutine::GetCurrentCoroutine()->setCanResume(false);
    Coroutine::Yield();
    // return;
  }

  if (m_is_over_time) {
    return;
  }

  if (!read_all) {
    RpcErrorLog << "not read all data in socket buffer";
  }
  RpcInfoLog << "recv [" << count << "] bytes data from [" << m_peer_addr->toString() << "], fd [" << m_fd << "]";
  if (m_connection_type == ConnectionType::ServerConnection) {
    this->resetServerCloseConnTime();
    // TcpTimeWheel::TcpConnectionSlot::sptr tmp = m_weak_slot.lock();
    // if (tmp) {
      // m_tcp_svr->freshTcpConnection(tmp);
    // }
  }

}

void TcpConnection::execute() {
  // RpcDebugLog << "begin to do execute";

  // it only server do this
  while(m_read_buffer->readAble() > 0) {
    AbstractData::sptr data;
    if (m_codec->getProtocalType() == ProtocalType::TinyPb_Protocal) {
      data = std::make_shared<TinyPbStruct>();
    } else {
      data = std::make_shared<HttpRequest>();
    }

    m_codec->decode(m_read_buffer.get(), data.get());
    // RpcDebugLog << "parse service_name=" << pb_struct.service_full_name;
    if (!data->decode_succ) {
      RpcErrorLog << "it parse request error of fd " << m_fd;
      break;
    }
    // RpcDebugLog << "it parse request success";
    if (m_connection_type == ConnectionType::ServerConnection) {
      // RpcDebugLog << "to dispatch this package";
      m_tcp_svr->getDispatcher()->dispatch(data.get(), this);
      // RpcDebugLog << "contine parse next package";
    } else if (m_connection_type == ConnectionType::ClientConnection) {
      // TODO:
      TinyPbStruct::pb_sptr tmp = std::dynamic_pointer_cast<TinyPbStruct>(data);
      if (tmp) {
        m_reply_datas.insert(std::make_pair(tmp->msg_req, tmp));
      }
    }

  }

}

void TcpConnection::output() {
  if (m_is_over_time) {
    RpcInfoLog << "over timer, skip output progress";
    return;
  }
  while(true) {
    TcpConnectionState state = getState();
    if (state != Connected) {
      break;
    }

    if (m_write_buffer->readAble() == 0) {
      RpcDebugLog << "app buffer of fd[" << m_fd << "] no data to write, to yiled this coroutine";
      break;
    }
    
    int total_size = m_write_buffer->readAble();
    int read_index = m_write_buffer->readIndex();
    tinyrpc::FdEventContainer::sptr fdEventPool = weakFdEventPool_.lock();
    assert(fdEventPool != nullptr && "fdEventPool had released");
    int rt = write_hook(fdEventPool->getFdEvent(m_fd), &(m_write_buffer->m_buffer[read_index]), total_size);
    int savedErrno = errno;
    // RpcInfoLog << "write end";
    if (rt <= 0) {
      if (rt == 0 || savedErrno == EAGAIN) {
        break;

      } else if (savedErrno == EINTR) {
        continue;

      } else {
        locateErrorExit
      }
      
    } else {

      RpcDebugLog << "succ write " << rt << " bytes";
      m_write_buffer->recycleRead(rt);
      RpcDebugLog << "recycle write index =" << m_write_buffer->writeIndex() << ", read_index =" << m_write_buffer->readIndex() << "readable = " << m_write_buffer->readAble();
      RpcInfoLog << "send[" << rt << "] bytes data to [" << m_peer_addr->toString() << "], fd [" << m_fd << "]";
    }

    if (m_write_buffer->readAble() <= 0) {
      // RpcInfoLog << "send all data, now unregister write event on reactor and yield Coroutine";
      RpcInfoLog << "send all data, now unregister write event and break";
      // m_fd_event->delListenEvents(IOEvent::WRITE);
      break;
    }

    if (m_is_over_time) {
      RpcInfoLog << "over timer, now break write function";
      break;
    }

  }
}


void TcpConnection::clearClient() {
  // 关闭连接，那么在清除连接端函数TcpServer::ClearClientTimerFunc()
  // 知晓设置CLOSED状态后，减少shared_ptr<TcpConnection>的计数
  // 从而关闭TcpConnection，换回coroutine
  if (getState() == Closed) {
    RpcDebugLog << "this client has closed";
    return;
  }
  // first unregister epoll event
  m_fd_event->unregisterFromReactor(); 

  // 结束当前coroutine循环
  // stop read and write cor
  m_stop = true;

  // 关闭fd
  close(m_fd_event->getFd());
  setState(Closed);

}

void TcpConnection::shutdownConnection() {
  TcpConnectionState state = getState();
  if (state == Closed || state == NotConnected) {
    RpcDebugLog << "this client has closed";
    return;
  }
  setState(HalfClosing);
  RpcInfoLog << "shutdown conn[" << m_peer_addr->toString() << "], fd=" << m_fd;
  // call sys shutdown to send FIN
  // wait client done something, client will send FIN
  // and fd occur read event but byte count is 0
  // then will call clearClient to set CLOSED
  // IOThread::MainLoopTimerFunc will delete CLOSED connection
  shutdown(m_fd_event->getFd(), SHUT_RDWR);

}

TcpBuffer* TcpConnection::getInBuffer() {
  return m_read_buffer.get();
}

TcpBuffer* TcpConnection::getOutBuffer() {
  return m_write_buffer.get();
}

bool TcpConnection::getResPackageData(const std::string& msg_req, TinyPbStruct::pb_sptr& pb_struct) {
  auto it = m_reply_datas.find(msg_req);
  if (it != m_reply_datas.end()) {
    RpcDebugLog << "return a resdata";
    pb_struct = it->second;
    m_reply_datas.erase(it);
    return true;
  }
  RpcDebugLog << msg_req << "|reply data not exist";
  return false;

}


AbstractCodeC::sptr TcpConnection::getCodec() const {
  return m_codec;
}

TcpConnectionState TcpConnection::getState() {
  TcpConnectionState state;
  {
  RWMutex::ReadLock lock(m_mutex);
  state = m_state;
  }

  return state;
}

void TcpConnection::setState(const TcpConnectionState& state) {
  RWMutex::WriteLock lock(m_mutex);
  m_state = state;
}

void TcpConnection::setOverTimeFlag(bool value) {
  if (value) {
    std::cout << "conn" << m_fd << "  is over time" << "\n";
  }
  m_is_over_time = value;
}

bool TcpConnection::getOverTimerFlag() {
  return m_is_over_time;
}

Coroutine::sptr TcpConnection::getCoroutine() {
  return m_loop_cor;
}


void TcpConnection::resetServerCloseConnTime() {
  int64_t tmp = details::getNowMs() + m_tcp_svr->getConnectAliveTime();
  serverCloseConnTime_ = tmp;
}



}
