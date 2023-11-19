#!/bin/bash

# ！！！
# 压力测试下，消耗资源够多，有资源没回收
# 尤其是tcp_connection.cc:
# RpcErrorLog << "read empty while occur read event, because of peer close, fd= " << m_fd << ", sys error=" << strerror(errno) << ", now to clear tcp connection";
# 文件资源不够
#   1. 检测资源泄露
#   2. 减少资源使用，并复用资源

count=0
while ((count < 10000)) ; do
    curl -X GET 'http://127.0.0.1:19999/nonblock?id=1125&req_no=64324325'
    echo $((++count))
done