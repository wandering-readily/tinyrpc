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

namespace tinyrpc {

TcpConnection::TcpConnection(tinyrpc::TcpServer* tcp_svr, tinyrpc::IOThread* io_thread, int fd, int buff_size, NetAddress::ptr peer_addr)
  : m_io_thread(io_thread), m_fd(fd), m_state(Connected), m_connection_type(ServerConnection), m_peer_addr(peer_addr) {	
  m_reactor = m_io_thread->getReactor();

  // DebugLog << "m_state=[" << m_state << "], =" << fd;
  m_tcp_svr = tcp_svr;

  m_codec = m_tcp_svr->getCodec();
  m_fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
  m_fd_event->setReactor(m_reactor);
  initBuffer(buff_size); 
  // IO线程自己在创建的时候已经建立
  //  void* IOThread::main(void* arg) 
  //    Coroutine* Coroutine::GetCurrentCoroutine()
  // 而要完成任务的协程从协程池中获取，这些协程都需要归还
  m_loop_cor = GetCoroutinePool()->getCoroutineInstanse();
  m_state = Connected;
  DebugLog << "succ create tcp connection[" << m_state << "], fd=" << fd;
}

TcpConnection::TcpConnection(tinyrpc::TcpClient* tcp_cli, tinyrpc::Reactor* reactor, int fd, int buff_size, NetAddress::ptr peer_addr)
  : m_fd(fd), m_state(NotConnected), m_connection_type(ClientConnection), m_peer_addr(peer_addr) {
  m_reactor = reactor;

  m_tcp_cli = tcp_cli;

  m_codec = m_tcp_cli->getCodeC();

  m_fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
  m_fd_event->setReactor(m_reactor);
  initBuffer(buff_size); 

  DebugLog << "succ create tcp connection[NotConnected]";

}

void TcpConnection::initServer() {
  registerToTimeWheel();
  m_loop_cor->setCallBack(std::bind(&TcpConnection::MainServerLoopCorFunc, this));
}
void TcpConnection::setUpServer() {
  m_reactor->addCoroutine(m_loop_cor);
}


void TcpConnection::registerToTimeWheel() {
  // cb回调函数不持有conn shared_ptr指针，因为参数传入
  auto cb = [] (TcpConnection::ptr conn) {
    conn->shutdownConnection();
  };
  // shared_from_this()传入shared_ptr指针
  // 但是 在AbstractSlot<TcpConnection>中将shared_ptr转化为weak_ptr
  // 因此仍不持有shared_ptr指针
  // 如果ConnectionPtr自己释放，那么将不会执行回调函数cb，也就不会shutdownConnection
  TcpTimeWheel::TcpConnectionSlot::ptr tmp = 
      std::make_shared<AbstractSlot<TcpConnection>>(shared_from_this(), cb);
  m_weak_slot = tmp;
  // 将回调函数放入timewheel
  // 服务器将调用shuown(SHUT_RDWR)，优雅地结束服务器的单方向连接
  // 查阅资料SHUT_RDWR会在tcp发送窗口缓存全部发送完毕后，发送FIN报文
  m_tcp_svr->freshTcpConnection(tmp);

}

void TcpConnection::setUpClient() {
  setState(Connected);
}

// 也就是TcpConnection结束，才释放coroutine
TcpConnection::~TcpConnection() {
  if (m_connection_type == ServerConnection) {
    GetCoroutinePool()->returnCoroutine(m_loop_cor);
  }

  DebugLog << "~TcpConnection, fd=" << m_fd;
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
  InfoLog << "this connection has already end loop";
}

void TcpConnection::input() {
  if (m_is_over_time) {
    InfoLog << "over timer, skip input progress";
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

    DebugLog << "m_read_buffer size=" << m_read_buffer->getBufferVector().size() << "rd=" << m_read_buffer->readIndex() << "wd=" << m_read_buffer->writeIndex();
    int rt = read_hook(m_fd, &(m_read_buffer->m_buffer[write_index]), read_count);
    if (rt > 0) {
      m_read_buffer->recycleWrite(rt);
    }
    DebugLog << "m_read_buffer size=" << m_read_buffer->getBufferVector().size() << "rd=" << m_read_buffer->readIndex() << "wd=" << m_read_buffer->writeIndex();

    DebugLog << "read data back, fd=" << m_fd;
    count += rt;
    if (m_is_over_time) {
      InfoLog << "over timer, now break read function";
      break;
    }
    if (rt <= 0) {
      DebugLog << "rt <= 0";
      ErrorLog << "read empty while occur read event, because of peer close, fd= " << m_fd << ", sys error=" << strerror(errno) << ", now to clear tcp connection";
      // this cor can destroy
      // 可能是
      //        客户端client自己关闭
      //        也可能是服务端server自己shutdown()
      close_flag = true;
      break;
    } else {
      if (rt == read_count) {
        DebugLog << "read_count == rt";
        // is is possible read more data, should continue read
        continue;
      } else if (rt < read_count) {
        DebugLog << "read_count > rt";
        // read all data in socket buffer, skip out loop
        read_all = true;
        break;
      }
    }
  }
  if (close_flag) {
    // 关闭服务器对客户的设置
    clearClient();
    DebugLog << "peer close, now yield current coroutine, wait main thread clear this TcpConnection";
    Coroutine::GetCurrentCoroutine()->setCanResume(false);
    Coroutine::Yield();
    // return;
  }

  if (m_is_over_time) {
    return;
  }

  if (!read_all) {
    ErrorLog << "not read all data in socket buffer";
  }
  InfoLog << "recv [" << count << "] bytes data from [" << m_peer_addr->toString() << "], fd [" << m_fd << "]";
  if (m_connection_type == ServerConnection) {
    TcpTimeWheel::TcpConnectionSlot::ptr tmp = m_weak_slot.lock();
    if (tmp) {
      m_tcp_svr->freshTcpConnection(tmp);
    }
  }

}

void TcpConnection::execute() {
  // DebugLog << "begin to do execute";

  // it only server do this
  while(m_read_buffer->readAble() > 0) {
    std::shared_ptr<AbstractData> data;
    if (m_codec->getProtocalType() == TinyPb_Protocal) {
      data = std::make_shared<TinyPbStruct>();
    } else {
      data = std::make_shared<HttpRequest>();
    }

    m_codec->decode(m_read_buffer.get(), data.get());
    // DebugLog << "parse service_name=" << pb_struct.service_full_name;
    if (!data->decode_succ) {
      ErrorLog << "it parse request error of fd " << m_fd;
      break;
    }
    // DebugLog << "it parse request success";
    if (m_connection_type == ServerConnection) {
      // DebugLog << "to dispatch this package";
      m_tcp_svr->getDispatcher()->dispatch(data.get(), this);
      // DebugLog << "contine parse next package";
    } else if (m_connection_type == ClientConnection) {
      // TODO:
      std::shared_ptr<TinyPbStruct> tmp = std::dynamic_pointer_cast<TinyPbStruct>(data);
      if (tmp) {
        m_reply_datas.insert(std::make_pair(tmp->msg_req, tmp));
      }
    }

  }

}

void TcpConnection::output() {
  if (m_is_over_time) {
    InfoLog << "over timer, skip output progress";
    return;
  }
  while(true) {
    TcpConnectionState state = getState();
    if (state != Connected) {
      break;
    }

    if (m_write_buffer->readAble() == 0) {
      DebugLog << "app buffer of fd[" << m_fd << "] no data to write, to yiled this coroutine";
      break;
    }
    
    int total_size = m_write_buffer->readAble();
    int read_index = m_write_buffer->readIndex();
    int rt = write_hook(m_fd, &(m_write_buffer->m_buffer[read_index]), total_size);
    // InfoLog << "write end";
    if (rt <= 0) {
      ErrorLog << "write empty, error=" << strerror(errno);
    }

    DebugLog << "succ write " << rt << " bytes";
    m_write_buffer->recycleRead(rt);
    DebugLog << "recycle write index =" << m_write_buffer->writeIndex() << ", read_index =" << m_write_buffer->readIndex() << "readable = " << m_write_buffer->readAble();
    InfoLog << "send[" << rt << "] bytes data to [" << m_peer_addr->toString() << "], fd [" << m_fd << "]";
    if (m_write_buffer->readAble() <= 0) {
      // InfoLog << "send all data, now unregister write event on reactor and yield Coroutine";
      InfoLog << "send all data, now unregister write event and break";
      // m_fd_event->delListenEvents(IOEvent::WRITE);
      break;
    }

    if (m_is_over_time) {
      InfoLog << "over timer, now break write function";
      break;
    }

  }
}


void TcpConnection::clearClient() {
  // 关闭连接，那么在清除连接端函数TcpServer::ClearClientTimerFunc()
  // 知晓设置CLOSED状态后，减少shared_ptr<TcpConnection>的计数
  // 从而关闭TcpConnection，换回coroutine
  if (getState() == Closed) {
    DebugLog << "this client has closed";
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
    DebugLog << "this client has closed";
    return;
  }
  setState(HalfClosing);
  InfoLog << "shutdown conn[" << m_peer_addr->toString() << "], fd=" << m_fd;
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

bool TcpConnection::getResPackageData(const std::string& msg_req, TinyPbStruct::pb_ptr& pb_struct) {
  auto it = m_reply_datas.find(msg_req);
  if (it != m_reply_datas.end()) {
    DebugLog << "return a resdata";
    pb_struct = it->second;
    m_reply_datas.erase(it);
    return true;
  }
  DebugLog << msg_req << "|reply data not exist";
  return false;

}


AbstractCodeC::ptr TcpConnection::getCodec() const {
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
  m_is_over_time = value;
}

bool TcpConnection::getOverTimerFlag() {
  return m_is_over_time;
}

Coroutine::ptr TcpConnection::getCoroutine() {
  return m_loop_cor;
}





}
