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
#include "netinet/tcp.h"

namespace tinyrpc {

// 构建方式和构建connection一样
RpcClient::RpcClient(NetAddress::sptr peer_addr, \
    std::shared_ptr<RpcClientGroups> rpcClientGroups) \
    : weakRpcClientGroups_(rpcClientGroups) {

  weakFdEventPool_ = rpcClientGroups->getFdEventPool();
  weakLightTimerPool_ = rpcClientGroups->getLightTimerPool();

  this->conn_ = rpcClientGroups->getConnection(peer_addr);

  this->conn_->m_reply_datas.clear();
  this->conn_->m_read_buffer->clearIndex();
  this->conn_->m_read_buffer->clearIndex();

}

RpcClient::~RpcClient() {
  std::shared_ptr<RpcClientGroups> rpcClientGroups = \
      weakRpcClientGroups_.lock();
  if (rpcClientGroups) [[likely]] {
    rpcClientGroups->delConnection(this->conn_);
  }
}


void RpcClient::resetFd() {
  // client connection确定不开启reactor
  // 而tcp_connection中包含了server 的同步和异步connection，因此开启reactor相关设置
  // this->conn_->m_fd_event->unregisterFromReactor();
  close(this->conn_->getFd());
  this->conn_->m_fd = createNonblockingOrDie(this->conn_->m_peer_addr->getFamily());
}

int RpcClient::sendAndRecvTinyPb(const std::string& msg_no, TinyPbStruct::pb_sptr& res) {

  updateTcpState();
  
  bool is_timeout = false;
  auto conn = this->conn_;
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
  // timer->cancelCB();
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

  // timer->cancelCB();
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

bool RpcClient::updateTcpState() {

  struct tcp_info info;
  int len = sizeof(info);
  getsockopt(this->conn_->m_fd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *)&len);

  if(info.tcpi_state != TCP_ESTABLISHED) {
    // printf("conn fd %d disconnect\n", this->conn_->getFd());
    // 避免第一次resetFd
    if (this->conn_->getState() != NotConnected && this->conn_->getState() != Closed) {
      // 从client的角度来看
      // 已经得到想要的数据包了，可以close fd
      printf("conn fd %d close\n", this->conn_->getFd());
      resetFd();
      this->conn_->setState(Closed);
    }
    return false;
  }
  return true;
}



RpcClientGroups::RpcClientGroups(int maxFreeConns, ProtocalType type) \
    : maxFreeConns_(maxFreeConns) {
  
  local_addr_ = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);
  
  if (type == ProtocalType::Http_Protocal) {
    codec_ = std::make_shared<HttpCodeC>();
  } else {
    codec_ = std::make_shared<TinyPbCodeC>();
  }

  fdEventPool_ = std::make_shared<FdEventContainer>(1000);

  lightTimerPool_ = std::make_shared<LightTimerPool> ();
}


RpcClientGroups::~RpcClientGroups() {
  for (auto &conn : workConns_) {
    int fd = conn->getFd();
    if (fd > 0) [[likely]] {
      // fdEventPool_->getFdEvent(fd)->unregisterFromReactor(); 
      close(fd);
    }
  }

  for (auto &[conn_flag, conns] : freeConns_) {
    for (auto &conn : conns->getConns()) {
      int fd = conn->getFd();
      if (fd > 0) [[likely]] {
        // fdEventPool_->getFdEvent(fd)->unregisterFromReactor(); 
        close(fd);
      }
    }
  }
  
}


TcpConnection::sptr RpcClientGroups::getConnection(NetAddress::sptr peer_addr) {

  const std::string conn_flag = peer_addr->toString();  
  TcpConnection::sptr rs;
    {
      bool hasFree = false;
      auto it = freeConns_.end();
      {
        Mutex::Lock lock(freeConns_mutex_);
        it = freeConns_.find(conn_flag);
        if (it != freeConns_.end()) {
          hasFree = true;
        }
      }

      if (hasFree) {
        // 一定存在
        Mutex &mutex = it->second->getMutex();
        Mutex::Lock lock(mutex);
        std::list<TcpConnection::sptr> &conns = it->second->getConns();
        if (!conns.empty()) {
          rs = conns.back();
          conns.pop_back();
        }
      }
    }

    if (rs) {
      {
        Mutex::Lock lock(workConns_mutex_);
        workConns_.insert(rs);
      }
      return rs;
    }

    int family = peer_addr->getFamily();
    // m_fd = socket(AF_INET, SOCK_STREAM, 0);
    // printf("make new connection\n");
    int fd = createNonblockingOrDie(family);
    // 这里不应该存在，TCP socket不应该关了又开
    // client应该重新启动addr
    // setReuseAddr(fd, true);

    if (isServerConn_) {
      reactor_ = Reactor::GetReactor();
    }

    auto conn = std::make_shared<TcpConnection>(this->codec_, reactor_,
      fd, 128, peer_addr, local_addr_, fdEventPool_);
    {
      Mutex::Lock lock(workConns_mutex_);
      workConns_.insert(conn);
    }

    return conn;
}

void RpcClientGroups::delConnection(TcpConnection::sptr conn) {

  auto conn_flag = conn->getPeerAddr()->toString();
  {
    Mutex::Lock lock(workConns_mutex_);
    auto it = workConns_.find(conn);
    assert(it != workConns_.end());
    workConns_.erase(it);
  }

  {
    {
      Mutex::Lock lock(freeConns_mutex_);
      auto it = freeConns_.find(conn_flag);
      if (it == freeConns_.end()) {
        freeConns_[conn_flag] = std::make_shared<freeConnBucket>();
      }
    }

    Mutex &mutex = freeConns_[conn_flag]->getMutex();
    {
      Mutex::Lock lock(mutex);
      std::list<TcpConnection::sptr> &list = freeConns_[conn_flag]->getConns();
      size_t deleteSize = list.size() / maxFreeConns_ + 1;
      while(deleteSize > 1) {
        close(list.front()->getFd());
        list.pop_front();
        deleteSize--;
      }
      list.push_back(conn);
    }
  }
}


} // namespace name
