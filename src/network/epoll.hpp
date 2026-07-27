#pragma once

#include <cstdint>

int create_epoll(const Connection *conn, int STDIN_FD);
int epoll_register(int epoll_fd, int fd, uint32_t events);
int epoll_fd_add(int epoll_fd, int fd, uint32_t events);
int epoll_fd_mod(int epoll_fd, int fd, uint32_t events);
int epoll_fd_remove(int epoll_fd, int fd, uint32_t events);
