#include <iostream>
#include <google/protobuf/service.h>
#include "tinyrpc/net/tinypb/tinypb_rpc_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_async_channel.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_controller.h"
#include "tinyrpc/net/tinypb/tinypb_rpc_closure.h"
#include "tinyrpc/net/net_address.h"
#include "test_tinypb_server.pb.h"

#include "tinyrpc/comm/client.hpp"
#include "tinyrpc/comm/asyncCorClient.hpp"

void test_client() {

  // tinyrpc::IPAddress::sptr addr = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 39999);
  tinyrpc::IPAddress::sptr addr = std::make_shared<tinyrpc::IPAddress>("127.0.0.1", 20000);

  tinyrpc::TinyPbRpcChannel channel(addr);
  QueryService_Stub stub(&channel);

  tinyrpc::TinyPbRpcController rpc_controller;
  rpc_controller.SetTimeout(5000);

  queryAgeReq rpc_req;
  queryAgeRes rpc_res;

  std::cout << "Send to tinyrpc server " << addr->toString() << ", requeset body: " << rpc_req.ShortDebugString() << std::endl;
  stub.query_age(&rpc_controller, &rpc_req, &rpc_res, NULL);

  if (rpc_controller.ErrorCode() != 0) {
    std::cout << "Failed to call tinyrpc server, error code: " << rpc_controller.ErrorCode() << ", error info: " << rpc_controller.ErrorText() << std::endl; 
    return;
  }

  std::cout << "Success get response from tinyrpc server " << addr->toString() << ", response body: " << rpc_res.ShortDebugString() << std::endl;

} 

int main(int argc, char* argv[]) {

  // test_client();

  // tinyrpc::TinyrpcClient<tinyrpc::IPAddress> client{
      // std::string("127.0.0.1"), (uint16_t)20000};
  // queryAgeReq rpc_req;
  // queryAgeRes rpc_res;

  // client.Call<QueryService>("query_age", &rpc_req, &rpc_res);


  // auto rpc_req2 = std::make_shared<queryAgeReq>();
  // auto rpc_res2 = std::make_shared<queryAgeRes>();
  // tinyrpc::AsyncCor_TinyrpcClient<tinyrpc::IPAddress> async_client(
    // std::string("127.0.0.1"), (uint16_t)(20000), 0, 0);
  // {
  // auto asyncer = async_client.Async_Call<QueryService>("query_age", rpc_req2, rpc_res2);
  // asyncer->wait();
  // }
  // std::cout << "response body: " << rpc_res2->ShortDebugString() << std::endl;


  tinyrpc::TinyrpcLongLiveClient client;
  auto peer_addr = client.addRpcClientAddr<tinyrpc::IPAddress>(std::string("127.0.0.1"), (uint16_t)(20000));
  for (size_t i = 0; i < 10000; i++) {
    auto rpc_req3 = std::make_shared<queryAgeReq>();
    auto rpc_res3 = std::make_shared<queryAgeRes>();

    client.Call<QueryService>("query_age", rpc_req3.get(), rpc_res3.get(), peer_addr);

    std::cout << "response body: " << rpc_res3->ShortDebugString() <<  "call times" << i+1 << "\n";
  }

  return 0;
}
