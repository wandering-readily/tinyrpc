#ifndef TINYRPC_COMM_LOG_H
#define TINYRPC_COMM_LOG_H

#include <sstream>
// #include <iostream>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <memory>
#include <vector>
#include <queue>
#include <semaphore.h>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/comm/config.h"


namespace tinyrpc {

/*
 * 1. 定义log输出宏
 * 下面是宏需要的函数和类型的提前声明
 */


// 1. 控制打不打印日志，这个在生产环境中有用
// 编译期设置打不打印log
// 编译期能够优化编译期false
static constexpr const bool g_openLogger_flag = false;
consteval const bool OpenLogger() {
  return g_openLogger_flag;
}


class Logger;
std::shared_ptr<Logger> GetGRpcLogger();




template<typename... Args>
std::string formatString(const char* str, Args&&... args) {

  // 这个函数返回(str, args...)的长度，不包括'/0'
  int size = snprintf(nullptr, 0, str, args...);

  std::string result;
  if (size > 0) {
    result.resize(size);
	// 写入size个字符，size+1添加为'/0'
	// std::string保证是内存连续，只不过如果是小内存那么分配在栈中，否在分配堆内存
	// C++11 std::string '/0'不计入size()当中，但是会隐藏在末尾
    snprintf(&result[0], size + 1, str, args...);
  }

  return result;
}


// 输出至文件流
// 默认stdout输出
#define RpcDebugLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::DEBUG, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define RpcInfoLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::INFO, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define RpcWarnLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::WARN, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define RpcErrorLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::ERROR, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \


#define AppDebugLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::DEBUG, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define AppInfoLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::INFO, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define AppWarnLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::WARN, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()

#define AppErrorLogStream \
if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogTmp(tinyrpc::LogLevel::ERROR, \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \



// 输出到conf指定文件
#define RpcDebugLog \
  if constexpr (tinyrpc::OpenLogger()) \
    tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()

#define RpcInfoLog \
  if constexpr (tinyrpc::OpenLogger()) \
    tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() 

#define RpcWarnLog \
  if constexpr (tinyrpc::OpenLogger()) \
    tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()




/*
  printf("%s: %s, %d, %s, errno %d, %s\n", __TIME__, __FILE__, __LINE__, __func__, errno, strerror(errno));\
  Exit(0); 
 */

#define RpcErrorLog \
  if constexpr (tinyrpc::OpenLogger()) \
	tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::sptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()

// APP_LOG_LEVEL
// ???
// !!!
// App类的输出改为文本，需要输入server
// LogEvent类需要返回输出 + 额外输出
#define AppDebugLog(str, ...) \
  if constexpr (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\

#define AppInfoLog(str, ...) \
  if constexpr (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\

#define AppWarnLog(str, ...) \
  if constexpr (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\

#define AppErrorLog(str, ...) \
  if constexpr (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\



// printf("%s", ss.str().c_str());
// std::cout << ss.str(); 

#define locateErrorExit \
	{\
	std::stringstream ss; \
	ss << __FILE__ << "-" << __func__ << "-" << __LINE__ \
		<<  ", errno " << errno << ", " << strerror(errno) << "\n"; \
	printf("%s", ss.str().c_str()); \
	Exit(0); \
	}\





/* 
 * 2. 定义输出内容
 */

typedef void (*OutputFunc)(const char* msg, int len);
typedef void (*FlushFunc)();

// 输出Logtype Loglevel的util函数
enum class LogType {
	RPC_LOG = 1,
	APP_LOG = 2,
};

pid_t gettid();
LogLevel stringToLevel(const std::string& str);
std::string levelToString(LogLevel level);


/*
 * 3. logger打印
 * LogEvent负责定义输出内容及格式
 * LogTmp负责输出stdout
 * LogInGrpcLogger负责输出gRpcLogger, {AsynLogger, Logger}是负责gRpcLogger的异步刷新，Logger负责定时刷新
 */
class LogEvent {

 public:
 	
	typedef std::shared_ptr<LogEvent> sptr;
	LogEvent(LogLevel level, const char* file_name, int line, 
			const char* func_name, LogType type);

	~LogEvent();
	
	void log();

	// m_ss的引用
	// 方便宏输入内容
	std::stringstream& getStringStream();

	std::string toString();


 private:
		
	// uint64_t m_timestamp;
	timeval m_timeval;
	LogLevel m_level;
	pid_t m_pid {0};
	pid_t m_tid {0};
	int m_cor_id {0};

	// const char *是指向常量str的指针，如果一直是从常量str中来，
	// 那么不需要关心什么时候释放内存
	// 而以防const char *指向的是char[]，最好使用string
	// 因为本例传入的是常量str内容，可以这么用
	const char* m_file_name;
	int m_line {0};
	const char* m_func_name;
	LogType m_type;
	std::string m_msg_no;

	std::stringstream m_ss;
};


class LogTmp {
 
 public:
	explicit LogTmp(LogLevel, LogEvent::sptr);

	~LogTmp();

	std::stringstream& getStringStream() {
		return m_event->getStringStream();
	}


 private:
	LogLevel m_level;
	LogEvent::sptr m_event;
};


class LogInGrpcLogger {
 
 public:
	explicit LogInGrpcLogger(LogEvent::sptr);

	~LogInGrpcLogger();

	std::stringstream& getStringStream() {
		return m_event->getStringStream();
	}

 private:
	LogEvent::sptr m_event;
};



/*
 * 4. 定义异步logger
 * AsyncLogger负责日志线程池内容输出到日志文件
 * Logger负责输出到前端输入
 */
class AsyncLogger {
 public:
  typedef std::shared_ptr<AsyncLogger> sptr;

	AsyncLogger(const char* file_name, const char* file_path, 
			int max_size, LogType logtype);
	~AsyncLogger();

	void push(std::vector<std::string>& buffer);

	void flush();

	static void* excute(void*);

	void stop();

 public:
	std::queue<std::vector<std::string>> m_tasks;

 private:
	const char* m_file_name;
	const char* m_file_path;
	int m_max_size {0};
	LogType m_log_type;
	int m_no {0};
	bool m_need_reopen {false};
	FILE* m_file_handle {nullptr};
	std::string m_date;

 	Mutex m_mutex;
	pthread_cond_t m_condition;
	bool m_stop {false};

 public:
	pthread_t m_thread;
	sem_t m_semaphore;

};

class Logger {

 public:
  typedef std::shared_ptr<Logger> sptr;

	Logger();
	~Logger();

	void init(const char* file_name, const char* file_path, 
			int max_size, int sync_inteval);

	// 对外提供输入格式
	void pushRpcLog(const std::string& log_msg);
	void pushAppLog(const std::string& log_msg);
	void loopFunc();

	void stopAndFlush();

	void start();

	AsyncLogger::sptr getAsyncLogger() {
		return m_async_rpc_logger;
	}

	AsyncLogger::sptr getAsyncAppLogger() {
		return m_async_app_logger;
	}

 public:
	std::vector<std::string> m_buffer;
	std::vector<std::string> m_app_buffer;

 private:
 	Mutex m_app_buff_mutex;
 	Mutex m_buff_mutex;
	bool m_is_init {false};
	AsyncLogger::sptr m_async_rpc_logger;
	AsyncLogger::sptr m_async_app_logger;

	int m_sync_inteval {0};

};

void Exit(int code);

}

#endif
