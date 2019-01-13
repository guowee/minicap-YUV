#include "SimpleServer.hpp"

#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
SimpleServer::SimpleServer(): mFd(0) {
}

SimpleServer::~SimpleServer() {
  if (mFd > 0) {
    ::close(mFd);
  }
}

int
SimpleServer::start(const char* sockname) {
int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0){
		return sfd;
	}
	// 非阻塞式发送
	/*
	int sendTimeout = 5 * 1000;
	struct timeval tv = { 10, 0 };
	setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
	tv.tv_sec = sendTimeout / 1000;
	tv.tv_usec = (sendTimeout % 1000) * 1000;
	setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
    */



	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;//协议
	addr.sin_addr.s_addr = htonl(INADDR_ANY);//IP地址
	addr.sin_port = htons(9999);//端口号

	int reuseaddr = 1;
	if (::setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) < 0) {
		perror("reuseaddr error.");
		goto close_fd;
	}

	if (::bind(sfd, (struct sockaddr*) &addr, sizeof(addr)) < 0){
		perror("bind error.");
		goto close_fd;
	}

	if (::listen(sfd, 1) < 0){//开始监听，只支持连接一个client对象
		perror("listen error.");
		goto close_fd;
	}

	mFd = sfd;
	return mFd;
close_fd:
	::close(sfd);
	return -1;
}

int
SimpleServer::accept() {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  return ::accept(mFd, (struct sockaddr *) &addr, &addr_len);
}
