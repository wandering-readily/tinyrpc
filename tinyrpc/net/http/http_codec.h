#ifndef TINYRPC_NET_HTTP_HTTP_CODEC_H
#define TINYRPC_NET_HTTP_HTTP_CODEC_H

#include <map>
#include <string>
#include "tinyrpc/net/abstract_data.h"
#include "tinyrpc/net/abstract_codec.h"
#include "tinyrpc/net/http/http_request.h"

/* 
 * 解析HTTP/1.0 HTTP/1.1 request内容
 * HttpRequestLine  ==>
 * 请求方法    空格
 * 资源路径    空格
 * HTTP版本
 * \r\n (回车换行CRLF)
 * GET http://www.xyz.edu.com/dir/index.htm HTTP/1.1
 *
 * HttpRequestHeader  ==>
 * key:value\r\n 
 * key:value\r\n 
 * key:value\r\n 
 * ...
 * \r\n\r\n
 * 
 * HttpRequestContent  ==>
 * requeset_header["Content-Length"]决定
 * 
 * 
 * 
 * HTTP_response (request和response HTTP_VERSION等HTTP协议内容 应该一样)  ==> 第一行
 * HTTP_VERSION HTTP_RESPONSE_CODE HTTP_RESPONSE_INFO\r\n
 * 
 * HTTP_RESPONSE_HEADER ==>
 * key:value\r\n 
 * key:value\r\n 
 * key:value\r\n 
 * ...
 * \r\n\r\n
 * 
 * HTTP_BODY  ==>
 * ...
  */
namespace tinyrpc {

class HttpCodeC : public AbstractCodeC {
 public:
  HttpCodeC();

  ~HttpCodeC();

  void encode(TcpBuffer* buf, AbstractData* data);
  
  void decode(TcpBuffer* buf, AbstractData* data);

  ProtocalType getProtocalType();

 private:
  bool parseHttpRequestLine(HttpRequest* requset, const std::string& tmp);
  bool parseHttpRequestHeader(HttpRequest* requset, const std::string& tmp);
  bool parseHttpRequestContent(HttpRequest* requset, const std::string& tmp);
};

} 


#endif
