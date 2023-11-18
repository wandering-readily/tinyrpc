#ifndef TINYRPC_NET_TCP_SOCKET_H
#define TINYRPC_NET_TCP_SOCKET_H


#include <unistd.h>


namespace tinyrpc {

int createNonblockingOrDie(sa_family_t);

void setReuseAddr(int sockfd, bool on);

void setReusePort(int sockfd, bool on);

void setKeepAlive(int sockfd, bool on);

};

#endif