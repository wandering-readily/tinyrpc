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

namespace tinyrpc {

RpcClient::RpcClient(ProtocalType type) \
    : fdEventPool_(std::make_shared<FdEventContainer>(1000)) {

  local_addr_ = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);

  if (type == ProtocalType::Http_Protocal) {
		codec_ = std::make_shared<HttpCodeC>();
	} else {
		codec_ = std::make_shared<TinyPbCodeC>();
	}

}

RpcClient::~RpcClient() {
  auto it = connections_.begin();
  while (it != connections_.end()) {
    int fd = it->second->getFd();
    if (fd > 0) {
      close(fd);
      RpcDebugLog << "~TcpClient() close fd = " << fd;
    }
    ++it;
  }
}


bool RpcClient::connect(NetAddress::sptr peer_addr) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    RpcErrorLog << "call socket error, fd=-1, sys error=" << strerror(errno);
    return false;
  }
  RpcDebugLog << "TcpClient() create fd = " << fd;

  auto conn = std::make_shared<TcpConnection>(this->codec_, nullptr, \
    fd, 128, peer_addr, fdEventPool_);
  if (!conn) {
    return false;
  }
  connections_[peer_addr->toString()] = conn;
  return true;
}

TcpConnection::sptr RpcClient::getConnection(NetAddress::sptr peer_addr) {
  const std::string addr = peer_addr->toString();
  if (connections_.find(addr) == connections_.end()) {
    return nullptr;
  }
  return connections_[addr];
}

bool RpcClient::delConnection(NetAddress::sptr peer_addr) {
  const std::string addr = peer_addr->toString();
  if (connections_.find(addr) == connections_.end()) {
    return false;
  }
  return connections_.erase(addr);
  return true;
}

int RpcClient::resetFd(int fd) {
  tinyrpc::FdEvent::sptr fd_event = fdEventPool_->getFdEvent(fd);
  close(fd);
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    RpcErrorLog << "call socket error, fd=-1, sys error=" << strerror(errno);
  }
  return fd;
}

int RpcClient::sendAndRecvTinyPb(NetAddress::sptr peer_addr, \
    const std::string& msg_no, TinyPbStruct::pb_sptr& res) {
  
  std::string addr = peer_addr->toString();
  auto it = connections_.find(addr);
  if (it == connections_.end()) {
    RpcInfoLog << "can't find conn" << peer_addr;
    getConnection(peer_addr);
  }
  tinyrpc::TcpConnection::sptr conn = connections_[addr];
  int fd = conn->getFd();
  
  bool is_timeout = false;
  auto timer_cb = [&is_timeout, &conn]() {
    RpcInfoLog << "TcpClient timer out event occur";
    // is_timeout设置，且m_connection也设置超时
    is_timeout = true;
    conn->setOverTimeFlag(true); 
  };

  auto timer = std::make_shared<LightTimer> (m_max_timeout, timer_cb, lightTimerPool_);
  timer->registerInLoop();

  while (!is_timeout) {
    RpcDebugLog << "begin to connect";
    if (conn->getState() != Connected) {
      // client connect连接服务器
      int rt = connect_hook(fdEventPool_->getFdEvent(fd), \
        reinterpret_cast<sockaddr*>(peer_addr->getSockAddr()), peer_addr->getSockLen());
      if (rt == 0) {
        RpcDebugLog << "connect [" << peer_addr->toString() << "] succ!";
        // 设置已连接
        conn->setUpClient();
        break;
      }

      // 重连失败后重置FD
      fd = resetFd(fd);
      conn->setFd(fd);

      if (is_timeout) {
        RpcInfoLog << "connect timeout, break";
        goto err_deal;
      }
      if (errno == ECONNREFUSED) {
        std::stringstream ss;
        ss << "connect error, peer[ " << peer_addr->toString() <<  " ] closed.";
        m_err_info = ss.str();
        RpcErrorLog << "cancle overtime event, err info=" << m_err_info;
        return ERROR_PEER_CLOSED;
      }
      if (errno == EAFNOSUPPORT) {
        // protocol family不支持
        std::stringstream ss;
        ss << "connect cur sys ror, errinfo is " << std::string(strerror(errno)) <<  " ] closed.";
        m_err_info = ss.str();
        RpcErrorLog << "cancle overtime event, err info=" << m_err_info;
        return ERROR_CONNECT_SYS_ERR;

      } 
    } else {
      break;
    }
  }    

  if (conn->getState() != Connected) {
    std::stringstream ss;
    ss << "connect peer addr[" << peer_addr->toString() << "] error. sys error=" << strerror(errno);
    m_err_info = ss.str();
    return ERROR_FAILED_CONNECT;
  }

  conn->setUpClient();
  // 写输出事件
  // 把protobuf格式的request任务发送给服务器
  conn->output();
  if (conn->getOverTimerFlag()) {
    RpcInfoLog << "send data over time";
    is_timeout = true;
    goto err_deal;
  }

  // 如果没收到msg_no对应的服务器回复，那么一直等待
  while (!conn->getResPackageData(msg_no, res)) {
    RpcDebugLog << "redo getResPackageData";
    conn->input();

    // 如果接收服务器回复超市的话，那么进入错误处理
    if (conn->getOverTimerFlag()) {
      RpcInfoLog << "read data over time";
      is_timeout = true;
      goto err_deal;
    }
    if (conn->getState() == Closed) {
      RpcInfoLog << "peer close";
      goto err_deal;
    }

    conn->execute();

  }

  m_err_info = "";
  return 0;

err_deal:
  // connect error should close fd and reopen new one
  close(fd);
  fd = socket(AF_INET, SOCK_STREAM, 0);
  std::stringstream ss;
  if (is_timeout) {
    ss << "call rpc falied, over " << m_max_timeout << " ms";
    m_err_info = ss.str();

    conn->setOverTimeFlag(false);
    return ERROR_RPC_CALL_TIMEOUT;
  } else {
    ss << "call rpc falied, peer closed [" << peer_addr->toString() << "]";
    m_err_info = ss.str();
    return ERROR_PEER_CLOSED;
  }

}


} // namespace name
