#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <dirent.h>
#include <string>
int initListenFd(unsigned short port)
{
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
	}
	int opt = 1;
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
	if (ret == -1)
	{
		perror("ret");
	}
	struct sockaddr_in addr;
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	ret = bind(lfd, (struct sockaddr*)&addr, sizeof addr);

	ret = listen(lfd, 128);

	return lfd;
}

int epollRun(int lfd)
{
	int epfd = epoll_create(1);
	if (epfd == -1)
	{
		perror("epfd");
	}
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1)
	{
		perror("epoll ret");
	}
	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(struct epoll_event);
	while (1)
	{
		
		
		int num = epoll_wait(epfd, evs, size, -1);
		for (int i = 0; i < num; ++i)
		{
			int fd = evs[i].data.fd;
			if (fd == lfd)
			{
				//build new connection
				acceptClient(lfd, epfd);
			}
			else
			{
				//receive data
				revHttp(fd, epfd);

			}
		}
	}
	return 0;
}

int acceptClient(int lfd, int epfd)
{
	int cfd = accept(lfd, NULL, NULL);
	if (cfd == -1)
	{
		perror("cfd");
	}

	//设置非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN|EPOLLET;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1)
	{
		perror("epoll ret");
	}
	return 0;
}

int revHttp(int cfd, int epfd)
{
	printf("start receive data");
	char buf[4096] = { 0 };
	char tmp[1024] = { 0 };
	int len = 0,total=0;
	while ((len = recv(cfd, tmp, sizeof tmp,0))>0);
	{
		if (total + len < sizeof buf)
		{
			memcpy(buf+total, tmp, len);
		}
		total += len;
	}
	if (len == -1 && errno == EAGAIN)
	{
		//解析请求行
		char* pt = strstr(buf,"\r\n");
		int relen = pt - buf;
		buf[relen] = '\0';
		requestLine(buf, cfd);
	}
	else if (len == 0)
	{
		epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
		close(cfd);
	}
	return 0;
}

int requestLine(const char* line, int cfd)
{
	//解析请求行
	char a[5];
	char b[1000];
	sscanf(line, "%[^ ] %[^ ]", a, b);
	printf("a:%s,b:%s\n", a, b);
	if (strcasecmp(a, "get")!=0)
	{
		return -1;
	}
	char* file = NULL;
	if (strcmp(b, "/") == 0)
	{
		file = "./";
	}
	else
	{
		file = b + 1;
	}
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		sendHead(cfd, 404, "not found", getFileType(".html"),-1);
		sendFile("404.html", cfd);
		return 0;
	}
	if (S_ISDIR(st.st_mode))
	{
		//发送目录
		sendHead(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else {
		sendHead(cfd, 200, "OK", getFileType(file), st.st_size);
		sendFile(file, cfd);
	}
	return 0;
}

int sendFile(const char* filename, int cfd)
{
	int fd = open(filename, O_WRONLY);
	assert(fd > 0);
	while (1)
	{
		char buf[1024];
		int len = read(fd, buf, sizeof buf);
		if (len > 0)
		{
			send(cfd, buf, len, 0);
			usleep(10);
		}
		else if (len == 0)
		{
			break;
		}
		else
		{
			perror("read");
		}
	}
	
	return 0;
}

int sendHead(int cfd, int status, const char* descr, const char* type, int length)
{
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n",status,descr);
	sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
	sprintf(buf + strlen(buf), "content-length: %d\r\n", length);
	send(cfd, buf, strlen(buf), 0);
	return 0;
}

const char* getFileType(const char* name)
{
	const char* dot = strrchr(name, ',');
	if (dot == NULL)
	{
		return "text/plain; charset=utf-8";
	}
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
	{
		return "text/html; charset=utf-8";
	}
	return nullptr;
}

int sendDir(const char* dirname, int cfd)
{
	char buf[4096] = { 0 };
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirname);
	struct dirent** namelist;
	int num=scandir(dirname, &namelist, NULL,alphasort);
	for (int i = 0; i < num; ++i)
	{
		char* name = namelist[i]->d_name;
		struct stat st;
		char sub[1024] = { 0 };
		sprintf(sub, "%s/%s", dirname, name);
		stat(sub, &st);
		if (S_ISDIR(st.st_mode))
		{
			//a标签 跳转
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", name,name, st.st_size);

		}
		else
		{
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", name, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		free(namelist[i]);
		memset(buf, 0, sizeof(buf));
	}
	sprintf(buf, "</table></body></html>");
	free(namelist);
	return 0;
}
