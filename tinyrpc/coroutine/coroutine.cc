#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <atomic>
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/run_time.h"

namespace tinyrpc {

// 多进程 --> 多协程
// 主协程
// 多进程(一) <--> 多协程(多) 一多对应?
// main coroutine, every io thread have a main_coroutine
static thread_local Coroutine* t_main_coroutine = NULL;

// 当前协程的object使用thread_local
// current thread is runing which coroutine
static thread_local Coroutine* t_cur_coroutine = NULL;

static thread_local RunTime* t_cur_run_time = NULL;

// static thread_local bool t_enable_coroutine_swap = true;

// 全局静态变量使用atomic类型
static std::atomic_int t_coroutine_count {0};

static std::atomic_int t_cur_coroutine_id {1};

int getCoroutineIndex() {
  return t_cur_coroutine_id;
}

RunTime* getCurrentRunTime() {
  return t_cur_run_time;
}

void setCurrentRunTime(RunTime* v) {
  t_cur_run_time = v;
}

// 执行Coroutine的回调函数，然后让出当前协程内容
void CoFunction(Coroutine* co) {

  if (co!= nullptr) {
    co->setIsInCoFunc(true);

    // 去执行协程回调函数
    co->m_call_back();

    co->setIsInCoFunc(false);
  }

  // here coroutine's callback function finished, that means coroutine's life is over. we should yiled main couroutine
  Coroutine::Yield();
}

// void Coroutine::SetCoroutineSwapFlag(bool value) {
//   t_enable_coroutine_swap = value;
// }

// bool Coroutine::GetCoroutineSwapFlag() {
//   return t_enable_coroutine_swap;
// }

// 主协程创建函数
Coroutine::Coroutine() {
  // main coroutine'id is 0
  m_cor_id = 0;
  t_coroutine_count++;
  memset(&m_coctx, 0, sizeof(m_coctx));
  t_cur_coroutine = this;
  // DebugLog << "coroutine[" << m_cor_id << "] create";
}

Coroutine::Coroutine(int size, char* stack_ptr) : m_stack_size(size), m_stack_sp(stack_ptr) {
  assert(stack_ptr);

  if (!t_main_coroutine) {
    // 创建主协程
    t_main_coroutine = new Coroutine();
  }

  m_cor_id = t_cur_coroutine_id++;
  t_coroutine_count++;
  // DebugLog << "coroutine[" << m_cor_id << "] create";
}

Coroutine::Coroutine(int size, char* stack_ptr, std::function<void()> cb)
  : m_stack_size(size), m_stack_sp(stack_ptr) {

  assert(m_stack_sp);
  
  if (!t_main_coroutine) {
    t_main_coroutine = new Coroutine();
  }

  // 创建回调函数
  setCallBack(cb);
  m_cor_id = t_cur_coroutine_id++;
  t_coroutine_count++;
  // DebugLog << "coroutine[" << m_cor_id << "] create";
}

// 设置cb相当于能够启用协程
// 设立协程的下一个执行地址
bool Coroutine::setCallBack(std::function<void()> cb) {

  if (this == t_main_coroutine) {
    ErrorLog << "main coroutine can't set callback";
    return false;
  }
  if (m_is_in_cofunc) {
    ErrorLog << "this coroutine is in CoFunction";
    return false;
  }

  // 类似线程的协程回调函数
  m_call_back = cb;

  // assert(m_stack_sp != nullptr);

  // 类似线程的协程栈
  char* top = m_stack_sp + m_stack_size;
  // first set 0 to stack
  // memset(&top, 0, m_stack_size);

  // & -16LL 操作取最低四位的地址
  // 那么高地址相当于stack，低地址相当于区间段
  top = reinterpret_cast<char*>((reinterpret_cast<unsigned long>(top)) & -16LL);

  // 设置寄存器
  memset(&m_coctx, 0, sizeof(m_coctx));

  // 栈顶
  m_coctx.regs[kRSP] = top;
  // 栈底
  m_coctx.regs[kRBP] = top;
  // 下一个要执行的命令，也就是回调函数
  m_coctx.regs[kRETAddr] = reinterpret_cast<char*>(CoFunction); 
  // 第一个参数位置
  m_coctx.regs[kRDI] = reinterpret_cast<char*>(this);

  m_can_resume = true;

  return true;

}

Coroutine::~Coroutine() {
  t_coroutine_count--;
  // DebugLog << "coroutine[" << m_cor_id << "] die";
}

Coroutine* Coroutine::GetCurrentCoroutine() {
  if (t_cur_coroutine == nullptr) {
    // 创建主协程后至少设置有一个协程
    t_main_coroutine = new Coroutine();
    t_cur_coroutine = t_main_coroutine;
  }
  return t_cur_coroutine;
}

Coroutine* Coroutine::GetMainCoroutine() {
  if (t_main_coroutine) {
    return t_main_coroutine;
  }
  t_main_coroutine = new Coroutine();
  return t_main_coroutine;
}

bool Coroutine::IsMainCoroutine() {
  if (t_main_coroutine == nullptr || t_cur_coroutine == t_main_coroutine) {
    return true;
  }
  return false;
}

/********
form target coroutine back to main coroutine
********/
void Coroutine::Yield() {
  // if (!t_enable_coroutine_swap) {
  //   ErrorLog << "can't yield, because disable coroutine swap";
  //   return;
  // }
  if (t_main_coroutine == nullptr) {
    ErrorLog << "main coroutine is nullptr";
    return;
  }

  if (t_cur_coroutine == t_main_coroutine) {
    // ???
    // 主协程一定不能退出?
    ErrorLog << "current coroutine is main coroutine";
    return;
  }
  Coroutine* co = t_cur_coroutine;
  t_cur_coroutine = t_main_coroutine;
  t_cur_run_time = NULL;
  // 从用户协程退出后 回到主协程
  coctx_swap(&(co->m_coctx), &(t_main_coroutine->m_coctx));
  // DebugLog << "swap back";
}

/********
form main coroutine switch to target coroutine
********/
void Coroutine::Resume(Coroutine* co) {
  // 只有主协程才可以复用其他协程!
  // 创建主协程后，在主协程基础上Resume(co)可以切换至新的协程
  if (t_cur_coroutine != t_main_coroutine) {
    ErrorLog << "swap error, current coroutine must be main coroutine";
    return;
  }

  if (!t_main_coroutine) {
    ErrorLog << "main coroutine is nullptr";
    return;
  }
  if (!co || !co->m_can_resume) {
    ErrorLog << "pending coroutine is nullptr or can_resume is false";
    return;
  }

  if (t_cur_coroutine == co) {
    DebugLog << "current coroutine is pending cor, need't swap";
    return;
  }

  t_cur_coroutine = co;
  t_cur_run_time = co->getRunTime();

  // 复用协程后切换到要复用的协程上
  coctx_swap(&(t_main_coroutine->m_coctx), &(co->m_coctx));
  // DebugLog << "swap back";

}

}
