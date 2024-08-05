#include <sys/socket.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/tcp/tcp_connection.h"
#include "tinyrpc/net/tcp/io_thread.h"
#include "tinyrpc/net/tcp/tcp_connection_time_wheel.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/net/http/http_codec.h"
#include "tinyrpc/net/http/http_dispatcher.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_dispatcher.h"

#include "tinyrpc/net/tcp/socket.h"


namespace tinyrpc {


TcpAcceptor::TcpAcceptor(NetAddress::sptr net_addr, FdEventContainer::wptr fdEventPool) \
		: m_local_addr(net_addr), weakFdEventPool_(fdEventPool) {
	m_family = m_local_addr->getFamily();
}

void TcpAcceptor::init() {
	m_fd = createNonblockingOrDie(m_local_addr->getFamily());
	// assert(m_fd != -1);
	RpcDebugLog << "create listenfd succ, listenfd=" << m_fd;

	// int flag = fcntl(m_fd, F_GETFL, 0);
	// int rt = fcntl(m_fd, F_SETFL, flag | O_NONBLOCK);
	
	// if (rt != 0) {
		// RpcErrorLog << "fcntl set nonblock error, errno=" << errno << ", error=" << strerror(errno);
	// }


	setReuseAddr(m_fd, true);
	setReusePort(m_fd, true);

	socklen_t len = m_local_addr->getSockLen();
	int rt = bind(m_fd, m_local_addr->getSockAddr(), len);
	if (rt != 0) {
		// RpcErrorLog << "start server error. bind error, errno=" << errno << ", error=" << strerror(errno);
		locateErrorExit
	}
  // assert(rt == 0);

	RpcDebugLog << "set REUSEADDR succ";
	rt = listen(m_fd, 10);
	if (rt != 0) {
		// RpcErrorLog << "start server error. listen error, fd= " << m_fd << ", errno=" << errno << ", error=" << strerror(errno);
		locateErrorExit
	}
  // assert(rt == 0);

}

TcpAcceptor::~TcpAcceptor() {
  tinyrpc::FdEventContainer::sptr fdEventPool = weakFdEventPool_.lock();
  assert(fdEventPool != nullptr && "fdEventPool had released");
  FdEvent::sptr fd_event = fdEventPool->getFdEvent(m_fd);
  fd_event->unregisterFromReactor();
	if (m_fd != -1) {
		close(m_fd);
	}
}

int TcpAcceptor::toAccept() {

	socklen_t len = 0;
	int rt = 0;

	if (m_family == AF_INET) {
		sockaddr_in cli_addr;
		memset(&cli_addr, 0, sizeof(cli_addr));
		len = sizeof(cli_addr);
		// call hook accept

		tinyrpc::FdEventContainer::sptr fdEventPool = weakFdEventPool_.lock();
		assert(fdEventPool != nullptr && "fdEventPool had released");
		rt = accept_hook(fdEventPool->getFdEvent(m_fd), reinterpret_cast<sockaddr *>(&cli_addr), &len);
		int savedErrno = errno;

		if (rt == -1) {
			RpcDebugLog << "error, no new client coming, errno=" << errno << "error=" << strerror(errno);
			switch (savedErrno)
			{
			case EAGAIN:
			case ECONNABORTED:
			case EINTR:
			case EPROTO: // ???
			case EPERM:
			case EMFILE: // per-process lmit of open file desctiptor ???
				// expected errors
				errno = savedErrno;
				break;

			case EBADF:
			case EFAULT:
			case EINVAL:
			case ENFILE:
			case ENOBUFS:
			case ENOMEM:
			case ENOTSOCK:
			case EOPNOTSUPP:
			default:
				// RpcErrorLog << "unknown error of ::accept " << savedErrno;
				locateErrorExit
				break;
			}

			return -1;
		}
		RpcInfoLog << "New client accepted succ! port:[" << cli_addr.sin_port;
		m_peer_addr = std::make_shared<IPAddress>(cli_addr);

	} else if (m_family == AF_UNIX) {
		sockaddr_un cli_addr;
		len = sizeof(cli_addr);
		memset(&cli_addr, 0, sizeof(cli_addr));
		// call hook accept
		tinyrpc::FdEventContainer::sptr fdEventPool = weakFdEventPool_.lock();
		assert(fdEventPool != nullptr && "fdEventPool had released");
		rt = accept_hook(fdEventPool->getFdEvent(m_fd), reinterpret_cast<sockaddr *>(&cli_addr), &len);

		int savedErrno = errno;

		if (rt == -1) {
			RpcDebugLog << "error, no new client coming, errno=" << errno << "error=" << strerror(errno);
			switch (savedErrno)
			{
			case EAGAIN:
			case ECONNABORTED:
			case EINTR:
			case EPROTO: // ???
			case EPERM:
			case EMFILE: // per-process lmit of open file desctiptor ???
				// expected errors
				errno = savedErrno;
				break;

			case EBADF:
			case EFAULT:
			case EINVAL:
			case ENFILE:
			case ENOBUFS:
			case ENOMEM:
			case ENOTSOCK:
			case EOPNOTSUPP:
			default:
				locateErrorExit
			}

			return -1;
		}
		
		m_peer_addr = std::make_shared<UnixDomainAddress>(cli_addr);

	} else {
		RpcErrorLog << "unknown type protocol!";
		close(rt);
		return -1;
	}

	RpcInfoLog << "New client accepted succ! fd:[" << rt <<  ", addr:[" << m_peer_addr->toString() << "]";
	return rt;	
}


// TcpServer::TcpServer(NetAddress::sptr addr, ProtocalType type /*= TinyPb_Protocal*/) : m_addr(addr) {
TcpServer::TcpServer(Config *config, CoroutinePool::wptr corPool, \
		FdEventContainer::wptr fdEventPool, \
		CoroutineTaskQueue::wptr corTaskQueue)
	 	: m_addr(config->addr), \
		weakCorPool_(corPool), \
		weakFdEventPool_(fdEventPool) {

	m_io_pool = std::make_shared<IOThreadPool>(config->m_iothread_num, weakCorPool_);
	m_io_pool->beginThreadPool(corTaskQueue);
	m_protocal_type = config->protocal;
	if (config->protocal == ProtocalType::Http_Protocal) {
		m_dispatcher = std::make_shared<HttpDispacther>();
		m_codec = std::make_shared<HttpCodeC>();
	} else {
		m_dispatcher = std::make_shared<TinyPbRpcDispacther>();
		m_codec = std::make_shared<TinyPbCodeC>();
	}

	// m_main_reactor这样设立，当前进程的主线程是没有t_reacrtor_ptr这个指针的
	m_main_reactor = tinyrpc::Reactor::GetReactor();
	m_main_reactor->setReactorType(ReactorType::MainReactor);

	// 时间轮存放定时事件
	m_time_wheel = std::make_shared<TcpTimeWheel>(m_main_reactor, config->m_timewheel_bucket_num, config->m_timewheel_inteval);

	// connect存活的毫秒计数
	// 也可以自己设置
	connectAliveTime_ = config->m_timewheel_bucket_num * config->m_timewheel_inteval * 1000;

	// 绑定删除clients过时连接
	// 也就是每10s触发一次ClearClientTimerFunc
	m_clear_clent_timer_event = std::make_shared<TimerEvent>(10000, true, std::bind(&TcpServer::ClearClientTimerFunc, this));
	m_main_reactor->getTimer()->addTimerEvent(m_clear_clent_timer_event);

	RpcInfoLog << "TcpServer setup on [" << m_addr->toString() << "]";
}

void TcpServer::start() {

	m_acceptor.reset(new TcpAcceptor(m_addr, weakFdEventPool_));
  m_acceptor->init();
	// 得到当前coroutine，并且设置回调函数
	tinyrpc::CoroutinePool::sptr corPool = weakCorPool_.lock();
	assert(corPool != nullptr && "corPool had released");
	m_accept_cor = corPool->getCoroutineInstanse();
	// 先进一次MainAcceptCorFunc
	m_accept_cor->setCallBack(std::bind(&TcpServer::MainAcceptCorFunc, this));

	RpcInfoLog << "resume accept coroutine";
	tinyrpc::Coroutine::Resume(m_accept_cor.get());

  m_io_pool->start();
	m_main_reactor->loop();

}

// 由于TcpServer自己抽取了一个m_accept_cor，所以需要释放
TcpServer::~TcpServer() {
	// 还回m_acceptor_coroutine
	tinyrpc::CoroutinePool::sptr corPool = weakCorPool_.lock();
	assert(corPool != nullptr && "corPool had released");
	if (!corPool) [[unlikely]]
	{
		RpcErrorLog << "CoroutinePool down before ~TcpServer, and m_accept_cor " \
			<< m_accept_cor << " runs";
		return;
	}
	corPool->returnCoroutine(m_accept_cor);
  RpcDebugLog << "~TcpServer";
}


void TcpServer::MainAcceptCorFunc() {

  while (!m_is_stop_accept) {

	// 接收连接
    int fd = m_acceptor->toAccept();
    if (fd == -1) {
      RpcErrorLog << "accept ret -1 error, return, to yield";
      Coroutine::Yield();
      continue;
    }
	// !!!
	// 重点阅读DEBUG
    IOThread *io_thread = m_io_pool->getIOThread();
		// 重置IOThreadPool的IOThread，生成新的TcpConnection
		// m_clients持有conn的shared_ptr指针
		// !!!
		// 重点阅读DEBUG
		// 添加TcpConnection
		TcpConnection::sptr conn = addClient(io_thread, fd);
		// !!!
		// 重点阅读DEBUG
		// 服务端TcpConnection循环协程钩子设置
		conn->initServer();
		RpcDebugLog << "tcpconnection address is " << conn.get() << ", and fd is" << fd;

    // auto cb = [io_thread, conn]() mutable {
    //   io_thread->addClient(conn.get());
		// 	conn.reset();
    // };

	// !!!
	// 重点阅读DEBUG
	// 对应线程添加协程执行命令
    io_thread->getReactor()->addCoroutine(conn->getCoroutine());
    m_tcp_counts++;
    RpcDebugLog << "current tcp connection count is [" << m_tcp_counts << "]";
  }
}


void TcpServer::addCoroutine(Coroutine::sptr cor) {
	m_main_reactor->addCoroutine(cor);
}

// 注册service
bool TcpServer::registerService(std::shared_ptr<google::protobuf::Service> service) {
	if (m_protocal_type == tinyrpc::ProtocalType::TinyPb_Protocal) {
		if (service) {
			dynamic_cast<TinyPbRpcDispacther*>(m_dispatcher.get())->registerService(service);
		} else {
			RpcErrorLog << "register service error, service ptr is nullptr";
			return false;
		}
	} else {
		RpcErrorLog << "register service error. Just TinyPB protocal server need to resgister Service";
		return false;
	}
	return true;
}

// 注册HTTP服务
bool TcpServer::registerHttpServlet(const std::string& url_path, HttpServlet::sptr servlet) {
	if (m_protocal_type == ProtocalType::Http_Protocal) {
		if (servlet) {
			dynamic_cast<HttpDispacther*>(m_dispatcher.get())->registerServlet(url_path, servlet);
		} else {
			RpcErrorLog << "register http servlet error, servlet ptr is nullptr";
			return false;
		}
	} else {
		RpcErrorLog << "register http servlet error. Just Http protocal server need to resgister HttpServlet";
		return false;
	}
	return true;
}


TcpConnection::sptr TcpServer::addClient(IOThread* io_thread, int fd) {
  auto it = m_clients.find(fd);
  if (it != m_clients.end()) {
		it->second.reset();
    // set new Tcpconnection	
		RpcDebugLog << "fd " << fd << "have exist, reset it";
		// 由于ClearClientTimerFunc()函数删除了shared_ptr指针
		// 但是m_clients (fd, shared_ptr<TcpConnecytion>(此时是nullptr))仍在
		// 因此在这里重建
		// std::cout << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) <<  " make " << fd << " server conn" << "\n\n";
		it->second = std::make_shared<TcpConnection>(this, io_thread, \
			fd, 128, getPeerAddr(), weakCorPool_, weakFdEventPool_);
		return it->second;

  } else {
		RpcDebugLog << "fd " << fd << "did't exist, new it";
		// std::cout << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) <<  " make " << fd << " server conn" << "\n\n";
    TcpConnection::sptr conn = std::make_shared<TcpConnection>(this, io_thread, \
		fd, 128, getPeerAddr(), weakCorPool_, weakFdEventPool_); 
    m_clients.insert(std::make_pair(fd, conn));
		return conn;
  }
}



// 重新添加slot任务
void TcpServer::freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::sptr slot) {
	auto cb = [slot, this]() mutable {
		this->getTimeWheel()->fresh(slot);	
		// 减少slot的增持方，这段task代码执行后剩下timewheel拥有原内容
		slot.reset();
	};
	m_main_reactor->addTask(cb);
}


void TcpServer::ClearClientTimerFunc() {
  // RpcDebugLog << "this IOThread loop timer excute";
  
  // delete Closed TcpConnection per loop
  // for free memory
	// RpcDebugLog << "m_clients.size=" << m_clients.size();

  // 在定时器中如果有连接超过了预定时间
  // 那么就会减少TcpConnection的shared_ptr指针，促进释放TcpConnection
  // 也会释放coroutine
  for (auto &i : m_clients) {
	// debug connection
	// if (i.second && i.second.use_count() > 0) {
		// std::cout << "connection fd: " << i.first ;
		// if (i.second) {
			// std::cout << ", state " << i.second->getState() << "\n";		
		// } else {
			// std::cout << ", conn out" << "\n";			
		// }
	// }
		// TcpConnection::sptr s_conn = i.second;
			// RpcDebugLog << "state = " << s_conn->getState();
	// 需要等待当前连接设置为Closed状态后才能关闭TcpConnection
    if (i.second && i.second.use_count() > 0 && i.second->getState() == Closed) {
      // need to delete TcpConnection
      RpcDebugLog << "TcpConection [fd:" << i.first << "] will delete, state=" << i.second->getState();
      (i.second).reset();
      // s_conn.reset();
    }
  }
  // std::cout << "\n";
}

NetAddress::sptr TcpServer::getPeerAddr() {
	return m_acceptor->getPeerAddr();
}

NetAddress::sptr TcpServer::getLocalAddr() {
	return m_addr;
}

TcpTimeWheel::sptr TcpServer::getTimeWheel() {
  return m_time_wheel;
}

IOThreadPool::sptr TcpServer::getIOThreadPool() {
	return m_io_pool;
}


AbstractDispatcher::sptr TcpServer::getDispatcher() {	
	return m_dispatcher;	
}

AbstractCodeC::sptr TcpServer::getCodec() {
	return m_codec;
}

}
