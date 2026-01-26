#pragma once

#include <vector>

#if defined(__linux__)
#include <unistd.h>
#include <sys/epoll.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#endif

namespace server {
class SockSelector {
public:
#ifdef _WIN32
    using socket_t = SOCKET;
#else
    using socket_t = int;
#endif

private:
    std::vector<socket_t> readable_socks;

#ifdef __linux__
    int epoll_fd;
    std::vector<struct epoll_event> events;
#else
    fd_set read_fd_set;
    std::vector<socket_t> read_fds;
#ifndef _WIN32
    socket_t highest_fd;
#else
    constexpr static socket_t highest_fd = 0;
#endif
#endif

public:
    SockSelector();
    ~SockSelector();

    // Zero-timeout means potentially forever-blocking
    bool run(unsigned timeout_ms = 0);

    bool add_read_fd(socket_t fd);
    void remove_read_fd(socket_t fd);

    const std::vector<socket_t>& get_readable_socks() { return readable_socks; }
};
} // namespace server
