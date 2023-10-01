#include <map>
#include <string>
#include "tinyrpc/comm/string_util.h"
#include "tinyrpc/comm/log.h"


namespace tinyrpc {

void StringUtil::SplitStrToMap(const std::string& str, const std::string& split_str, 
    const std::string& joiner, std::map<std::string, std::string>& res) {

  if (str.empty() || split_str.empty() || joiner.empty()) {
    DebugLog << "str or split_str or joiner_str is empty";
    return;
  }
  const std::string &tmp = str;

  // [([key_str, join_str, value_str],  split_str),...  [key_str, join_str, value_str]]
  // 最后一项没有split_str
  std::vector<std::string> vec;
  SplitStrToVector(tmp, split_str, vec);
  for (auto i : vec) {
    if (!i.empty()) {
      size_t j = i.find_first_of(joiner);
      if (j != i.npos && j != 0) {
        std::string key = i.substr(0, j);
        std::string value = i.substr(j + joiner.length(), i.length() - j - joiner.length());
        DebugLog << "insert key = " << key << ", value=" << value;
        res[key.c_str()] = value;
      }
    }
  }

}

void StringUtil::SplitStrToVector(const std::string& str, const std::string& split_str, 
    std::vector<std::string>& res) {

  if (str.empty() || split_str.empty()) {
    // DebugLog << "str or split_str is empty";
    return;
  }
  // 这里已经拷贝了，因此上一级函数不用拷贝std::string str
  std::string tmp = str;
  // 这里保证如果最后一段不是split_str，那么结果会把str放入vector<std::string> &res
  if (tmp.substr(tmp.length() - split_str.length(), split_str.length()) != split_str) {
    tmp += split_str;
  }

  while (1) {
    size_t i = tmp.find_first_of(split_str);
    if (i == tmp.npos) {
      return;
    }
    int l = tmp.length();
    std::string x = tmp.substr(0, i);
    tmp = tmp.substr(i + split_str.length(), l - i - split_str.length());
    if (!x.empty()) {
      res.push_back(std::move(x));
    }
  }

}


}