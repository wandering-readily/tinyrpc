#include <google/protobuf/service.h>
#include <memory>
#include "tinyrpc/net/http/http_dispatcher.h"
#include "tinyrpc/net/http/http_request.h"
#include "tinyrpc/net/http/http_servlet.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/msg_req.h"


namespace tinyrpc {

// 分发，派发
void HttpDispacther::dispatch(AbstractData* data, TcpConnection* conn) {
  HttpRequest* resquest = dynamic_cast<HttpRequest*>(data);
  HttpResponse response;
  // 从http接收到的任务，从这生成msg_req_no
  Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no = MsgReqUtil::genMsgNumber();
  setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());

  RpcInfoLog << "begin to dispatch client http request, msgno=" << Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no;

  // 转发处理方式
  std::string url_path = resquest->m_request_path;
  if (!url_path.empty()) {
    // url_path是service处理方法字段
    auto it = m_servlets.find(url_path);
    if (it == m_servlets.end()) {
      RpcErrorLog << "404, url path{ " << url_path << "}, msgno=" << Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no;
      NotFoundHttpServlet servlet;
      Coroutine::GetCurrentCoroutine()->getRunTime()->m_interface_name = servlet.getServletName();
      servlet.setCommParam(resquest, &response);
      servlet.handle(resquest, &response);
    } else {

      Coroutine::GetCurrentCoroutine()->getRunTime()->m_interface_name = it->second->getServletName();
      it->second->setCommParam(resquest, &response);
      it->second->handle(resquest, &response);
    }
  }

  // 写入发送内容
  conn->getCodec()->encode(conn->getOutBuffer(), &response);

  RpcInfoLog << "end dispatch client http request, msgno=" << Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no;

}

// 注册不同URL PATH的不同处理方法
void HttpDispacther::registerServlet(const std::string& path, HttpServlet::sptr servlet) {
  auto it = m_servlets.find(path);
  if (it == m_servlets.end()) {
    RpcDebugLog << "register servlet success to path {" << path << "}";
    m_servlets[path] = servlet;
  } else {
    RpcErrorLog << "failed to register, beacuse path {" << path << "} has already register sertlet";
  }
}




}