#include <string>

namespace SrvHttp {
  class Connection {
  public:
    int fd;
    char *buf;
    char *rp;
    char *ep;
    std::string name;

  public:
    int Available();
    int FillBuffer();
    int ReadLine(char*, int);
    char** ReadMessageHeaders();
    void Close();

    Connection(int _fd, std::string _name)
      : fd(_fd), buf(new char[8192]), rp(buf), ep(buf), name(_name) {}
  };

  void ServeHTTP(Connection&);
} // namespace SrvHttp
