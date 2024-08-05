#include <time.h>
#include <sys/time.h>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <algorithm>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>

#ifdef DECLARE_MYSQL_PLUGIN 
#include <mysql/mysql.h>
#endif


#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/run_time.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/timer.h"



namespace tinyrpc {

// flush，然后回收异步处理线程
void CoredumpHandler(int signal_no) {
  printf("progress received invalid signal, will exit\n");

  // 设置默认处理signal_no的信号处理函数
  signal(signal_no, SIG_DFL);
  // 发起信号
  raise(signal_no);
}

class Coroutine;

static thread_local pid_t t_thread_id = 0;
static pid_t g_pid = 0;

// LogLevel g_log_level = DEBUG;

pid_t gettid() {
  if (t_thread_id == 0) {
    t_thread_id = syscall(SYS_gettid);
  }
  return t_thread_id;
}






// 2. 链接在start.cc保存的全局变量gRpcLogger
class Logger;
extern Logger::sptr gRpcLogger;

std::shared_ptr<Logger> GetGRpcLogger() {
  return gRpcLogger;
}


// 3. 常用的{LogLevel, Logtype}  <==>  std::string转换函数
std::string levelToString(LogLevel level) {
  std::string re = "DEBUG";
  switch(level) {
    case LogLevel::DEBUG:
      re = "DEBUG";
      return re;
    
    case LogLevel::INFO:
      re = "INFO";
      return re;

    case LogLevel::WARN:
      re = "WARN";
      return re;

    case LogLevel::ERROR:
      re = "ERROR";
      return re;

    case LogLevel::NONE:
      re = "NONE";

    default:
      return re;
  }
}


LogLevel stringToLevel(const std::string& str) {
    if (str == "DEBUG")
      return LogLevel::DEBUG;
    
    if (str == "INFO")
      return LogLevel::INFO;

    if (str == "WARN")
      return LogLevel::WARN;

    if (str == "ERROR")
      return LogLevel::ERROR;

    if (str == "NONE")
      return LogLevel::NONE;

    return LogLevel::DEBUG;
}

std::string LogTypeToString(LogType logtype) {
  switch (logtype) {
    case LogType::APP_LOG:
      return "app";
    case LogType::RPC_LOG:
      return "rpc";
    default:
      return "";
  }
}



// 4. LogEvent
LogEvent::LogEvent(LogLevel level, const char* file_name, int line, const char* func_name, LogType type)
  : m_level(level),
    m_file_name(file_name),
    m_line(line),
    m_func_name(func_name),
    m_type(type) {
}

LogEvent::~LogEvent() {}

void LogEvent::log() {
  m_ss << "\n";
  if (m_type == LogType::RPC_LOG) {
    gRpcLogger->pushRpcLog(m_ss.str());
  } else if (m_type == LogType::APP_LOG) {
    gRpcLogger->pushAppLog(m_ss.str());
  }
}


std::stringstream& LogEvent::getStringStream() {

  // time_t now_time = m_timestamp;
  
  // 1. 当前时间
  gettimeofday(&m_timeval, nullptr);

  struct tm time; 
  localtime_r(&(m_timeval.tv_sec), &time);

  const char* format = "%Y-%m-%d %H:%M:%S";
  char buf[128];
  strftime(buf, sizeof(buf), format, &time);

  m_ss << "[" << buf << "." << m_timeval.tv_usec << "]\t"; 

  std::string s_level = levelToString(m_level);
  m_ss << "[" << s_level << "]\t";

  // 2. 当前进程号
  // 因为getpid()每次得到相同值，且我们不在乎不在乎值的先后顺序
  if (g_pid == 0) {
    g_pid = getpid();
  }
  m_pid = g_pid;  

  // 3. 当前线程号
  if (t_thread_id == 0) {
    t_thread_id = gettid();
  }
  m_tid = t_thread_id;

  // 4. 当前协程号
  m_cor_id = Coroutine::GetCurrentCoroutine()->getCorId();
  
  m_ss << "[" << m_pid << "]\t" 
		<< "[" << m_tid << "]\t"
		<< "[" << m_cor_id << "]\t"
    << "[" << m_file_name << ":" << m_line << "]\t"
    << "[" << m_func_name << "]\t";
  RunTime* runtime = getCurrentRunTime();

  // 5. 当前协程运行时参数
  if (runtime) {
    std::string msgno = runtime->m_msg_no;
    if (!msgno.empty()) {
      m_ss << "[" << msgno << "]\t";
    }

    std::string interface_name = runtime->m_interface_name;
    if (!interface_name.empty()) {
      m_ss << "[" << interface_name << "]\t";
    }

  }
  return m_ss;
}

std::string LogEvent::toString() {
  return getStringStream().str();
}



// 5. LogTmp
LogTmp::LogTmp(LogLevel level, LogEvent::sptr event) : m_level(level), m_event(event) {}
LogTmp::~LogTmp() {
  std::string content = m_event->toString();
  fwrite(content.data(), 1, content.size(), stdout);
}


// 6. logInGrpcLogger
LogInGrpcLogger::LogInGrpcLogger(LogEvent::sptr event) : m_event(event) {}
LogInGrpcLogger::~LogInGrpcLogger() {
  m_event->log(); 
}







Logger::Logger() { // cannot do anything which will call LOG ,otherwise is will coredump
}

Logger::~Logger() {
  // 清空AsyncLogger内容，并强制fflush()
  stopAndFlush();

  // 按道理谁来启动谁结束，那么应该是asyncLogger自己join()
  // pthread_join(m_async_rpc_logger->m_thread, NULL);
  // pthread_join(m_async_app_logger->m_thread, NULL);
}


void Logger::init(const char* file_name, const char* file_path, int max_size, int sync_inteval) {
  if (!m_is_init) {
    m_sync_inteval = sync_inteval;

    for (int i = 0 ; i < 1000000; ++i) {
      m_app_buffer.push_back("");
      m_buffer.push_back("");
    }
    // m_app_buffer.resize(1000000);
    // m_buffer.resize(1000000);

    // 启动Async保存日志程序
    m_async_rpc_logger = std::make_shared<AsyncLogger>(file_name, file_path, max_size, LogType::RPC_LOG);
    m_async_app_logger = std::make_shared<AsyncLogger>(file_name, file_path, max_size, LogType::APP_LOG);

    // 设置信号处理函数
    signal(SIGSEGV, CoredumpHandler);
    signal(SIGABRT, CoredumpHandler);
    signal(SIGTERM, CoredumpHandler);
    signal(SIGKILL, CoredumpHandler);
    signal(SIGINT, CoredumpHandler);
    signal(SIGSTKFLT, CoredumpHandler);

    // ignore SIGPIPE 
    signal(SIGPIPE, SIG_IGN);
    m_is_init = true;
  }
}


// 定义Loggeer定时输出函数
// 并加入reactor，定时输出到底层线程池
void Logger::start() {
  TimerEvent::sptr event = std::make_shared<TimerEvent>(m_sync_inteval, true, std::bind(&Logger::loopFunc, this));
  Reactor::GetReactor()->getTimer()->addTimerEvent(event);
}
	
void Logger::loopFunc() {
  std::vector<std::string> app_tmp;
  {
  Mutex::Lock lock1(m_app_buff_mutex);
  app_tmp.swap(m_app_buffer);
  }
  
  std::vector<std::string> tmp;
  {
  Mutex::Lock lock2(m_buff_mutex);
  tmp.swap(m_buffer);
  }

  // 添加async内容
  m_async_rpc_logger->push(tmp);
  m_async_app_logger->push(app_tmp);
}

void Logger::pushRpcLog(const std::string& msg) {
  Mutex::Lock lock(m_buff_mutex);
  m_buffer.push_back(std::move(msg));
}

void Logger::pushAppLog(const std::string& msg) {
  Mutex::Lock lock(m_app_buff_mutex);
  m_app_buffer.push_back(std::move(msg));
}

void Logger::stopAndFlush() {
  // 确保先输出内容到底层线程，再stop
  loopFunc();
  m_async_rpc_logger->stop();
  m_async_rpc_logger->flush();

  m_async_app_logger->stop();
  m_async_app_logger->flush();
}



AsyncLogger::AsyncLogger(const char* file_name, const char* file_path, int max_size, LogType logtype)
  : m_file_name(file_name), m_file_path(file_path), m_max_size(max_size), m_log_type(logtype) {
  // sem_init()初始化
  // sem作为同步工具
  int rt = sem_init(&m_semaphore, 0, 0);
  assert(rt == 0);

  rt = pthread_create(&m_thread, nullptr, &AsyncLogger::excute, this);
  assert(rt == 0);
  rt = sem_wait(&m_semaphore);
  assert(rt == 0);

}

AsyncLogger::~AsyncLogger() {
  pthread_join(m_thread, NULL);
  pthread_cond_destroy(&m_condition);
  sem_destroy(&m_semaphore);
}

void* AsyncLogger::excute(void* arg) {
  AsyncLogger* ptr = reinterpret_cast<AsyncLogger*>(arg);
  int rt = pthread_cond_init(&ptr->m_condition, NULL);
  assert(rt == 0);

  // 确保创建线程后的pthread_cond_init()之后才完成构造
  rt = sem_post(&ptr->m_semaphore);
  assert(rt == 0);

  while (1) {
    std::vector<std::string> tmp;
    bool is_stop = false;

    // 1. 取任务
    {
    Mutex::Lock lock(ptr->m_mutex);
    // 要么来task，要么要退出
    while (ptr->m_tasks.empty() && !ptr->m_stop) {
      pthread_cond_wait(&(ptr->m_condition), ptr->m_mutex.getMutex());
    }

    if (!ptr->m_tasks.empty()) {
      tmp.swap(ptr->m_tasks.front());
      ptr->m_tasks.pop();
    }
    is_stop = ptr->m_stop;
    }

    // 2. 添加日志内容
    timeval now;
    gettimeofday(&now, nullptr);

    struct tm now_time;
    localtime_r(&(now.tv_sec), &now_time);

    const char *format = "%Y%m%d";
    char date[32];
    strftime(date, sizeof(date), format, &now_time);
    if (ptr->m_date != std::string(date)) {
      // cross day
      // reset m_no m_date
      // 重置
      // 隔天换日志文件
      ptr->m_no = 0;
      ptr->m_date = std::string(date);
      ptr->m_need_reopen = true;
    }

    // 确保第一次打开文件错误时，下次输入内容时再次打开文件
    if (!ptr->m_file_handle) {
      ptr->m_need_reopen = true;
    }    

    // 日志文件名字
    std::string full_file_name;

    if (ptr->m_need_reopen) {
      // 有两种可能：1. 日志文件满了；  2. 日志文件没打开
      // 因此多加判断
      if (ptr->m_file_handle) {
        fclose(ptr->m_file_handle);
      }

      std::stringstream ss;
      ss << ptr->m_file_path << ptr->m_file_name << "_" << ptr->m_date << "_" 
        << LogTypeToString(ptr->m_log_type) << "_" << ptr->m_no << ".log";
      full_file_name = ss.str();

      ptr->m_file_handle = fopen(full_file_name.c_str(), "a");
      if(ptr->m_file_handle == nullptr) {
        printf("open fail errno = %d reason = %s \n", errno, strerror(errno));
      }
      ptr->m_need_reopen = false;
    }

    // 确保日志文件不超长
    if (ptr->m_file_handle && ftell(ptr->m_file_handle) > ptr->m_max_size) {
      fclose(ptr->m_file_handle);

      // single log file over max size
      ptr->m_no++;
      std::stringstream ss2;
      ss2 << ptr->m_file_path << ptr->m_file_name << "_" << ptr->m_date << "_" 
        << LogTypeToString(ptr->m_log_type) << "_" << ptr->m_no << ".log";
      full_file_name = ss2.str();

      // printf("open file %s", full_file_name.c_str());
      ptr->m_file_handle = fopen(full_file_name.c_str(), "a");
      ptr->m_need_reopen = false;
    }


    if (!ptr->m_file_handle) {
      printf("open log file %s error!", full_file_name.c_str());

    } else {
      for(auto i : tmp) {
        if (!i.empty()) {
          fwrite(i.c_str(), 1, i.length(), ptr->m_file_handle);
        }
      }
      // tmp.clear();
      fflush(ptr->m_file_handle);
    }

    // 最后内容输出完毕后，如果要退出就推出
    if (is_stop) {
      break;
    }
  }
 
  if (ptr->m_file_handle) {
    fclose(ptr->m_file_handle);
  }

  return nullptr;
}

// ???
// 不是fixed queue，因此可能有超出内存的风险
void AsyncLogger::push(std::vector<std::string>& buffer) {
  if (!buffer.empty()) {
    Mutex::Lock lock(m_mutex);
    m_tasks.push(buffer);
    pthread_cond_signal(&m_condition);
  }
}

void AsyncLogger::flush() {
  if (m_file_handle) {
    fflush(m_file_handle);
  }
}


void AsyncLogger::stop() {
  if (!m_stop) {
    m_stop = true;
    pthread_cond_signal(&m_condition);
  }
}



// Exit()优雅的退出
void Exit(int code) {
  #ifdef DECLARE_MYSQL_PLUGIN
  mysql_library_end();
  #endif

  _exit(code);
}


}
