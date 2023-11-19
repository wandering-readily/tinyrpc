#include <unistd.h>
#include <string.h>
#include "tinyrpc/net/tcp/tcp_buffer.h"
#include "tinyrpc/comm/log.h"


namespace tinyrpc {

TcpBuffer::TcpBuffer(int size) {
  assert(size != 0);
  m_buffer = std::vector<char> (size);
	// m_buffer.resize(size);	
}


TcpBuffer::~TcpBuffer() {

}

int TcpBuffer::readAble() {

	return m_write_index - m_read_index;
}

int TcpBuffer::writeAble() {
	
	return m_buffer.size() - m_write_index;
}

int TcpBuffer::readIndex() const {
  return m_read_index;
}

int TcpBuffer::writeIndex() const {
  return m_write_index;
}

// int TcpBuffer::readFromSocket(int sockfd) {
	// if (writeAble() == 0) {
		// m_buffer.resize(2 * m_size);
	// }
	// int rt = read(sockfd, &m_buffer[m_write_index], writeAble());
	// if (rt >= 0) {
		// m_write_index += rt;
	// }
	// return rt;
// }

// 会损失已读的字符
void TcpBuffer::resizeBuffer(int size) {
  std::vector<char> tmp(size);
  int c = std::min(size, readAble());
  memcpy(&tmp[0], &m_buffer[m_read_index], c);

  m_buffer.swap(tmp);
  m_read_index = 0;
  m_write_index = m_read_index + c;

}

void TcpBuffer::writeToBuffer(const char* buf, int size) {
	if (size > writeAble()) {
    // 新的buffer_size = 1.5 * (m_write_index + write_size)
    // 保证了new_size > readAble()
    int new_size = (int)(1.5 * (m_write_index + size));
    // resizeBuffer()会损失已经读的字符
		resizeBuffer(new_size);
	}
	memcpy(&m_buffer[m_write_index], buf, size);
	m_write_index += size;

}


void TcpBuffer::readFromBuffer(std::vector<char>& re, int size) {
  if (readAble() == 0) {
    RpcDebugLog << "read buffer empty!";
    return; 
  }
  int read_size = readAble() > size ? size : readAble();
  std::vector<char> tmp(read_size); 

  // std::copy(m_read_index, m_read_index + read_size, tmp);
  memcpy(&tmp[0], &m_buffer[m_read_index], read_size);
  re.swap(tmp);
  m_read_index += read_size;
  // 调整已读字符，已读字符会被删除
  adjustBuffer();

}

void TcpBuffer::adjustBuffer() {
  if (m_read_index > static_cast<int>(m_buffer.size() / 3)) {
    
    std::vector<char> new_buffer(m_buffer.size());

    int count = readAble();
    // std::copy(&m_buffer[m_read_index], readAble(), &new_buffer);
    memcpy(&new_buffer[0], &m_buffer[m_read_index], count);

    m_buffer.swap(new_buffer);
    m_write_index = count;
    m_read_index = 0;
    // new_buffer自身会释放
    // new_buffer.clear();

  }

}

int TcpBuffer::getSize() {
  return m_buffer.size();
}

void TcpBuffer::clearBuffer() {
  m_buffer.clear();
  m_read_index = 0;
  m_write_index = 0;
}

void TcpBuffer::clearIndex() {
  m_read_index = 0;
  m_write_index = 0;
}

// 往前移动index，往前移动后的位置必须小于m_buffer.size()
void TcpBuffer::recycleRead(int index) {
  int j = m_read_index + index;
  if (j > (int)m_buffer.size()) {
    RpcErrorLog << "recycleRead error";
    return;
  }
  m_read_index = j;
  if(m_write_index < m_read_index) {
    m_write_index = m_read_index;
  }
  // 如果移动的位置超过m_write_index呢，count可能变为负数
  adjustBuffer();
}

void TcpBuffer::recycleWrite(int index) {
  int j = m_write_index + index;
  if (j > (int)m_buffer.size()) {
    RpcErrorLog << "recycleWrite error";
    return;
  }
  m_write_index = j;
  adjustBuffer();
}

// const char* TcpBuffer::getBuffer() {
//   char* tmp;
//   memcpy(&tmp, &m_buffer[m_read_index], readAble());
//   return tmp;
// }

std::string TcpBuffer::getBufferString() {
  std::string re(readAble(), '0');
  memcpy(&re[0],  &m_buffer[m_read_index], readAble());
  return re;
}

// !!!
// ???
// 注意这里会拷贝m_buffer内容
std::vector<char> TcpBuffer::getBufferVector() {
  return m_buffer;
}

}
