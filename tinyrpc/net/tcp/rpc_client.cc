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

namespace tinyrpc {

RpcClient::RpcClient(ProtocalType type) \
    : fdEventPool_(std::make_shared<FdEventContainer>(1000)) {

  local_addr_ = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 0);
  reactor_ = Reactor::GetReactor();

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
      fdEventPool_->getFdEvent(fd)->unregisterFromReactor(); 
      close(fd);
      RpcDebugLog << "~TcpClient() close fd = " << fd;
    }
    ++it;
  }
}


bool RpcClient::connect(NetAddress::ptr peer_addr) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    RpcErrorLog << "call socket error, fd=-1, sys error=" << strerror(errno);
    return false;
  }
  RpcDebugLog << "TcpClient() create fd = " << fd;

  auto conn = std::make_shared<TcpConnection>(this->codec_, reactor_, \
    fd, 128, peer_addr, fdEventPool_);
  if (!conn) {
    return false;
  }
  connections_[peer_addr->toString()] = conn;
  return true;
}

TcpConnection* RpcClient::getConnection(NetAddress::ptr peer_addr) {
  return getConnection(peer_addr->toString());
}

TcpConnection* RpcClient::getConnection(const std::string &peer_addr) {
  if (connections_.find(peer_addr) == connections_.end()) {
    return nullptr;
  }
  return connections_[peer_addr].get();
}

int RpcClient::resetFd(int fd) {
  tinyrpc::FdEvent::ptr fd_event = fdEventPool_->getFdEvent(fd);
  fd_event->unregisterFromReactor();
  close(fd);
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    RpcErrorLog << "call socket error, fd=-1, sys error=" << strerror(errno);
  }
  return fd;
}

int RpcClient::sendAndRecvTinyPb(NetAddress::ptr peer_addr, \
    const std::string& msg_no, TinyPbStruct::pb_ptr& res) {
 
  std::string addr = peer_addr->toString();
  auto it = connections_.find(addr);
  if (it == connections_.end()) {
    RpcErrorLog << "can't find conn" << peer_addr;
    getConnection(peer_addr);
  }
  auto conn = connections_[addr];
  int fd = conn->getFd();

  // 设置定时timeout
  bool is_timeout = false;
  tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();
  // 1. 
  // timercb将在调用时恢复当前环境
  // 此时timeout，设置m_is_over_time=true
  // 这样就会打断client等待服务器回复的循环
  auto timer_cb = [conn, &is_timeout, cur_cor]() {
    RpcInfoLog << "TcpClient timer out event occur";
    // is_timeout设置，且m_connection也设置超时
    is_timeout = true;
    conn->setOverTimeFlag(true); 
    tinyrpc::Coroutine::Resume(cur_cor);
  };
  // 将超时恢复环境任务存放在m_reactor->getTimer()
  // ???
  // 是否应该增加超时重连机制?
  TimerEvent::ptr event = std::make_shared<TimerEvent>(m_max_timeout, false, timer_cb);
  reactor_->getTimer()->addTimerEvent(event);

  RpcDebugLog << "add rpc timer event, timeout on " << event->m_arrive_time;

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
        // m_connection超时后仍未连接上
        RpcInfoLog << "connect timeout, break";
        goto err_deal;
      }
      if (errno == ECONNREFUSED) {
        // peer addr拒绝连接
        // 返回连接失败
        std::stringstream ss;
        ss << "connect error, peer[ " << peer_addr->toString() <<  " ] closed.";
        m_err_info = ss.str();
        RpcErrorLog << "cancle overtime event, err info=" << m_err_info;
        // 删除之前的定时事件
        reactor_->getTimer()->delTimerEvent(event);
        return ERROR_PEER_CLOSED;
      }
      if (errno == EAFNOSUPPORT) {
        // protocol family不支持
        std::stringstream ss;
        ss << "connect cur sys ror, errinfo is " << std::string(strerror(errno)) <<  " ] closed.";
        m_err_info = ss.str();
        RpcErrorLog << "cancle overtime event, err info=" << m_err_info;
        reactor_->getTimer()->delTimerEvent(event);
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
    reactor_->getTimer()->delTimerEvent(event);
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

  reactor_->getTimer()->delTimerEvent(event);
  m_err_info = "";
  return 0;

err_deal:
  // connect error should close fd and reopen new one
  fdEventPool_->getFdEvent(fd)->unregisterFromReactor();
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
