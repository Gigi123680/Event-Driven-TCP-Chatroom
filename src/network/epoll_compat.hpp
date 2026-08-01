#pragma once

#ifdef __linux__
#include <sys/epoll.h>
#else
#include <cstdint>

#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLRDHUP 0x2000

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union epoll_data {
  int fd;
} epoll_data_t;

struct epoll_event {
  uint32_t events;
  epoll_data_t data;
};

int epoll_create1(int flags);
int epoll_ctl(int epoll_fd, int op, int fd, epoll_event *event);
int epoll_wait(int epoll_fd, epoll_event *events, int maxevents, int timeout);
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
