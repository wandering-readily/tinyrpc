#ifndef TINYRPC_COMM_LOG_H
#define TINYRPC_COMM_LOG_H

#include <sstream>
#include <sstream>
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

typedef void (*OutputFunc)(const char* msg, int len);
typedef void (*FlushFunc)();

class Logger;
bool OpenLogger();
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
	tinyrpc::LogTmp(tinyrpc::LogLevel::DEBUG, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define RpcInfoLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::INFO, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define RpcWarnLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::WARN, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define RpcErrorLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::ERROR, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \


#define AppDebugLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::DEBUG, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define AppInfoLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::INFO, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \

#define AppWarnLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::WARN, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()

#define AppErrorLogStream \
	tinyrpc::LogTmp(tinyrpc::LogLevel::ERROR, \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() \



// 输出到conf指定文件
#define RpcDebugLog \
  if (tinyrpc::OpenLogger()) \
    tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()

#define RpcInfoLog \
  if (tinyrpc::OpenLogger()) \
    tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream() 

#define RpcWarnLog \
  if (tinyrpc::OpenLogger()) \
    tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::ptr( \
			new tinyrpc::LogEvent( \
				tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG \
			) \
		) \
	).getStringStream()

#define RpcErrorLog \
  printf("%s, %d, %s, errno %d, %s\n", __FILE__, __LINE__, __func__, errno, strerror(errno));\
  Exit(0); \
  if (tinyrpc::OpenLogger()) \
	tinyrpc::LogInGrpcLogger( \
		tinyrpc::LogEvent::ptr( \
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
  if (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\

#define AppInfoLog(str, ...) \
  if (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\

#define AppWarnLog(str, ...) \
  if (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\

#define AppErrorLog(str, ...) \
  if (tinyrpc::OpenLogger()) { \
    tinyrpc::GetGRpcLogger()->pushAppLog( \
		tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, \
			__FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
		+ "[" + std::string(__FILE__) \
		+ ":" + std::to_string(__LINE__) \
		+ "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n"); \
  }\



/* 
 * 2. 定义输出内容
 */

// 输出Logtype Loglevel的util函数
enum class LogType {
	RPC_LOG = 1,
	APP_LOG = 2,
};

pid_t gettid();

LogLevel stringToLevel(const std::string& str);
std::string levelToString(LogLevel level);

// 返回是否设置了gRpcLogger
class LogEvent {

 public:
 	
	typedef std::shared_ptr<LogEvent> ptr;
	LogEvent(LogLevel level, const char* file_name, int line, 
			const char* func_name, LogType type);

	~LogEvent();
	
	void log();

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
	explicit LogTmp(LogLevel, LogEvent::ptr);

	~LogTmp();

	std::stringstream& getStringStream() {
		return m_event->getStringStream();
	}


 private:
	LogLevel m_level;
	LogEvent::ptr m_event;
};


class LogInGrpcLogger {
 
 public:
	explicit LogInGrpcLogger(LogEvent::ptr);

	~LogInGrpcLogger();

	std::stringstream& getStringStream() {
		return m_event->getStringStream();
	}

 private:
	LogEvent::ptr m_event;
};


class AsyncLogger {
 public:
  typedef std::shared_ptr<AsyncLogger> ptr;

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
  typedef std::shared_ptr<Logger> ptr;

	Logger();
	~Logger();

	void init(const char* file_name, const char* file_path, 
			int max_size, int sync_inteval);

	void pushRpcLog(const std::string& log_msg);
	void pushAppLog(const std::string& log_msg);
	void loopFunc();

	void stopAndFlush();

	void start();

	AsyncLogger::ptr getAsyncLogger() {
		return m_async_rpc_logger;
	}

	AsyncLogger::ptr getAsyncAppLogger() {
		return m_async_app_logger;
	}

 public:
	std::vector<std::string> m_buffer;
	std::vector<std::string> m_app_buffer;

 private:
 	Mutex m_app_buff_mutex;
 	Mutex m_buff_mutex;
	bool m_is_init {false};
	AsyncLogger::ptr m_async_rpc_logger;
	AsyncLogger::ptr m_async_app_logger;

	int m_sync_inteval {0};

};

void Exit(int code);

}

#endif
