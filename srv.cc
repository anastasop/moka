#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>
#include <iostream>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "srvhttp.h"

DEFINE_int32(port, 8080, "server port");
DEFINE_string(exec, "/bin/cat", "program to execute");
DEFINE_int32(backlog, 5, "listening socket backlog");
DEFINE_bool(http, false, "enable http server");
DEFINE_string(awk, "", "awk one liner");
DEFINE_int32(num_workers, 4, "number of http workers to prefork");

void handle_sigchld(int signo) {
  LOG(INFO) << "handling SIGCHLD";
  int status = 0;
  pid_t pid = 0;

  for (;;) {
    pid = waitpid(-1, &status, WNOHANG);
    if (pid == 0) {
      break;
    } else if (pid > 0) {
      LOG(INFO) << "child " << pid << " exited";
      break;
    } else {
      PLOG(ERROR) << "waitpid failed";
      break;
    }
  }
  // TODO use sigaction when installing handler
  ::signal(SIGCHLD, &handle_sigchld);
}

void InstallSignalHandlers() {
  // TODO use sigaction
  ::signal(SIGCHLD, &handle_sigchld);
}

// this will run in a separate process
void HTTPWorker(int listenfd) {
  for (;;) {
    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(listenfd, &sockets);

    int status = ::select(listenfd + 1, &sockets, 0, 0, 0);
    if (status == -1) {
      if (errno == EINTR) {
	continue;
      }
      PLOG(FATAL) << "select failed";
    }

    if (!FD_ISSET(listenfd, &sockets)) {
      LOG(ERROR) << "select returned but listenfd is not set";
      continue;
    }

    struct sockaddr_in peeraddr;
    int peeraddr_len = sizeof(peeraddr);
    ::memset(&peeraddr, 0, peeraddr_len);
    int connfd = ::accept(listenfd, (struct sockaddr*)&peeraddr, (socklen_t*)&peeraddr_len);
    if (connfd == -1) {
      PLOG(ERROR) << "accept failed";
      continue;
    }

    LOG(INFO) << "accepted connection from " << inet_ntoa(peeraddr.sin_addr) << ":" << ntohl(peeraddr.sin_port);
    std::stringstream ss;
    ss << "Worker-" << ::getpid();
    SrvHttp::Connection conn(connfd, ss.str());
    SrvHttp::ServeHTTP(conn);
    conn.Close();
    LOG(INFO) << "Worker finished job, waiting next one";
  }
}

int main(int argc, char **argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  InstallSignalHandlers();

  int listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd == -1) {
    PLOG(FATAL) << "socket failed";
  }

  struct sockaddr_in servaddr;
  ::memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons((int)FLAGS_port);
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (::bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) == -1) {
    PLOG(FATAL) << "bind failed";
  }
  if (::listen(listenfd, FLAGS_backlog) == -1) {
    PLOG(FATAL) << "listen failed";
  }

  LOG(INFO) << "server started listening at port " << FLAGS_port;

  if (FLAGS_http) {
    int nworkers = FLAGS_num_workers;
    for (int i = 0; i < nworkers; i++) {
      LOG(INFO) << "forking worker " << i + 1;
      pid_t pid = ::fork();
      if (pid == -1) {
	PLOG(ERROR) << "fork failed";
      } else if (pid == 0) { // child
	HTTPWorker(listenfd);
	// can't get here, worker is a select(2) loop
	::exit(0);
      } else { // parent
	LOG(INFO) << "worker pid is: " << pid;
      }
    }
    LOG(INFO) << "pausing master";
    // TODO make master respond to SIGUSR to display workers statuses
    for (;;) {
      ::pause();
    }
    ::exit(0);
  }

  //
  // non-http server. Fork a child for each request
  // the program should read from stdin and writes to stdout
  //
  struct sockaddr_in peeraddr;
  ::memset(&peeraddr, 0, sizeof(peeraddr));
  int peeraddr_len = sizeof(peeraddr);
  for (;;) {
    int connfd = ::accept(listenfd, (struct sockaddr*)&peeraddr, (socklen_t*)&peeraddr_len);
    if (connfd == -1) {
      if (errno == EINTR) {
	continue;
      }
      PLOG(FATAL) << "accept failed";
    }
    LOG(INFO) << "accepted connection from " << ::inet_ntoa(peeraddr.sin_addr) << ":" << ntohl(peeraddr.sin_port); 
    LOG(INFO) << "forking child to serve client";
    pid_t pid = ::fork();
    if (pid > 0) { // parent
      LOG(INFO) << "forked child " << pid;
      ::close(connfd);
    } else if (pid == -1) {
      PLOG(ERROR) << "fork failed";
    } else if (pid == 0) { // child
      ::close(listenfd);
      if (::dup2(connfd, 0) == -1) {
	LOG(FATAL) << "dup2 failed";
      }
      if (::dup2(connfd, 1) == -1) {
	LOG(FATAL) << "dup2 failed";
      }
      if (::dup2(connfd, 2) == -1) {
	LOG(FATAL) << "dup2 failed";
      }
      const char *execName = 0, *execPath = 0, *execArg = 0;
      if (FLAGS_awk != "") {
	execName = "awk";
	execPath = "/usr/bin/awk";
	execArg = FLAGS_awk.c_str();
      } else {
	execName = FLAGS_exec.c_str();
	execPath = FLAGS_exec.c_str();
	execArg = 0;
      }
      if (::execl(execPath, execName, execArg, 0) == -1) {
	PLOG(ERROR) << "exec failed";
	::exit(2);
      }
    } 
  }
	
  return 0;
}

