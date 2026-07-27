#pragma once

#include <cstdint>

int create_epoll(const Connection *conn, int STDIN_FD);
bool epoll_register(int epoll_fd, int fd, uint32_t events);
bool epoll_fd_add(int epoll_fd, int fd, uint32_t events);
bool epoll_fd_mod(int epoll_fd, int fd, uint32_t events);
bool epoll_fd_remove(int epoll_fd, int fd, uint32_t events);
