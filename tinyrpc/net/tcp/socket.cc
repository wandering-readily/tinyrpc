#include <sys/socket.h>

#include "socket.h"
#include "tinyrpc/comm/log.h"


namespace tinyrpc {

int createNonblockingOrDie(sa_family_t family)
{
  // SOCK_NONBLOCK兼容性不太好，要求Linux内核版本高于2.6.27才可以使用
  // 还有::fcntl()方法
  // SOCK_CLOEXEC 在fork时子进程关闭SOCK
  // family: "AF_INET"(IPV4)和"AF_INET6"(IPV6)
  int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
  if (sockfd < 0) [[unlikely]] {
    locateErrorExit
  }
  // printf("open fd %d\n", sockfd);
  return sockfd;
}

void setReuseAddr(int sockfd, bool on) {
  int optval = on ? 1 : 0;
  // 设置是否复用地址
  // 1.
  // SO_REUSEADDR用于对TCP套接字处于TIME_WAIT状态下的socket
  // 才可以重复绑定使用
  // 2.
  // SO_REUSEADDR允许在同一端口上启动同一服务器的多个实例
  // 只要每个实例捆绑一个不同的本地IP地址即可
  // 对于TCP，我们根本不可能启动捆绑相同IP地址和相同端口号的多个服务器
  // 3.
  // SO_REUSEADDR允许单个进程捆绑同一端口到多个套接口上
  // 只要每个捆绑指定不同的本地IP地址即可. 这一般不用于TCP服务器
  // 4.
  // SO_REUSEADDR允许完全重复的捆绑
  // 当一个IP地址和端口绑定到某个套接口上时
  // 还允许此IP地址和端口捆绑到另一个套接口上
  // 一般来说, 这个特性仅在支持多播的系统上才有, 
  // 而且只对UDP套接口而言(TCP不支持多播)

  ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               &optval, static_cast<socklen_t>(sizeof optval));
  // FIXME CHECK
}

void setReusePort(int sockfd, bool on) {
  int optval = on ? 1 : 0;
  int ret = ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT,
                         &optval, static_cast<socklen_t>(sizeof optval));
  if (ret < 0 && on) [[unlikely]] {
    locateErrorExit
  }
}

void setKeepAlive(int sockfd, bool on) {
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE,
               &optval, static_cast<socklen_t>(sizeof optval));
  // FIXME CHECK
}

};