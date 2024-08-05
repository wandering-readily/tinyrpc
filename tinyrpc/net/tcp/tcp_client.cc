#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/tcp_client.h" 
#include "tinyrpc/comm/error_code.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/http/http_codec.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"

#include "tinyrpc/net/tcp/light_timer.h"
#include "tinyrpc/net/tcp/socket.h"
// #include <chrono>

namespace tinyrpc {

TcpClient::TcpClient(NetAddress::sptr addr, ProtocalType type) \
    : m_peer_addr(addr), fdEventPool_(std::make_shared<FdEventContainer>(1000)) {

  m_family = m_peer_addr->getFamily();
  // m_fd = socket(AF_INET, SOCK_STREAM, 0);
  m_fd = createNonblockingOrDie(m_family);

  RpcDebugLog << "TcpClient() create fd = " << m_fd;
  m_local_addr = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);

  if (isServerConn_) {
    m_reactor = Reactor::GetReactor();
  }

  // 实验功能5: 这里应该做成多态功能
  if (type == ProtocalType::Http_Protocal) {
		m_codec = std::make_shared<HttpCodeC>();
	} else {
		m_codec = std::make_shared<TinyPbCodeC>();
	}

  // 根据本地FD，创建连接
  // std::cout << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) <<  " make " << m_fd << " client conn" << "\n\n";
  m_connection = std::make_shared<TcpConnection>(this->m_codec, m_reactor,
    m_fd, 128, m_peer_addr, m_local_addr, fdEventPool_);

  lightTimerPool_ = std::make_shared<LightTimerPool> ();
}

TcpClient::~TcpClient() {
  if (m_fd > 0) {
    fdEventPool_->getFdEvent(m_fd)->unregisterFromReactor(); 
    close(m_fd);
    m_fd = -1;
    RpcDebugLog << "~TcpClient() close fd = " << m_fd;
  }
}

TcpConnection* TcpClient::getConnection() {
  if (!m_connection.get()) {
    // std::cout << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) <<  " make " << m_fd << " client conn" << "\n\n";
    m_connection = std::make_shared<TcpConnection>(this->m_codec, m_reactor,
      m_fd, 128, m_peer_addr, m_local_addr, fdEventPool_);
  }
  return m_connection.get();
}
void TcpClient::resetFd() {
  tinyrpc::FdEvent::sptr fd_event = fdEventPool_->getFdEvent(m_fd);
  fd_event->unregisterFromReactor();
  close(m_fd);
  // m_fd = socket(AF_INET, SOCK_STREAM, 0);
  m_fd = createNonblockingOrDie(m_family);
}

int TcpClient::sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_sptr& res) {
  // 设置定时timeout
  bool is_timeout = false;
  // tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
  // 1. 
  // timercb将在调用时恢复当前环境
  // 此时timeout，设置m_is_over_time=true
  // 这样就会打断client等待服务器回复的循环
  // auto timer_cb = [this, &is_timeout, cur_cor]() {
    // RpcInfoLog << "TcpClient timer out event occur";
    // // is_timeout设置，且m_connection也设置超时
    // is_timeout = true;
    // this->m_connection->setOverTimeFlag(true); 
    // tinyrpc::Coroutine::Resume(cur_cor);
  // };

  auto timer_cb = [this, &is_timeout]() {
    RpcInfoLog << "TcpClient timer out event occur";
    // is_timeout设置，且m_connection也设置超时
    is_timeout = true;
    this->m_connection->setOverTimeFlag(true); 
  };

  // 将超时恢复环境任务存放在m_reactor->getTimer()
  // ???
  // 是否应该增加超时重连机制?
  // TimerEvent::sptr event = std::make_shared<TimerEvent>(m_max_timeout, false, timer_cb);
  // m_reactor->getTimer()->addTimerEvent(event);

  // 实验功能6: 改善加入一个轻量级定时器
  auto timer = std::make_shared<LightTimer> (m_max_timeout, timer_cb, lightTimerPool_);
  timer->registerInLoop();

  // RpcDebugLog << "add rpc timer event, timeout on " << event->m_arrive_time;

  // bool connFuncInSys = false;
  // if (tinyrpc::Coroutine::IsMainCoroutine()) {
    // connFuncInSys = true;
  // }

  while (!is_timeout) {
    RpcDebugLog << "begin to connect";
    if (m_connection->getState() != Connected) {
      // client connect连接服务器
      int rt = connect_hook(fdEventPool_->getFdEvent(m_fd), reinterpret_cast<sockaddr*>(m_peer_addr->getSockAddr()), m_peer_addr->getSockLen());
      int savedErrno = (rt == 0) ? 0 : (m_connection->isServerConn()? rt : errno);

      switch (savedErrno) {
        case 0:
        case EINPROGRESS:
        case EINTR:
        case EISCONN:
        {
          RpcDebugLog << "connect [" << m_peer_addr->toString() << "] succ!";
          // 设置已连接
          m_connection->setUpClient();
          break;
        }

        case EAGAIN:
        case EADDRINUSE:
        case EADDRNOTAVAIL:
        case ECONNREFUSED:
        case ENETUNREACH:
        {
          // 重连
          resetFd();
          if (is_timeout) {
            // m_connection超时后仍未连接上
            RpcInfoLog << "connect timeout, break";
            goto err_deal;
          }
          break;
        }

        case EACCES:
        case EPERM:
        case EAFNOSUPPORT:
        case EALREADY:
        case EBADF:
        case EFAULT:
        case ENOTSOCK:
        default:
        {
          std::stringstream ss;
          ss << "connect cur sys ror, errinfo is " << std::string(strerror(errno)) <<  " ] closed.";
          m_err_info = ss.str();
          RpcErrorLog << "cancle overtime event, err info=" << m_err_info;
          return ERROR_FAILED_CONNECT;
        }
      }

    } else {
      break;
    }
  }    

  if (m_connection->getState() != Connected) {
    std::stringstream ss;
    ss << "connect peer addr[" << m_peer_addr->toString() << "] error. sys error=" << strerror(errno);
    m_err_info = ss.str();
    // m_reactor->getTimer()->delTimerEvent(event);
    return ERROR_FAILED_CONNECT;
  }

  m_connection->setUpClient();
  // 写输出事件
  // 把protobuf格式的request任务发送给服务器
  m_connection->output();
  if (m_connection->getOverTimerFlag()) {
    RpcInfoLog << "send data over time";
    is_timeout = true;
    goto err_deal;
  }

  // 如果没收到msg_no对应的服务器回复，那么一直等待
  while (!m_connection->getResPackageData(msg_no, res)) {
    RpcDebugLog << "redo getResPackageData";
    m_connection->input();

    // 如果接收服务器回复超时的话，那么进入错误处理
    if (m_connection->getOverTimerFlag()) {
      RpcInfoLog << "read data over time";
      is_timeout = true;
      goto err_deal;
    }
    if (m_connection->getState() == Closed) {
      RpcInfoLog << "peer close";
      goto err_deal;
    }

    m_connection->execute();
  }

  // m_reactor->getTimer()->delTimerEvent(event);
  m_err_info = "";
  return 0;

err_deal:
  // connect error should close fd and reopen new one
  fdEventPool_->getFdEvent(m_fd)->unregisterFromReactor();
  close(m_fd);
  // m_fd = socket(AF_INET, SOCK_STREAM, 0);
  m_fd = createNonblockingOrDie(m_family);
  std::stringstream ss;
  if (is_timeout) {
    ss << "call rpc falied, over " << m_max_timeout << " ms";
    m_err_info = ss.str();

    m_connection->setOverTimeFlag(false);
    return ERROR_RPC_CALL_TIMEOUT;
  } else {
    ss << "call rpc falied, peer closed [" << m_peer_addr->toString() << "]";
    m_err_info = ss.str();
    return ERROR_PEER_CLOSED;
  }

}

void TcpClient::stop() {
  if (!m_is_stop) {
    m_is_stop = true;
    if (m_reactor) {
      m_reactor->stop();
    }
  }
}

} // namespace name
