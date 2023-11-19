### 已完成
> 从muduo中学习添加errno处理，尤其在_hook系列函数和TcpCoonection中

> 添加异步asyncChannel

> 添加keepAlive机制

> 添加template client模板(SIFNAE)，避免不合理调用

> 添加轻型timer, TimerPool

> 原实现定时删除connection，只有一次服务，无论是client还是server，增加同步调用的tcp复用，异步调用使用tcp复用则会出错

### 待扩展
> 增加注册服务中心，转发节点（HTTP实现）仿照https://github.com/wandering-readily/7days-golang.git 的gee-cache, gee-rpc设计

> 源代码存在资源符分配泄露或者不回收的问题，尝试解决，间testHTTP.sh

> 增加一个XClient, 复用TCP连接，复用配置中心提供的地址
