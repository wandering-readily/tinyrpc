#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>

// #include "../abstract_dispatcher.h"
#include "tinyrpc/net/abstract_dispatcher.h"
// #include "../../comm/error_code.h"
#include "tinyrpc/comm/error_code.h"
#include "tinypb_data.h"
#include "tinypb_rpc_dispatcher.h"
#include "tinypb_rpc_controller.h"
#include "tinypb_rpc_closure.h"
#include "tinypb_codec.h"
// #include "../../comm/msg_req.h"
#include "tinyrpc/comm/msg_req.h"

namespace tinyrpc {

class TcpBuffer;

void TinyPbRpcDispacther::dispatch(AbstractData* data, TcpConnection* conn) {
  TinyPbStruct* tmp = dynamic_cast<TinyPbStruct*>(data);

  if (tmp == nullptr) {
    RpcErrorLog << "dynamic_cast error";
    return;
  }
  // 1. 设置当前协程的RunTime
  // ???
  // 如果当前msg_req和下面问题一样，为空怎么办?
  Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no = tmp->msg_req;
  setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());


  RpcInfoLog << "begin to dispatch client tinypb request, msgno=" << tmp->msg_req;

  std::string service_name;
  std::string method_name;

  // 2. 设置replay.m_msg_no
  TinyPbStruct reply_pk;
  reply_pk.service_full_name = tmp->service_full_name;
  reply_pk.msg_req = tmp->msg_req;
  if (reply_pk.msg_req.empty()) {
    reply_pk.msg_req = MsgReqUtil::genMsgNumber();
  }

  // service_full_name = service_name + "." + method_name
  // 3. 解析service_name, method_name
  if (!parseServiceFullName(tmp->service_full_name, service_name, method_name)) {
    RpcErrorLog << reply_pk.msg_req << "|parse service name " << tmp->service_full_name << "error";

    reply_pk.err_code = ERROR_PARSE_SERVICE_NAME;
    std::stringstream ss;
    ss << "cannot parse service_name:[" << tmp->service_full_name << "]";
    reply_pk.err_info = ss.str();
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  // 4. 寻找已经注册的服务service_ptr
  Coroutine::GetCurrentCoroutine()->getRunTime()->m_interface_name = tmp->service_full_name;
  auto it = m_service_map.find(service_name);
  if (it == m_service_map.end() || !((*it).second)) {
    reply_pk.err_code = ERROR_SERVICE_NOT_FOUND;
    std::stringstream ss;
    ss << "not found service_name:[" << service_name << "]"; 
    RpcErrorLog << reply_pk.msg_req << "|" << ss.str();
    reply_pk.err_info = ss.str();

    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));

    RpcInfoLog << "end dispatch client tinypb request, msgno=" << tmp->msg_req;
    return;

  }

  service_ptr service = (*it).second;

  // 5. google::protobuf操作
  const google::protobuf::MethodDescriptor* method = service->GetDescriptor()->FindMethodByName(method_name);
  if (!method) {
    reply_pk.err_code = ERROR_METHOD_NOT_FOUND;
    std::stringstream ss;
    ss << "not found method_name:[" << method_name << "]"; 
    RpcErrorLog << reply_pk.msg_req << "|" << ss.str();
    reply_pk.err_info = ss.str();
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  google::protobuf::Message* request = service->GetRequestPrototype(method).New();
  RpcDebugLog << reply_pk.msg_req << "|request.name = " << request->GetDescriptor()->full_name();

  if(!request->ParseFromString(tmp->pb_data)) {
    reply_pk.err_code = ERROR_FAILED_SERIALIZE;
    std::stringstream ss;
    ss << "faild to parse request data, request.name:[" << request->GetDescriptor()->full_name() << "]";
    reply_pk.err_info = ss.str();
    RpcErrorLog << reply_pk.msg_req << "|" << ss.str();
    delete request;
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  RpcInfoLog << "============================================================";
  RpcInfoLog << reply_pk.msg_req <<"|Get client request data:" << request->ShortDebugString();
  RpcInfoLog << "============================================================";

  google::protobuf::Message* response = service->GetResponsePrototype(method).New();

  RpcDebugLog << reply_pk.msg_req << "|response.name = " << response->GetDescriptor()->full_name();

  TinyPbRpcController rpc_controller;
  rpc_controller.SetMsgReq(reply_pk.msg_req);
  rpc_controller.SetMethodName(method_name);
  rpc_controller.SetMethodFullName(tmp->service_full_name);

  std::function<void()> reply_package_func = [](){};

  TinyPbRpcClosure closure(reply_package_func);
  service->CallMethod(method, &rpc_controller, request, response, &closure);

  RpcInfoLog << "Call [" << reply_pk.service_full_name << "] succ, now send reply package";

  if (!(response->SerializeToString(&(reply_pk.pb_data)))) {
    reply_pk.pb_data = "";
    RpcErrorLog << reply_pk.msg_req << "|reply error! encode reply package error";
    reply_pk.err_code = ERROR_FAILED_SERIALIZE;
    reply_pk.err_info = "failed to serilize relpy data";
  } else {
    RpcInfoLog << "============================================================";
    RpcInfoLog << reply_pk.msg_req << "|Set server response data:" << response->ShortDebugString();
    RpcInfoLog << "============================================================";
  }

  delete request;
  delete response;

  conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));

}


bool TinyPbRpcDispacther::parseServiceFullName(const std::string& full_name, std::string& service_name, std::string& method_name) {
  if (full_name.empty()) {
    RpcErrorLog << "service_full_name empty";
    return false;
  }
  std::size_t i = full_name.find(".");
  if (i == full_name.npos) {
    RpcErrorLog << "not found [.]";
    return false;
  }

  service_name = full_name.substr(0, i);
  RpcDebugLog << "service_name = " << service_name;
  method_name = full_name.substr(i + 1, full_name.length() - i - 1);
  RpcDebugLog << "method_name = " << method_name;

  return true;

}

void TinyPbRpcDispacther::registerService(service_ptr service) {
  std::string service_name = service->GetDescriptor()->full_name();
  m_service_map[service_name] = service;
  RpcInfoLog << "succ register service[" << service_name << "]!"; 
}

}