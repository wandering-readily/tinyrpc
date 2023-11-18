// 额外增加的#define
// 只是为了看到MYSQL的操作
// #define DECLARE_MYSQL_PLUGIN 1

#ifdef DECLARE_MYSQL_PLUGIN 
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#endif

#include "tinyrpc/comm/mysql_instase.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine_hook.h"


namespace tinyrpc {

#ifdef DECLARE_MYSQL_PLUGIN 

static thread_local MySQLInstaseFactroy* t_mysql_factory = NULL;


MySQLThreadInit::MySQLThreadInit() {
  RpcDebugLog << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< call mysql_thread_init";
  mysql_thread_init();
}

MySQLThreadInit::~MySQLThreadInit() {
  mysql_thread_end();
  RpcDebugLog << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> call mysql_thread_end";
}

// 每个thread一个单例线程!
// 这样保证多线程服务
MySQLInstaseFactroy* MySQLInstaseFactroy::GetThreadMySQLFactory(std::map<std::string, MySQLOption> mysql_options) {
  if (t_mysql_factory) {
    return t_mysql_factory;
  }
  t_mysql_factory = new MySQLInstaseFactroy(std::move(mysql_options));
  return t_mysql_factory;

}

MySQLInstase::sptr MySQLInstaseFactroy::GetMySQLInstase(const std::string& key) {
  // 因为是m_mysql_option在读取DBConfig后只读，因此可以多线程服务
  auto it2 = m_mysql_options.find(key);
  if (it2 == m_mysql_options.end()) {
    RpcErrorLog << "get MySQLInstase error, not this key[" << key << "] exist";
    return NULL;
  }
  RpcDebugLog << "create MySQLInstase of key " << key;
  MySQLInstase::sptr instase = std::make_shared<MySQLInstase>(it2->second);
  return instase;
}


MySQLInstase::MySQLInstase(const MySQLOption& option) : m_option(option) {
  // MYSQL instance初始化情况
  int ret = reconnect();
  if (ret != 0) {
    return;
  }

  m_init_succ = true;

}

int MySQLInstase::reconnect() {
  // this static value only call once
  // it will call mysql_thread_init when first call MySQLInstase::reconnect function
  // and it will call mysql_thread_end when current thread destroy
  // 也就是第一次进入这个函数载入构造函数
  // 线程结束时载入析构函数
  static thread_local MySQLThreadInit t_mysql_thread_init;

  if (m_sql_handler) {
    mysql_close(m_sql_handler);
    m_sql_handler = NULL;
  }

  // ???
  // 为什么获取mysql_handler需要加锁！
  {
  Mutex::Lock lock(m_mutex);
  m_sql_handler =  mysql_init(NULL);
  // RpcDebugLog << "mysql fd is " << m_sql_handler.net.fd;
  }
  if (!m_sql_handler) {
    RpcErrorLog << "faild to call mysql_init allocate MYSQL instase";
    return -1;
  }
  // int value = 0;
  // mysql_options(m_sql_handler, MYSQL_OPT_RECONNECT, &value);
  if (!m_option.m_char_set.empty()) {
    mysql_options(m_sql_handler, MYSQL_SET_CHARSET_NAME, m_option.m_char_set.c_str());
  }
  RpcDebugLog << "begin to connect mysql{ip:" << m_option.m_addr.getIP() << ", port:" << m_option.m_addr.getPort() 
    << ", user:" << m_option.m_user << ", passwd:" << m_option.m_passwd << ", select_db: "<< m_option.m_select_db << "charset:" << m_option.m_char_set << "}";
  // mysql_real_connect(m_sql_handler, m_option.m_addr.getIP().c_str(), m_option.m_user.c_str(), 
  //     m_option.m_passwd.c_str(), m_option.m_select_db.c_str(), m_option.m_addr.getPort(), NULL, 0);
  if (!mysql_real_connect(m_sql_handler, m_option.m_addr.getIP().c_str(), m_option.m_user.c_str(), 
      m_option.m_passwd.c_str(), m_option.m_select_db.c_str(), m_option.m_addr.getPort(), NULL, 0)) {

    RpcErrorLog << "faild to call mysql_real_connect, peer addr[ " << m_option.m_addr.getIP() << ":" << m_option.m_addr.getPort() << "], mysql sys errinfo[" << mysql_error(m_sql_handler) << "]";
    return -1;
  }
  RpcDebugLog << "mysql_handler connect succ";
  return 0;
}

bool MySQLInstase::isInitSuccess() {
  return m_init_succ;
}

MySQLInstase::~MySQLInstase() {
  if (m_sql_handler) {
    mysql_close(m_sql_handler);
    m_sql_handler = NULL;
  }
}

// 1. 问答
int MySQLInstase::commit() {
  int rt = query("COMMIT;");
  if (rt == 0) {
    m_in_trans = false;
  }
  return rt;
}

int MySQLInstase::begin() {
  int rt = query("BEGIN;");
  if (rt == 0) {
    m_in_trans = true;
  }
  return rt;
}

int MySQLInstase::rollBack() {
  int rt = query("ROLLBACK;");
  if (rt == 0) {
    m_in_trans = false;
  }
  return rt;
}

int MySQLInstase::query(const std::string& sql) {
  if (!m_init_succ) {
    RpcErrorLog << "query error, mysql_handler init faild";
    return -1;
  }
  if (!m_sql_handler) {
    RpcDebugLog << "*************** will reconnect mysql ";
    reconnect();
  }
  if (!m_sql_handler) {
    RpcDebugLog << "reconnect error, query return -1";
    return -1;
  }

  RpcDebugLog << "begin to excute sql[" << sql << "]";
  int rt = mysql_real_query(m_sql_handler, sql.c_str(), sql.length());
  if (rt != 0) {
    RpcErrorLog << "excute mysql_real_query error, sql[" << sql << "], mysql sys errinfo[" << mysql_error(m_sql_handler) << "]"; 
    // if connect error, begin to reconnect
    // 如果问询mysql发生server error，可以再次连接server问答
    if (mysql_errno(m_sql_handler) == CR_SERVER_GONE_ERROR || mysql_errno(m_sql_handler) == CR_SERVER_LOST) {
      
      rt = reconnect();
      if (rt != 0 && !m_in_trans) {
        // if reconnect succ, and current is not a trans, can do query sql again 
        rt = mysql_real_query(m_sql_handler, sql.c_str(), sql.length());
        return rt;
      }
    }
  } else {
    RpcInfoLog << "excute mysql_real_query success, sql[" << sql << "]";
  }
  return rt;
}

// 2. 结果查询
MYSQL_RES* MySQLInstase::storeResult() {
  if (!m_init_succ) {
    RpcErrorLog << "query error, mysql_handler init faild";
    return NULL;
  }
  int count = mysql_field_count(m_sql_handler);
  if (count != 0) {
    MYSQL_RES* res = mysql_store_result(m_sql_handler);
    if (!res) {
      RpcErrorLog << "excute mysql_store_result error, mysql sys errinfo[" << mysql_error(m_sql_handler) << "]";
    } else {
      RpcDebugLog << "excute mysql_store_result success";
    }
    return res;
  } else {
    RpcDebugLog << "mysql_field_count = 0, not need store result";
    return NULL;
  }

}

MYSQL_ROW MySQLInstase::fetchRow(MYSQL_RES* res) {
  if (!m_init_succ) {
    RpcErrorLog << "query error, mysql_handler init faild";
    return NULL;
  }
  return mysql_fetch_row(res);
}

long long MySQLInstase::numFields(MYSQL_RES* res) {
  if (!m_init_succ) {
    RpcErrorLog << "query error, mysql_handler init faild";
    return -1;
  }
  return mysql_num_fields(res);
}

void MySQLInstase::freeResult(MYSQL_RES* res) {
  if (!m_init_succ) {
    RpcErrorLog << "query error, mysql_handler init faild";
    return;
  }
  if (!res) {
    RpcDebugLog << "free result error, res is null";
    return;
  }
  mysql_free_result(res);

}


long long MySQLInstase::affectedRows() {
  if (!m_init_succ) {
    RpcErrorLog << "query error, mysql_handler init faild";
    return -1;
  }
  return mysql_affected_rows(m_sql_handler);

}


// 错误信息获取
std::string MySQLInstase::getMySQLErrorInfo() {
  return std::string(mysql_error(m_sql_handler));
}

int MySQLInstase::getMySQLErrno() {
  return mysql_errno(m_sql_handler);
}

#endif

}