#include <sys/socket.h>
#include <arpa/inet.h>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/net/net_address.h"
#include "tinyrpc/net/tcp/rpc_client.h" 
#include "tinyrpc/comm/error_code.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/http/http_codec.h"
#include "tinyrpc/net/tinypb/tinypb_codec.h"

#include "tinyrpc/net/tcp/light_timer.h"
#include "tinyrpc/net/tcp/socket.h"

namespace tinyrpc {

// 构建方式和构建connection一样
RpcClient::RpcClient(int fmaily, const std::string &conn_flag, \
    NetAddress::sptr peer_addr, \
    FdEventContainer::wptr fdEventPool, \
    std::weak_ptr<LightTimerPool> lightTimerPool, \
    std::shared_ptr<RpcClientGroups> rpcClientGroups) \
    : m_family(fmaily), conn_flag(conn_flag), \
    weakFdEventPool_(fdEventPool), \
    weakLightTimerPool_(lightTimerPool), \
    weakRpcClientGroups_(rpcClientGroups) {

  conn_ = rpcClientGroups->getConnection(conn_flag, peer_addr);
}

RpcClient::~RpcClient() {
  std::shared_ptr<RpcClientGroups> rpcClientGroups = \
      weakRpcClientGroups_.lock();
  assert(rpcClientGroups != nullptr);
  if (rpcClientGroups) {
    rpcClientGroups->delConnection(conn_flag);
  }
}


void RpcClient::resetFd() {
  this->conn_->m_fd_event->unregisterFromReactor();
  close(this->conn_->m_fd);
  this->conn_->m_fd = createNonblockingOrDie(m_family);
}

int RpcClient::sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_sptr& res) {
  
  bool is_timeout = false;
  auto conn = conn_;
  auto timer_cb = [&is_timeout, &conn]() {
    RpcInfoLog << "TcpClient timer out event occur";
    // is_timeout设置，且m_connection也设置超时
    is_timeout = true;
    conn->setOverTimeFlag(true); 
  };

  LightTimerPool::sptr lightTimerPool = weakLightTimerPool_.lock();
  assert(lightTimerPool != nullptr);
  auto timer = std::make_shared<LightTimer> (m_max_timeout, timer_cb, lightTimerPool);

  timer->registerInLoop();

  while (!is_timeout) {
    RpcDebugLog << "begin to connect";
    if (conn->getState() != Connected) {
      // client connect连接服务器
      tinyrpc::FdEventContainer::sptr fdEventPool = this->weakFdEventPool_.lock();
      assert(fdEventPool != nullptr);
      int rt = connect_hook(fdEventPool->getFdEvent(this->conn_->m_fd), \
          reinterpret_cast<sockaddr*>(this->conn_->m_peer_addr->getSockAddr()), \
          this->conn_->m_peer_addr->getSockLen());
      int savedErrno = (rt == 0) ? 0 : (this->conn_->isServerConn()? rt : errno);

      switch (savedErrno) {
        case 0:
        case EINPROGRESS:
        case EINTR:
        case EISCONN:
        {
        // 设置已连接
          this->conn_->setUpClient();
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
          return ERROR_FAILED_CONNECT;
        }
      }

    } else {
      break;
    }
  }    

  if (this->conn_->getState() != Connected) {
    std::stringstream ss;
    ss << "connect peer addr[" << this->conn_->m_peer_addr->toString() << "] error. sys error=" << strerror(errno);
    m_err_info = ss.str();
    return ERROR_FAILED_CONNECT;
  }

  this->conn_->setUpClient();
  // 写输出事件
  // 把protobuf格式的request任务发送给服务器
  this->conn_->output();
  if (this->conn_->getOverTimerFlag()) {
    is_timeout = true;
    goto err_deal;
  }

  // 如果没收到msg_no对应的服务器回复，那么一直等待
  while (!this->conn_->getResPackageData(msg_no, res)) {
    this->conn_->input();

    // 如果接收服务器回复超市的话，那么进入错误处理
    if (this->conn_->getOverTimerFlag()) {
      is_timeout = true;
      goto err_deal;
    }
    if (this->conn_->getState() == Closed) {
      goto err_deal;
    }

    this->conn_->execute();

  }

  m_err_info = "";
  return 0;

err_deal:
  // connect error should close fd and reopen new one
  tinyrpc::FdEventContainer::sptr fdEventPool = this->weakFdEventPool_.lock();
  assert(fdEventPool != nullptr);
  resetFd();
  std::stringstream ss;
  if (is_timeout) {
    ss << "call rpc falied, over " << m_max_timeout << " ms";
    m_err_info = ss.str();
    this->conn_->setOverTimeFlag(false);
    return ERROR_RPC_CALL_TIMEOUT;
  } else {
    ss << "call rpc falied, peer closed [" << this->conn_->m_peer_addr->toString() << "]";
    m_err_info = ss.str();
    return ERROR_PEER_CLOSED;
  }

}




RpcClientGroups::RpcClientGroups(ProtocalType type) {
  
  if (type == ProtocalType::Http_Protocal) {
    codec_ = std::make_shared<HttpCodeC>();
  } else {
    codec_ = std::make_shared<TinyPbCodeC>();
  }

  fdEventPool_ = std::make_shared<FdEventContainer>(1000);

  lightTimerPool_ = std::make_shared<LightTimerPool> ();
}


RpcClientGroups::~RpcClientGroups() {
  auto it = conns_.begin();
  while (it != conns_.end()) {
    int fd = it->second->getFd();
    if (fd > 0) [[likely]] {
      fdEventPool_->getFdEvent(fd)->unregisterFromReactor(); 
      close(fd);
    }
    ++it;
  }
}


TcpConnection::sptr RpcClientGroups::getConnection(const std::string &conn_flag,
      NetAddress::sptr peer_addr) {

    {
      RWMutex::ReadLock lock(conns_mutex_);
      if (conns_.find(conn_flag) != conns_.end()) {
        return conns_[conn_flag];
      }
    }

    int family = peer_addr->getFamily();
    // m_fd = socket(AF_INET, SOCK_STREAM, 0);
    int fd = createNonblockingOrDie(family);

    if (isServerConn_) {
      reactor_ = Reactor::GetReactor();
    }

    auto conn = std::make_shared<TcpConnection>(this->codec_, reactor_,
      fd, 128, std::move(peer_addr), local_addr_, fdEventPool_);
    {
      RWMutex::WriteLock lock(conns_mutex_);
      conns_[conn_flag] = conn;
    }

    return conn;
}

bool RpcClientGroups::delConnection(const std::string &conn_flag) {
  {
    RWMutex::ReadLock lock(conns_mutex_);
    if (conns_.find(conn_flag) != conns_.end()) {
      conns_.erase(conn_flag);
      return true;
    }
  }
  return false;
}


} // namespace name
