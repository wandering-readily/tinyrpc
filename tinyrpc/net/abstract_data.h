#ifndef TINYRPC_NET_ABSTRACT_DATA_H
#define TINYRPC_NET_ABSTRACT_DATA_H

#include <memory>

namespace tinyrpc {

class AbstractData {

 public:
  typedef std::shared_ptr<AbstractData> sptr;

 public:
  AbstractData() = default;
  virtual ~AbstractData() {};

  bool decode_succ {false};
  bool encode_succ {false};
};


}

#endif