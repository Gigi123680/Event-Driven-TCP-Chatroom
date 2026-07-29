#pragma once

#include <cstdint>

#include "../connection/connection.hpp"

int create_epoll(const Connection *conn, int STDIN_FD);
int create_epoll(int socket_fd, int STDIN_FD);
bool epoll_register(int epoll_fd, int fd, uint32_t events);
bool epoll_fd_add(int epoll_fd, int fd, uint32_t events);
bool epoll_fd_mod(int epoll_fd, int fd, uint32_t events);
bool epoll_fd_remove(int epoll_fd, int fd, uint32_t events);
