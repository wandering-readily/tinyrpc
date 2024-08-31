### Before
    感谢作者开源这个多线程多epoll网络框架，能够学习到64位Linux环境下的有栈协程设置，我从这个项目收益良多，当然根据我自己的想法也加入了部分功能，下面是具体工作：

### 已完成
> - 从muduo中学习添加errno处理，尤其在_hook系列函数和TcpCoonection中

> - 添加异步asyncChannel

> - 添加keepAlive机制

> - 添加template client模板(SIFNAE)，避免不合理调用

> - 同步client callByAddr callByID(更加节省开销) 很好地节省了开销

> - 添加轻型timer, TimerPool

> - 原实现定时删除connection，只有一次服务，无论是client还是server，增加同步调用的tcp复用，异步调用使用tcp复用则会出错

### 待完成
- [half] .h头文件链接了过多头文件,，且::sptr, ::wptr, ::pptr(plain pointer)混杂

- [half] 检查是否有资源泄露

- [x] 粘包问题 TcpConnction.cc --> void TcpConnection::input() --> line240
如果rt == read_count 且 package全部传入完;  这会让read_hook会在EPOLL_WAIT等待;
在tinypb_codec方法 可以分析packageLen，那么很好避免这一点;
而HTTP方法 header['Content-Type']不好分析packageLen
**已经解决部分，read_hook()填入一个参数表示这次read_hook是不是持续read的第二次，如果是的话，且g_sys_read返回值为-1，errno=EAGAIN那么就要返回；还是有问题存在!!!，见原文**


- [ ] 增加注册服务中心，转发节点（HTTP实现）仿照https://github.com/wandering-readily/7days-golang.git 的gee-cache, gee-rpc设计

- [ ] 增加一个XClient, 复用TCP连接，复用配置中心提供的地址