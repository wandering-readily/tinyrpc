#ifndef TINYRPC_NET_HTTP_HTTP_SERVLET_H
#define TINYRPC_NET_HTTP_HTTP_SERVLET_H

#include <memory>
#include "tinyrpc/net/http/http_request.h"
#include "tinyrpc/net/http/http_response.h"

namespace tinyrpc {

class CoroutinePool;
class IOThreadPool;

class HttpServlet : public std::enable_shared_from_this<HttpServlet> {
 public:
  typedef std::shared_ptr<HttpServlet> ptr;

  HttpServlet();

  virtual ~HttpServlet();

  virtual void handle(HttpRequest* req, HttpResponse* res) = 0;

  virtual std::string getServletName() = 0;

  void handleNotFound(HttpRequest* req, HttpResponse* res);

  void setHttpCode(HttpResponse* res, const int code);
  
  void setHttpContentType(HttpResponse* res, const std::string& content_type);
  
  void setHttpBody(HttpResponse* res, const std::string& body);

  void setCommParam(HttpRequest* req, HttpResponse* res);

};


class NotFoundHttpServlet: public HttpServlet {
 public:

  NotFoundHttpServlet();

  ~NotFoundHttpServlet();

  void handle(HttpRequest* req, HttpResponse* res);

  std::string getServletName();

};


class AsyncHttpServlet : public HttpServlet {
 public:

  AsyncHttpServlet(std::weak_ptr<CoroutinePool> corPool, \
    std::weak_ptr<IOThreadPool> threadPool) \
    : weakCorPool_(corPool), 
    weakIOThreadPool_(threadPool) {}

  std::weak_ptr<CoroutinePool> getWeakCoroutinePool() {return weakCorPool_;}
  std::weak_ptr<IOThreadPool> getweakIOThreadPool() {return weakIOThreadPool_;}

private:
  std::weak_ptr<CoroutinePool> weakCorPool_;

  std::weak_ptr<IOThreadPool> weakIOThreadPool_;

};


}


#endif
