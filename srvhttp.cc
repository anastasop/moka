#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "srvhttp.h"

DEFINE_string(www_root, "/home/spyros/www", "web root. Must not end with /");

namespace SrvHttp {

const int BUFSIZE = 8192;
const int MESGLEN = 1024;
const int MAXHLINES = 64;

void Connection::Close() {
  if (::close(fd) != 0) {
    PLOG(ERROR) << "close failed";
  }
}

int Connection::Available() {
	return ep - rp;
}

int Connection::FillBuffer() {
  int nr;
  if ((nr = Available()) > 0) {
    return nr;
  }
  nr = ::read(fd, buf, BUFSIZE);
  if(nr < 0) {
    PLOG(ERROR) << "FillBuffer.Read failed";
    return -1;
  }
  rp = buf;
  ep = buf + nr;
  return nr;
}

int Connection::ReadLine(char *p, int n) {
  char c0, c;
  int i;

  for (c0 = '*', i = 0; i < n; c0 = c, i++) {
    if (Available() == 0) {
      int s = FillBuffer();
      if (s == 0 || s == -1) {
	return s;
      }
    }
    c = *rp++;
    if (c0 == '\r' && c == '\n') {
      break;
    }
    *p++ = c;
  }
  if (i >= n) {
    LOG(ERROR) << "ReadLine: http error: message too long";
    return -1;
  }
  p[-1] = '\0';
  return i;
}

// TODO leaks memory in case of errors
char** Connection::ReadMessageHeaders() {
  int na = MESGLEN;
  char* s = new char[MESGLEN];
  char** lines = new char*[MAXHLINES];
  int nl = 0;
  for (;;) {
    if (nl >= 64) {
      LOG(ERROR) << "ReadMessageHeader: too many headers";
      return 0;
    }
    int n = ReadLine(s, na);
    if (n == -1) {
      return 0;
    }
    if (n == 0) {
      if (nl > 0) {
	LOG(ERROR) << "http error: unexpected eof in headers";
      }
      return 0;
    }
    if (n == 1) {
      break; /* what about empty lines at start? */
    }
    if (nl > 0 && *s == ' ' || *s == '\t') {
      s[-1] = ' ';
    } else {
      lines[nl++] = s;
    }
    s += n;
    na -= n;
  }
  if (nl == 0) {
    return 0;
  }
  lines[nl] = 0;
  return lines;
}

std::string StatusMessage(int code) {
  switch(code){
  default:  return "GOK";
  case 100: return "Continue";
  case 101: return "Switching protocols";
  case 200: return "OK";
  case 201: return "Created";
  case 202: return "Accepted";
  case 203: return "Non-Authoritative Information";
  case 204: return "No Content";
  case 205: return "Reset Content";
  case 206: return "Partial Content";
  case 207: return "Partial Update OK";
  case 300: return "Multiple Choices";
  case 301: return "Moved Permanently";
  case 302: return "Moved Temporarily";
  case 303: return "See Other";
  case 304: return "Not Modified";
  case 305: return "Use Proxy";
  case 307: return "Temporary Redirect";
  case 400: return "Bad Request";
  case 401: return "Unauthorized";
  case 402: return "Payment Required";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 406: return "Not Acceptable";
  case 407: return "Proxy Authentication Required";
  case 408: return "Request Timeout";
  case 409: return "Conflict";
  case 410: return "Gone";
  case 411: return "Length Required";
  case 412: return "Precondition Failed";
  case 413: return "Request Entity Too Large";
  case 414: return "Request-URI Too Long";
  case 415: return "Unsupported Media Type";
  case 416: return "Requested range not satisfiable";
  case 417: return "Expectation Failed";
  case 418: return "Reauthentication Required";
  case 419: return "Proxy Reauthentication Required";
  case 500: return "Internal Server Error";
  case 501: return "Not Implemented";
  case 502: return "Bad Gateway";
  case 503: return "Service Unavailable";
  case 504: return "Gateway Timeout";
  case 505: return "HTTP Version Not Supported";
  case 506: return "Partial Update Not Implemented";
  }
}

std::string weekday[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

std::string month[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void ReplyTextMessage(Connection& conn, int code, std::string msg) {
  std::stringstream ss;
  ss << "HTTP/1.1 " << code << " " << StatusMessage(code) << "\r\n";
  ss << "Connection: close" << "\r\n";
  ss << "Server: spysrv" << "\r\n";
  ss << "X-Srv-Connection: " << conn.name << "\r\n";
  std::time_t now = std::clock();
  struct tm* tm = std::gmtime(&now);
  ss << "Date: "
     << weekday[tm->tm_wday] << ", "
     << std::setw(2) << std::setfill('0') << tm->tm_mday << " "
     << month[tm->tm_mon] << " " << tm->tm_year + 1900 << " "
     << std::setw(2) << std::setfill('0') << tm->tm_hour << ":"
     << std::setw(2) << std::setfill('0') << tm->tm_min << ":"
     << std::setw(2) << std::setfill('0') << tm->tm_sec << " GMT"
     << "\r\n";
  ss << "Content-Type: text/plain" << "\r\n";
  ss << "\r\n";
  ss << msg << "\r\n";
  ss << std::ends;
  const char* data = ss.str().c_str();
  if (::write(conn.fd, data, std::strlen(data)) < 0) {
    PLOG(ERROR) << "write failed";
  }
}

void ServeFile(Connection& conn, int code, char* path, std::string root) {
  const char* absPath = (root + std::string(path)).c_str();
  int ffd = ::open(absPath, O_RDONLY);
  if (ffd < 0) {
    ReplyTextMessage(conn, 400, "open failed: " + std::string(strerror(errno)));
    return;
  }

  std::stringstream ss;
  ss << "HTTP/1.1 " << code << " " << StatusMessage(code) << "\r\n";
  ss << "Connection: close" << "\r\n";
  ss << "Server: spysrv" << "\r\n";
  ss << "X-Srv-Connection: " << conn.name << "\r\n";
  std::time_t now = std::clock();
  struct tm* tm = std::gmtime(&now);
  ss << "Date: "
     << weekday[tm->tm_wday] << ", "
     << std::setw(2) << std::setfill('0') << tm->tm_mday << " "
     << month[tm->tm_mon] << " " << tm->tm_year + 1900 << " "
     << std::setw(2) << std::setfill('0') << tm->tm_hour << ":"
     << std::setw(2) << std::setfill('0') << tm->tm_min << ":"
     << std::setw(2) << std::setfill('0') << tm->tm_sec << " GMT"
     << "\r\n";
  // TODO implement sth, rather than let browser decide
  //  ss << "Content-Type: " << FileMediaType(path) << "\r\n";
  ss << "\r\n";

  // TODO send above http header with sendfile and check errors
  ::sendfile(ffd, conn.fd, 0, 0, 0, 0, 0);
}

static char RootPath[] = {'/', '\0'};

char* ExtractFilePathFromUrl(char* s) {
  if (s == 0) {
    return RootPath;
  }
  if (strncmp(s, "http://", 7) == 0 || strncmp(s, "HTTP://", 7) == 0) {
    s += 7;
  }
  while (*s != '\0' && *s != '/' && *s != '?') {
    s++;
  }
  if (*s != '/') {
    return RootPath;
  }
  char* t = s;
  while (*s != '\0' && *s != '?') {
    s++;
  }
  *s = '\0';
  // TODO normalize the url
  return t;
}

void ServeHTTP(Connection& conn) {
  char** rawMessage = conn.ReadMessageHeaders();
  if (rawMessage == 0) {
    return;
  }

  // parse request line
  char* tokens[3] = { 0, 0, 0 };
  char* s = rawMessage[0];
  char* t = rawMessage[0];
  while (*t != '\0' && *t != ' ' && *t != '\t') {
    t++;
  }
  *t = '\0';
  tokens[0] = s; // HTTP Method GET, POST etc
  s = ++t;

  while (*t != '\0' && *t != ' ' && *t != '\t') {
    t++;
  }
  *t = '\0';
  tokens[1] = s; // URL
  s = ++t;

  while (*t != '\0' && *t != ' ' && *t != '\t') {
    t++;
  }
  *t = '\0';
  tokens[2] = s; // HTTP version, usually 'HTTP/1.1'

  // tokens[0] is method. Only GET is supported
  if (strcmp(tokens[0], "GET") != 0) {
    ReplyTextMessage(conn, 405, "Only GET is supported");
  }
  // tokens[2] is the protocol version. Currently it doesn't matter

  // tokens[1] is the URL. it may be absolute or relative
  char* path = ExtractFilePathFromUrl(tokens[1]);
  LOG(INFO) << "serving url: " << path;
  ServeFile(conn, 200, path, FLAGS_www_root);
}


} // namespace SrvHttp
