#ifndef TINYRPC_COMM_CONFIG_H
#define TINYRPC_COMM_CONFIG_H

#include <string>
#include <memory>
#include <map>
#include <tinyxml/tinyxml.h>
#include "tinyrpc/net/net_address.h"

#ifdef DECLARE_MYSQL_PLUGIN
#include "tinyrpc/comm/mysql_instase.h"
#endif

namespace tinyrpc {

enum class ProtocalType;

enum class LogLevel {
	DEBUG = 1,
	INFO = 2,
	WARN = 3,
	ERROR = 4,
  NONE = 5    // don't print log
};

class Config {

 public:
  typedef std::shared_ptr<Config> sptr;

  Config(const char* file_path);

  ~Config();

  void readConf();

  void readDBConfig(TiXmlElement* node);

  void readLogConfig(TiXmlElement* node);

private:
  void printNodeAndGetTextErrorIfExist(TiXmlElement *, const char *);
  void printNodeErrorIfExist(TiXmlElement *, const char *);


 public:

  // log params
  std::string m_log_path;
  std::string m_log_prefix;
  int m_log_max_size {0};
  LogLevel m_log_level {LogLevel::DEBUG};
  LogLevel m_app_log_level {LogLevel::DEBUG};
  int m_log_sync_inteval {500};

  // coroutine params
  int m_cor_stack_size {0};
  int m_cor_pool_size {0};

  int m_msg_req_len {0};

  int m_max_connect_timeout {0};    // ms
  int m_iothread_num {0};

  int m_timewheel_bucket_num {0};
  int m_timewheel_inteval {0};

  std::string protocalName;
  ProtocalType protocal;
  NetAddress::sptr addr;

  #ifdef DECLARE_MYSQL_PLUGIN 
  std::map<std::string, MySQLOption> m_mysql_options;
  #endif

 private:
  std::string m_file_path;

  TiXmlDocument* m_xml_file;


};


}

#endif // TINYRPC_COMM_CONFIG_H