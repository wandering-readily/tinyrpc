### 已完成
> 从muduo中学习添加errno处理，尤其在_hook系列函数和TcpCoonection中

> 添加异步asyncChannel

> 添加keepAlive机制

> 添加template client模板(SIFNAE)，避免不合理调用

> 添加轻型timer, TimerPool

> 原实现定时删除connection，只有一次服务，无论是client还是server，增加同步调用的tcp复用，异步调用使用tcp复用则会出错

### 待扩展
> 检查是否有资源泄露

> 粘包问题 TcpConnction.cc --> void TcpConnection::input() --> line240
如果rt == read_count 且 package全部传入完;  这会让read_hook会在EPOLL_WAIT等待;
在tinypb_codec方法 可以分析packageLen，那么很好避免这一点;
而HTTP方法 header['Content-Type']不好分析packageLen


> 增加注册服务中心，转发节点（HTTP实现）仿照https://github.com/wandering-readily/7days-golang.git 的gee-cache, gee-rpc设计

> 增加一个XClient, 复用TCP连接，复用配置中心提供的地址
