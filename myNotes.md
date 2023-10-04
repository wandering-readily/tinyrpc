### reading
- 启动
    - server启动
        - 初始化config  ==>     tinyrpc::InitConfig(argv[1]);
        - 注册服务      ==>     REGISTER_SERVICE(QueryServiceImpl);
        - 启动service   ==>     tinyrpc::StartRpcServer();


        - 保证多线程安全的static变量
            - 未修改
                - thread_local 已保证线程安全
                - 成本低，并保证只读的static变量，例如log.cc的static pid_t g_pid变量

            - 修改
                - 设置读取一次的static变量，修改操作加锁保证多线程安全，**但是初始化使用get___()函数**，这样存在多线程不安全危险，**因此在static变量初始化增加pthread_once约束，保证在多线程中只进行一次初始化**，**增加的pthread_once约束有msg_req.cc的once_msgReq，fd_event.cc的once_GFdContainer,reactor.cc的once_CoroutineTaskQueue**

            - 保证在启动时单线程初始化
                - gRpcConfig, gRpcLogger, gRpcServer变量
                - g_init_config, g_hook变量 在initConfig()函数中单独使用
                - t_coroutine_container_ptr 在 StartRpcServer() --> TcpServer::start() --> m_accept_cor = GetCoroutinePool()->getCoroutineInstanse(); 中的启动单线程调用保证t_coroutine_container_ptr创建构造时多线程安全

        - 根据test_tinypb_server.cc 和 test_tinypb_server_client.cc DEBUG可以得到整个脉络

--------------------------------------------------

- comm模块
    - config模块
        - 根据 <tinyxml/tinyxml.h> library提取conf/___.xml的设置

        - ```InitConfig() {...... if (g_init_config == 0) {gRpcConfig = std::make_shared<tinyrpc::Config>(file);gRpcConfig->readConf(); g_init_config = 1;} ......}```                      创建gRpcConfig

        - ```if (protocal == "HTTP") { gRpcServer = std::make_shared<TcpServer>(addr, Http_Protocal);} else {gRpcServer = std::make_shared<TcpServer>(addr, TinyPb_Protocal);}```       创建gRpcServer

        - TcpServer()的构造函数直接构造 m_dispatcher(访问路径转发器), m_codec(gprobuf的encode/decode), m_protocal_type(协议)， m_main_reactor(主线程，用于接受tcp连接(accept_hook和m_accept_cor彼此异步))，
            - m_time_wheel时间轮，往m_main_reactor->getTimer()也就是Timer定时器添加TcpTimeWheel::loopFunc()回调函数，loopFunc()在每个时间轮取出std::vector<TcpConnectionSlot::ptr>， 析构函数执行TcpConnectionSlot::ptr slot时间

            - m_clear_clent_timer_event将往m_main_reactor->getTimer()添加定时事件m_clear_clent_timer_event，执行TcpServer::ClearClientTimerFuc()函数，shared_ptr.reset()断开TcpConnection连接，

            - 而m_time_wheel的添加事件fresh()只在freshTcpConnection()函数中调用, TcpTimeWheel::TcpConnectionSlot::ptr将被填入freshTcpConnection()中，执行TcpConnection断开连接事件

            - 而只有registerToTimeWheel()调用freshTcpConnection()函数，```auto cb = [] (TcpConnection::ptr conn) {conn->shutdownConnection();}; TcpTimeWheel::TcpConnectionSlot::ptr tmp = std::make_shared<AbstractSlot<TcpConnection>>(shared_from_this(), cb); m_weak_slot = tmp;``` 函数不会将当前TcpConnection的shared_ptr计数增加，AbstractSlot<TcpConnection>类在构造时只保存weak_ptr<>

            - 也就是说m_time_wheel取出的回调函数执行时，会升级weak_ptr<TcpConnection>，如果已经在ClearClientTimerFuc()断开连接，那么什么也不会执行，否则将会执行断开连接操作

            - 注意到TcpConnection::input()函数在正确接受来自client的字节流时，```if (m_connection_type == ServerConnection) { TcpTimeWheel::TcpConnectionSlot::ptr tmp = m_weak_slot.lock(); if (tmp) { m_tcp_svr->freshTcpConnection(tmp); }}```，这段代码可能没什么意义，因为在initServer()  --> registerToTimeWheel()  --> ```m_weak_slot = tmp; m_tcp_svr->freshTcpConnection(tmp);``` 中已经注册了m_weak_ptr的断开回调函数

    - log模块
        - Logger缓冲设计，
        - AsyncLogger异步输入日志, 
        - LogEvent决定日志输出行格式，
        - LogTmp完成RAII时输出，以及输出流字符串获取

    - msg_req模块
        - 完成服务的msg_number的字符串数字设计
        - 初始值由```g_random_fd = open("/dev/urandom", O_RDONLY);```的g_random_fd随机得到，而后累加获取```static thread_local std::string t_msg_req_nu; static thread_local std::string t_max_msg_req_nu;```

    - mysql_instance模块
        - 完成一些mysql.h的函数封装

    - runtime.h
        - 当前服务的调用service.method name
        - 回应消息编号m_msg_no

    - start模块
        - 刚开始单线程启动需要调用的函数

    - string_util模块
        - 对url path的分割util函数

    - thread_pool模块
        - 线程池
        - 启发：需要增加start()函数，这里实际创建线程并执行，有利于线程之间的同步

--------------------------------------------------

- coroutine模块
    - coctx_swap.S, cotx.h模块
        - 保存当前重要寄存器值到每个协程当中，并恢复其它寄存器值
    - coroutine_hook.c模块
        - **调用的精髓，如果当前协程不是t_main_corutine，那么toEpoll将当前事件注册到当前线程的reactor，并关联当前协程，等待reactor事件唤醒，而后删除到当前reactor的注册，取消关联协程，并返回结果**
    - coroutine_pool.c模块
        - **协程池，源代码采取 线程M : 协程N 的对应模式，协程池提供了获得当前或额外的协程函数(其中设有memory ==> stack内存分配) Coroutine::ptr CoroutinePool::getCoroutineInstanse()，以及协程内容归还函数void CoroutinePool::returnCoroutine(Coroutine::ptr cor)**
    - memory模块
        - 分配和回收协程内存

--------------------------------------------------

- net模块
    - net公共模块
        - abstract_codec模块
            - 定义ProtocalType类型  ```enum ProtocalType { TinyPb_Protocal = 1, Http_Protocal = 2 };```
            - AbstractCodeC 虚基类  ``` virtual void encode(TcpBuffer* buf, AbstractData* data) = 0; virtual void decode(TcpBuffer* buf, AbstractData* data) = 0;```

        - abstract_data模块
            - AbstractData 虚基类 ```bool decode_succ {false}; bool encode_succ {false};``` 指明是否encode/decode成功

        - byte模块
            - 网络序int32转化为字节序

        - fdevent模块
            - 对文件描述符的包装，```m_fd, m_read_callback, m_write_callback, m_listen_events, m_reactor, m_coroutine``` 

            - m_listen_events是m_fd监听事件

            - fdevent类提供函数addListenEvents(), deleteListenEvents()函数，先修改m_listen_events，然后调用updateToReactor()更新状态到m_reactor

            - updateToReactor()函数设置```epoll_event event.data.ptr```设置为当前fdEvent指针，然后在m_reactor调用addEvent(),根据调用线程和m_reactor()所在线程不同做出不同举动，如果是相同线程，那么直接epoll_ctl，否则添加到m_pending_add_fds

            - unregisterFromReactor()函数将m_fd从m_reactor中删除，epoll_ctl删除m_fd事件，并且清除所有状态```m_listen_events = 0; m_read_callback = nullptr; m_write_callback = nullptr;```

            - setNonBlock(), isNonBlock()函数设置fd为非阻塞状态

            - 增加针对m_coroutine的系列函数，**有m_coroutine的fdEvent，那么可以分为server的主线程的m_accept_cor和其它IOThread的IOCouroutine，m_accept_cor特殊完成accept任务，其它IOThread完成IO任务**

        - mutex模块
            - RAII设置
        
        - net_address模块
            - 网络IP PORT sock_addr等设置函数

        - timer模块
            - TimerEvent类，可以用来复用提醒时间

            - Timer类，继承自tinyrpc::FdEvent类，包装timer_fd，``` m_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);```创建事件，由于没有设置coroutine，那么将会read_cb()执行，调用Timer::onTimer()函数，将执行到期的task

            - 增加定时事件的函数addTimerEvent()，
                - log.cc将Logger::loopFunc()放进定时器，将Logger的缓冲换进AsyncLogger
                - tcp_connection_time_wheel.cc将timer_wheel中每个时间轮绑定timer
                - tcpServer定时清除连接 ```m_clear_clent_timer_event = std::make_shared<TimerEvent>(10000, true, std::bind(&TcpServer::ClearClientTimerFunc, this)); m_main_reactor->getTimer()->addTimerEvent(m_clear_clent_timer_event);```

        - reactor模块
            - 详见loop()函数注解
            - 得自己去阅读


    - http模块
        - class HttpCode
            - 分析decodeHTTP报文            HttpCodeC::decode()
                - parseHttpRequestLine
                - parseHttpRequestHeader
                - parseHttpRequestContent
            - encode编码回复                HttpCodeC::encode()

        - ```class HttpRequestHeader : public HttpHeaderComm ```, ```class HttpResponseHeader : public HttpHeaderComm```，解析request和response content内容

        - ```class HttpRequest : public AbstractData {```
            ``` HttpMethod m_request_method;   ```
            ``` std::string m_request_path;   ```
            ``` std::string m_request_query;   ```
            ``` std::string m_request_version;   ```
            ``` HttpRequestHeader m_requeset_header;```
            ``` std::string m_request_body;   ```
            ``` std::map<std::string, std::string> m_query_maps;```
            ```};```
        
        - ```class HttpResponse : public AbstractData {```
            ```std::string m_response_version;   ```
            ```int m_response_code;```
            ```std::string m_response_info;```
            ```HttpResponseHeader m_response_header;```
            ```std::string m_response_body;   ```
            ```};```

        - HttpServlet类 ```virtual void handle(HttpRequest* req, HttpResponse* res) = 0;```处理当前内容

    
    - tinypb模块
        - https://juejin.cn/post/6954883558347374622 查看相关内容

        - ```void TinyPbCodeC::encode(TcpBuffer* buf, AbstractData* data); void TinyPbCodeC::decode(TcpBuffer* buf, AbstractData* data); ``` 根据 ```class TinyPbStruct : public AbstractData``` 内容编码和解码要传递的内容

        - tcp_rpc_controller模块
            - 继承自google::protobuf::RpcController虚基类，承担CallMethod()的控制显示类功能
        
        - TinyPbRpcDispacther模块
            - ```std::map<std::string, service_ptr> m_service_map;```记录服务转发路径
            - ```void registerService(service_ptr service)``` 记录服务
            - ```REGISTER_HTTP_SERVLET REGISTER_SERVICE宏 调用不同的registerService()```
            - dispatch()函数将decode的内容解码decode，调用service，并将内容编码encode进写buffer

        - tinypb_rpc_channel模块
            - https://juejin.cn/post/6954883558347374622有rpc_channel的解释
            - 需要完成以下步骤
                - 将 const google::protobuf::Message* request 和其它内容编码
                - ```int rt = m_client->sendAndRecvTinyPb(pb_struct msg_req, res_data);``` 发送编码内容，并获得rpc server的回复并解码成TinyPbStruct格式
                - 将回复内容存入 google::protobuf::Message* response
            
        - tinypb_rpc_async_channel模块
            - 利用tinypb_rpc_channel模块内容完成操作
            - CallMethod()将tinypb_rpc_channel->CallMethod()任务放入任一线程coroutine当中，
            - ```GetServer()->getIOThreadPool()->addCoroutineToRandomThread(cb, false);```完成异步任务
            - 异步任务 还有回调函数 ``` s_ptr->getIOThread()->getReactor()->addTask(call_back, true);```
            - **wait()完成coroutine的异步等待**
            - **C++ lambda函数[]抓取shared_ptr增加计数时，应该在结束任务时调用reset()，减少计数(lambda增加了计数，但是作用域已经不清楚了，应该在lambda函数直接减少计数)**

    - tcp模块
        - ioThread ioThreadPool模块
            - ioThread ioThreadPool有sem_t内容之间的同步

            - IOThread::main是每个thread的执行函数，最后进入 ```t_reactor_ptr->loop();```，也就是IOThread线程的ractor的loop()

        - tcp_buffer模块
            - TcpBuffer保证buffer的动态扩容

        - ***tcp_client模块***
            - **阅读并DEBUG TcpClient::sendAndRecvTinyPb()函数便知道其中奥妙和复用函数**

        - tcp_connection_time_whell模块
            - 前文多有介绍，这里不重复

        - ***tcp_connection模块***
            - 存在server, client 服务器和调用段两种不同的启动方式， 
            - server
                - m_loop_cor是执行的coroutine
                - **重点阅读函数**
                    - TcpConnection::initServer()
                        - ```registerToTimeWheel(); m_loop_cor->setCallBack(std::bind(&TcpConnection::MainServerLoopCorFunc, this));```
                        - coroutine执行函数MainServerLoopCorFunc()
                            - ```while (!m_stop) { input();  execute(); output(); }```
                                - ```input()```
                                - ```execute()```
                                - ```output()```
                    - TcpConnection::setUpServer()
                        - ``` m_reactor->addCoroutine(m_loop_cor);```
            - client
                - ```TcpConnection::setUpClient()```
                    - ```setState(Connected);```

        - ***tcp_server模块****
            - m_accept_cor模块调用TcpServer::MainAcceptCorFunc()
            - m_acceptor异步accept ```int fd = m_acceptor->toAccept();```
                - ***重点阅读debug函数***
                - TcpAcceptor::::toAccept()函数调用accept_hook()
                - ```IOThread *io_thread = m_io_pool->getIOThread();```
                - ```TcpConnection::ptr conn = addClient(io_thread, fd);```
                - ```conn->initServer();```
                - ```io_thread->getReactor()->addCoroutine(conn->getCoroutine());```
                - ```m_tcp_counts++;```