#pragma once
int initListenFd(unsigned short port);
//����epoll
int epollRun(int lfd);
int acceptClient(int lfd, int epfd);
int revHttp(int cfd, int epfd);
int requestLine(const char*line,int cfd);
int sendFile(const char* filename, int cfd);
//������Ӧͷ
int sendHead(int cfd, int status, const char* descr,const char*type,int length);
const char* getFileType(const char* name);
int sendDir(const char* dirname, int cfd);