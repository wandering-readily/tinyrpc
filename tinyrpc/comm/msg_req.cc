#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <random>
#include <atomic>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/msg_req.h"


namespace tinyrpc {

static thread_local std::string t_msg_req_nu;
static thread_local std::string t_max_msg_req_nu;
// config每个进程只读一次，确保在进程主线程读取config时后不发生改变
static std::atomic_int t_msg_req_len = 20;
static std::atomic_bool t_msg_req_len_inited = false;
bool Init_t_msg_req_len(int msg_req_len) {
  if (!t_msg_req_len_inited) {
    t_msg_req_len = msg_req_len;
    t_msg_req_len_inited = true;
    return true;
  }
  return false;
}


class randomgFDRAII {
public:
  randomgFDRAII(int fd) : fd_(fd) {}

  ~randomgFDRAII() {
    close(fd_);
  }

private:
  int fd_;
};

static pthread_once_t once_msgReq = PTHREAD_ONCE_INIT;  
static int g_random_fd = -1;
std::unique_ptr<randomgFDRAII> randomgFDPtr;

void createRandomFd() {
  // /dev/random /dev/urandom 读均线程安全
  // 如果只有读线程，那么没有问题。
  // 因为，不同的线程可以创建自己的文件描述符表项，再分别指向不同的文件表项，
  // 而每个文件表项里面可以有不同的当前文件偏移量，所以没有问题。而且这种情况也根本不需要用到锁

  // 文件写APPEND模式多线程安全，会触发inode的锁
  // Linux多线程写（共享描述符，非共享描述符）
  // 多线程或者多进程 共享文件描述符的写操作会对同一个struct file操作，
  // 那么写操作会保证原子序，但写入顺序由系统调用顺序决定

  // 虽然系统级线程写安全，但是文件层面线程写不安全
  // 可能共享描述符写可能写入混乱
  // 其次fd可能关闭，因此要避免多线程操作一个文件
  g_random_fd = open("/dev/urandom", O_RDONLY);
  // 程序退出后自动释放资源
  randomgFDPtr = std::make_unique<randomgFDRAII>(g_random_fd);
}

std::string MsgReqUtil::genMsgNumber() {

  if (t_msg_req_nu.empty() || t_msg_req_nu == t_max_msg_req_nu) {
    // ???
    // 这里存在多线程问题？
    // 考虑是否放在多线程环境中!
    if (g_random_fd == -1) {
      // 随机数生成器，为系统提供随机数
      // g_random_fd = open("/dev/urandom", O_RDONLY);
      // pthread_once解决多线程问题
      // 为什么用pthread_once()呢，因为要避免fd资源分配但未释放
      pthread_once(&once_msgReq, createRandomFd);
    } 
    std::string res(t_msg_req_len, 0);
    if ((::read(g_random_fd, &res[0], t_msg_req_len)) != t_msg_req_len) {
      RpcErrorLog << "read /dev/urandom data less " << t_msg_req_len << " bytes";
      return "";
    }

    // 非数字项转化为数字项
    t_max_msg_req_nu = "";
    for (int i = 0; i < t_msg_req_len; ++i) {
      uint8_t x = ((uint8_t)(res[i])) % 10;
      res[i] = x + '0';
      t_max_msg_req_nu += "9";
    }
    t_msg_req_nu = res;

  } else {
    // t_msg_req_nu当作是一个数字，从random状态递增到20*'9'后再次random循环
    int i = t_msg_req_nu.length() - 1; 
    while(t_msg_req_nu[i] == '9' && i >= 0) {
      i--;
    }
    if (i >= 0) {
      t_msg_req_nu[i] += 1;
      for (size_t j = i + 1; j < t_msg_req_nu.length(); ++j) {
        t_msg_req_nu[j] = '0';
      }
    }

  }    
  // RpcDebugLog << "get msg_req_nu is " << t_msg_req_nu;
  return t_msg_req_nu;
}

}