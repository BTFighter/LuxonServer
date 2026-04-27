// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "sock_selector.hpp"

#include <algorithm>
#include <tracy/Tracy.hpp>

namespace server {
namespace {
#ifndef __linux__
timeval ms_to_timeval(unsigned long long ms) {
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return tv;
}
#endif
} // namespace

SockSelector::SockSelector() {
#if defined(__linux__) || defined(_WIN32)
    epoll_fd = epoll_create1(0);
    events.resize(128);
#else
    FD_ZERO(&read_fd_set);
#ifndef _WIN32
    highest_fd = 0;
#endif
#endif
}

SockSelector::~SockSelector() {
#ifdef __linux__
    if (epoll_fd >= 0)
        close(epoll_fd);
#else
#ifdef _WIN32
    if (epoll_fd)
        epoll_close(epoll_fd);
#endif
#endif
}

bool SockSelector::run(unsigned timeout_ms) {
#if defined(__linux__) || defined(_WIN32)
    int t = (timeout_ms == 0) ? -1 : static_cast<int>(timeout_ms);
    int nfds = epoll_wait(epoll_fd, events.data(), events.size(), t);

    ZoneScoped;

    if (nfds < 0)
        return false;

    for (int i = 0; i < nfds; ++i) {
        const int fd = events[i].data.fd;
        callbacks[fd](fd);
    }

    if (nfds == (int)events.size())
        events.resize(events.size() * 2);

    return true;
#else
    timeval tv;
    if (timeout_ms)
        tv = ms_to_timeval(timeout_ms);

    // Do not destroy previous fd set (select is destructive on the fd sets)
    fd_set read_fd_wset = read_fd_set;
    fd_set except_fd_wset = read_fd_set;
    int fd_count = select(highest_fd + 1, &read_fd_wset, nullptr, &except_fd_wset, timeout_ms ? &tv : nullptr);
    if (fd_count < 0)
        return false;

    for (auto fd : read_fds)
        if (FD_ISSET(fd, &read_fd_wset) || FD_ISSET(fd, &except_fd_wset))
            callbacks[fd](fd);

    return true;
#endif
}

bool SockSelector::add_read_fd(socket_t fd, callback_t&& callback) {
    ZoneScoped;

#if defined(__linux__) || defined(_WIN32)
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
        return false;
#else
    if (fd >= FD_SETSIZE)
        return false; // Prevent POSIX stack corruption

    FD_SET(fd, &read_fd_set);
    read_fds.push_back(fd);

    if (fd > highest_fd)
        highest_fd = fd;
#endif

    callbacks[fd] = std::move(callback);

    return true;
}

void SockSelector::remove_read_fd(socket_t fd) {
    ZoneScoped;

#if defined(__linux__) || defined(_WIN32)
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
#else
    FD_CLR(fd, &read_fd_set);

    // O(1) removal (swap and pop)
    for (size_t i = 0; i < read_fds.size(); ++i) {
        if (read_fds[i] == fd) {
            read_fds[i] = read_fds.back();
            read_fds.pop_back();
            break;
        }
    }

    if (fd == highest_fd) {
        highest_fd = 0;
        for (auto s : read_fds)
            if (s > highest_fd)
                highest_fd = s;
    }
#endif

    callbacks.erase(fd);
}
} // namespace server
