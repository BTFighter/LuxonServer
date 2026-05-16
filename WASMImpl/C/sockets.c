#include "luxon_server.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#define IPV6_V6ONLY 26
#define WASM_DOWNGRADE_IPV6 1
#ifndef AF_INET6
#define AF_INET6 10
#endif
#endif

struct w2c_env {
    w2c_luxon__server* instance;
};

#ifdef __DJGPP__
struct socket_flag_state {
    int fd;
    int guest_flags;
    struct socket_flag_state* next;
};

static struct socket_flag_state* g_socket_flag_states = NULL;

static struct socket_flag_state* socket_flag_state_find(int fd) {
    struct socket_flag_state* curr = g_socket_flag_states;
    while (curr) {
        if (curr->fd == fd) return curr;
        curr = curr->next;
    }
    return NULL;
}

static struct socket_flag_state* socket_flag_state_ensure(int fd) {
    struct socket_flag_state* state = socket_flag_state_find(fd);
    if (state) return state;

    state = (struct socket_flag_state*)malloc(sizeof(*state));
    if (!state) return NULL;

    state->fd = fd;
    state->guest_flags = 0;
    state->next = g_socket_flag_states;
    g_socket_flag_states = state;
    return state;
}

static void socket_flag_state_register(int fd) {
    if (fd < 0) return;
    struct socket_flag_state* state = socket_flag_state_ensure(fd);
    if (state) state->guest_flags = 0;
}

static void socket_flag_state_unregister(int fd) {
    struct socket_flag_state** link = &g_socket_flag_states;
    while (*link) {
        if ((*link)->fd == fd) {
            struct socket_flag_state* dead = *link;
            *link = dead->next;
            free(dead);
            return;
        }
        link = &(*link)->next;
    }
}

static int socket_flag_state_get(int fd, int* found) {
    struct socket_flag_state* state = socket_flag_state_find(fd);
    if (state) {
        if (found) *found = 1;
        return state->guest_flags;
    }
    if (found) *found = 0;
    return 0;
}

static void socket_flag_state_set(int fd, int guest_flags) {
    struct socket_flag_state* state = socket_flag_state_ensure(fd);
    if (state) state->guest_flags = guest_flags;
}

static void socket_flag_state_set_nonblock(int fd, int enabled) {
    struct socket_flag_state* state = socket_flag_state_ensure(fd);
    if (!state) return;
    if (enabled) state->guest_flags |= 0x00000800;
    else state->guest_flags &= ~0x00000800;
}

static int djgpp_set_socket_nonblocking(int fd, int enabled) {
    unsigned long mode = enabled ? 1UL : 0UL;
    int res = ioctlsocket(fd, FIONBIO, (char*)&mode);
    if (res == 0) {
        socket_flag_state_set_nonblock(fd, enabled);
    }
    return res;
}
#endif

static int translate_domain_g2h(int guest_domain) {
    switch (guest_domain) {
        case 0: return AF_UNSPEC;
        case 2: return AF_INET;
#ifdef WASM_DOWNGRADE_IPV6
        case 10: return AF_INET;
#else
        case 10: return AF_INET6;
#endif
        default: return -1;
    }
}

static int translate_domain_h2g(int host_domain) {
    switch (host_domain) {
        case AF_UNSPEC: return 0;
        case AF_INET: return 2;
        case AF_INET6: return 10;
        default: return host_domain;
    }
}

static int translate_socktype_g2h(int guest_type) {
    switch (guest_type) {
        case 1: return SOCK_STREAM;
        case 2: return SOCK_DGRAM;
        default: return -1;
    }
}

static int translate_socktype_h2g(int host_type) {
    switch (host_type) {
        case SOCK_STREAM: return 1;
        case SOCK_DGRAM: return 2;
        default: return host_type;
    }
}

static int translate_protocol_g2h(int guest_proto) {
    switch (guest_proto) {
        case 0: return IPPROTO_IP;
        case 6: return IPPROTO_TCP;
        case 17: return IPPROTO_UDP;
        case 41: return IPPROTO_IPV6;
        default: return 0;
    }
}

static int translate_protocol_h2g(int host_proto) {
    switch (host_proto) {
        case IPPROTO_IP: return 0;
        case IPPROTO_TCP: return 6;
        case IPPROTO_UDP: return 17;
        case IPPROTO_IPV6: return 41;
        default: return host_proto;
    }
}

static int translate_level_g2h(int guest_level) {
    switch (guest_level) {
        case 1: return SOL_SOCKET;
        case 41: return IPPROTO_IPV6;
        default: return guest_level;
    }
}

static int translate_optname_g2h(int guest_level, int guest_optname) {
    if (guest_level == 1) {
        switch (guest_optname) {
            case 2: return SO_REUSEADDR;
            case 4: return SO_ERROR;
            default: return guest_optname;
        }
    } else if (guest_level == 41) {
        switch (guest_optname) {
            case 26: return IPV6_V6ONLY;
            default: return guest_optname;
        }
    }
    return guest_optname;
}

static int translate_shutdown_how_g2h(int guest_how) {
    switch (guest_how) {
        case 0: return SHUT_RD;
        case 1: return SHUT_WR;
        case 2: return SHUT_RDWR;
        default: return guest_how;
    }
}

static unsigned long translate_ioctl_req_g2h(unsigned long guest_req) {
    switch (guest_req) {
        case 0x5421u: return FIONBIO;
        default: return guest_req;
    }
}

static int translate_fcntl_cmd_g2h(int guest_cmd) {
    switch (guest_cmd) {
        case 3: return F_GETFL;
        case 4: return F_SETFL;
        default: return guest_cmd;
    }
}

static int translate_fcntl_flags_g2h(int guest_flags) {
    int host_flags = 0;
    if (guest_flags & 0x00000800) host_flags |= O_NONBLOCK;
    return host_flags;
}

static int translate_fcntl_flags_h2g(int host_flags) {
    int guest_flags = 0;
    if (host_flags & O_NONBLOCK) guest_flags |= 0x00000800;
    return guest_flags;
}

static int sockaddr_g2h(const uint8_t* guest_addr, uint32_t guest_len, struct sockaddr_storage* host_addr, socklen_t* host_len) {
    if (!guest_addr || guest_len < 2) return -1;
    uint16_t guest_family = *(uint16_t*)guest_addr;

    if (guest_family == 2) {
        if (guest_len < 16) return -1;
        struct sockaddr_in* h4 = (struct sockaddr_in*)host_addr;
        memset(h4, 0, sizeof(*h4));
        h4->sin_family = AF_INET;
        h4->sin_port = *(uint16_t*)(guest_addr + 2);
        memcpy(&h4->sin_addr.s_addr, guest_addr + 4, 4);
        *host_len = sizeof(struct sockaddr_in);
        return 0;
    } else if (guest_family == 10) {
        if (guest_len < 28) return -1;
#ifdef WASM_DOWNGRADE_IPV6
        struct sockaddr_in* h4 = (struct sockaddr_in*)host_addr;
        memset(h4, 0, sizeof(*h4));
        h4->sin_family = AF_INET;
        h4->sin_port = *(uint16_t*)(guest_addr + 2);

        const uint8_t* raw_ip = guest_addr + 8;
        int is_mapped = 1, is_any = 1;
        for (int i = 0; i < 10; i++) if (raw_ip[i] != 0) is_mapped = 0;
        if (raw_ip[10] != 0xff || raw_ip[11] != 0xff) is_mapped = 0;
        for (int i = 0; i < 16; i++) if (raw_ip[i] != 0) is_any = 0;

        if (is_mapped) {
            memcpy(&h4->sin_addr.s_addr, raw_ip + 12, 4);
        } else if (is_any) {
            h4->sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            h4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        *host_len = sizeof(struct sockaddr_in);
        return 0;
#else
        struct sockaddr_in6* h6 = (struct sockaddr_in6*)host_addr;
        memset(h6, 0, sizeof(*h6));
        h6->sin6_family = AF_INET6;
        h6->sin6_port = *(uint16_t*)(guest_addr + 2);
        h6->sin6_flowinfo = *(uint32_t*)(guest_addr + 4);
        memcpy(h6->sin6_addr.s6_addr, guest_addr + 8, 16);
        h6->sin6_scope_id = *(uint32_t*)(guest_addr + 24);
        *host_len = sizeof(struct sockaddr_in6);
        return 0;
#endif
    }
    return -1;
}

static int sockaddr_h2g(const struct sockaddr* host_addr, socklen_t host_len, uint8_t* guest_addr, uint32_t guest_max_len, uint32_t* guest_len_out) {
    if (!host_addr || !guest_addr) return -1;

    if (host_addr->sa_family == AF_INET) {
        if (guest_max_len < 16) return -1;
        const struct sockaddr_in* h4 = (const struct sockaddr_in*)host_addr;
        memset(guest_addr, 0, 16);
        *(uint16_t*)guest_addr = 2;
        *(uint16_t*)(guest_addr + 2) = h4->sin_port;
        memcpy(guest_addr + 4, &h4->sin_addr.s_addr, 4);
        if (guest_len_out) *guest_len_out = 16;
        return 0;
    } else if (host_addr->sa_family == AF_INET6) {
#ifdef WASM_DOWNGRADE_IPV6
        return -1;
#else
        if (guest_max_len < 28) return -1;
        const struct sockaddr_in6* h6 = (const struct sockaddr_in6*)host_addr;
        memset(guest_addr, 0, 28);
        *(uint16_t*)guest_addr = 10;
        *(uint16_t*)(guest_addr + 2) = h6->sin6_port;
        *(uint32_t*)(guest_addr + 4) = h6->sin6_flowinfo;
        memcpy(guest_addr + 8, h6->sin6_addr.s6_addr, 16);
        *(uint32_t*)(guest_addr + 24) = h6->sin6_scope_id;
        if (guest_len_out) *guest_len_out = 28;
        return 0;
#endif
    }
    return -1;
}

static void fd_set_g2h(const uint8_t* guest_fds, fd_set* host_fds, int nfds) {
    FD_ZERO(host_fds);
    if (!guest_fds) return;
    const uint64_t* g_bits = (const uint64_t*)guest_fds;
    for (int i = 0; i < nfds; i++) {
        if (g_bits[i >> 6] & (1ULL << (i & 63))) {
            FD_SET(i, host_fds);
        }
    }
}

static void fd_set_h2g(const fd_set* host_fds, uint8_t* guest_fds, int nfds) {
    if (!guest_fds) return;
    uint64_t* g_bits = (uint64_t*)guest_fds;
    memset(g_bits, 0, ((nfds + 63) / 64) * 8);
    for (int i = 0; i < nfds; i++) {
        if (FD_ISSET(i, host_fds)) {
            g_bits[i >> 6] |= (1ULL << (i & 63));
        }
    }
}

u32 w2c_env_socket_socket(struct w2c_env* env, u32 domain, u32 type, u32 protocol) {
    int res = socket(translate_domain_g2h(domain), translate_socktype_g2h(type), translate_protocol_g2h(protocol));
#ifdef __DJGPP__
    if (res >= 0) socket_flag_state_register(res);
#endif
    return (u32)res;
}

u32 w2c_env_socket_bind(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (addr_ptr + addrlen > mem_size) return (u32)-1;

    struct sockaddr_storage host_addr;
    socklen_t host_len;
    if (sockaddr_g2h(mem + addr_ptr, addrlen, &host_addr, &host_len) < 0) return (u32)-1;

    int opt = 1;
    setsockopt((int)sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int res = bind((int)sockfd, (struct sockaddr*)&host_addr, host_len);
    return (u32)res;
}

u32 w2c_env_socket_listen(struct w2c_env* env, u32 sockfd, u32 backlog) {
    int res = listen((int)sockfd, (int)backlog);
    return (u32)res;
}

u32 w2c_env_socket_accept(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;

    if (addrlen_ptr && addrlen_ptr + 4 > mem_size) return (u32)-1;

    struct sockaddr_storage host_addr;
    socklen_t host_len = sizeof(host_addr);

    int res = accept((int)sockfd, (struct sockaddr*)&host_addr, &host_len);

#ifdef __DJGPP__
    if (res >= 0) socket_flag_state_register(res);
#endif

    if (res >= 0 && addr_ptr && addrlen_ptr) {
        uint32_t guest_max_len = *(uint32_t*)(mem + addrlen_ptr);
        if (addr_ptr + guest_max_len > mem_size) return (u32)-1;

        uint32_t guest_len = 0;
        sockaddr_h2g((struct sockaddr*)&host_addr, host_len, mem + addr_ptr, guest_max_len, &guest_len);
        *(uint32_t*)(mem + addrlen_ptr) = guest_len;
    }

    return (u32)res;
}

u32 w2c_env_socket_connect(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (addr_ptr + addrlen > mem_size) return (u32)-1;

    struct sockaddr_storage host_addr;
    socklen_t host_len;
    if (sockaddr_g2h(mem + addr_ptr, addrlen, &host_addr, &host_len) < 0) return (u32)-1;

    int res = connect((int)sockfd, (struct sockaddr*)&host_addr, host_len);
    return (u32)res;
}

u32 w2c_env_socket_send(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    ssize_t res = send((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags);
    return (u32)res;
}

u32 w2c_env_socket_recv(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    ssize_t res = recv((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags);
    return (u32)res;
}

u32 w2c_env_socket_sendto(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags, u32 dest_addr_ptr, u32 addrlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    struct sockaddr_storage dest_addr;
    socklen_t dest_len = 0;
    struct sockaddr* dest_ptr = NULL;

    if (dest_addr_ptr) {
        if (sockaddr_g2h(mem + dest_addr_ptr, addrlen, &dest_addr, &dest_len) == 0) {
            dest_ptr = (struct sockaddr*)&dest_addr;
        }
    }

    ssize_t res = sendto((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags, dest_ptr, dest_len);
    return (u32)res;
}

u32 w2c_env_socket_recvfrom(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags, u32 src_addr_ptr, u32 addrlen_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    if (addrlen_ptr && addrlen_ptr + 4 > mem_size) return (u32)-1;

    struct sockaddr_storage src_addr;
    socklen_t src_len = sizeof(src_addr);

    ssize_t res = recvfrom((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags, (struct sockaddr*)&src_addr, &src_len);

    if (res >= 0 && src_addr_ptr && addrlen_ptr) {
        uint32_t guest_max_len = *(uint32_t*)(mem + addrlen_ptr);
        if (src_addr_ptr + guest_max_len > mem_size) return (u32)-1;

        uint32_t guest_len = 0;
        sockaddr_h2g((struct sockaddr*)&src_addr, src_len, mem + src_addr_ptr, guest_max_len, &guest_len);
        *(uint32_t*)(mem + addrlen_ptr) = guest_len;
    }

    return (u32)res;
}

u32 w2c_env_socket_setsockopt(struct w2c_env* env, u32 sockfd, u32 level, u32 optname, u32 optval_ptr, u32 optlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (optval_ptr + optlen > mem_size) return (u32)-1;

    int host_level = translate_level_g2h(level);
    int host_optname = translate_optname_g2h(level, optname);

#ifdef WASM_DOWNGRADE_IPV6
    if (host_level == IPPROTO_IPV6) {
        return 0;
    }
#endif

    int res = setsockopt((int)sockfd, host_level, host_optname, mem + optval_ptr, (socklen_t)optlen);
    return (u32)res;
}

u32 w2c_env_socket_shutdown(struct w2c_env* env, u32 sockfd, u32 how) {
    int res = shutdown((int)sockfd, translate_shutdown_how_g2h(how));
    return (u32)res;
}

u32 w2c_env_socket_close(struct w2c_env* env, u32 sockfd) {
    int res = close((int)sockfd);
#ifdef __DJGPP__
    if (res == 0) socket_flag_state_unregister((int)sockfd);
#endif
    return (u32)res;
}

u32 w2c_env_socket_fcntl(struct w2c_env* env, u32 fd, u32 cmd, u32 arg) {
    int host_cmd = translate_fcntl_cmd_g2h(cmd);

#ifdef __DJGPP__
    if (host_cmd == F_GETFL) {
        int found = 0;
        int guest_flags = socket_flag_state_get((int)fd, &found);
        if (found) {
            return (u32)guest_flags;
        }
    } else if (host_cmd == F_SETFL) {
        int nonblock = (arg & 0x00000800) ? 1 : 0;
        if (djgpp_set_socket_nonblocking((int)fd, nonblock) == 0) {
            socket_flag_state_set((int)fd, (int)arg);
            return 0;
        }
    }
#endif

    int host_arg = (host_cmd == F_SETFL) ? translate_fcntl_flags_g2h(arg) : (int)arg;
    int res = fcntl((int)fd, host_cmd, host_arg);

    if (host_cmd == F_GETFL && res >= 0) {
        return translate_fcntl_flags_h2g(res);
    }

    return (u32)res;
}

u32 w2c_env_socket_ioctl(struct w2c_env* env, u32 fd, u32 request, u32 argp) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    unsigned long host_request = translate_ioctl_req_g2h(request);

#ifdef __DJGPP__
    if (host_request == FIONBIO) {
        if (!argp || argp + 4 > mem_size) return (u32)-1;
        unsigned long mode = (*(uint32_t*)(mem + argp) != 0) ? 1UL : 0UL;
        int res = ioctlsocket((int)fd, FIONBIO, (char*)&mode);
        if (res == 0) {
            *(uint32_t*)(mem + argp) = (uint32_t)mode;
            socket_flag_state_set_nonblock((int)fd, mode != 0);
            return 0;
        }
    }
#endif

    void* host_argp = NULL;
    if (argp) {
        if (argp + 4 > mem_size) return (u32)-1;
        host_argp = mem + argp;
    }

    int res = ioctl((int)fd, host_request, host_argp);
    return (u32)res;
}

u32 w2c_env_socket_inet_pton(struct w2c_env* env, u32 af, u32 src_ptr, u32 dst_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (!src_ptr || !dst_ptr || src_ptr >= mem_size || dst_ptr >= mem_size) return 0;

    uint32_t len = 0;
    while (src_ptr + len < mem_size && mem[src_ptr + len] != '\0') {
        len++;
    }
    if (src_ptr + len >= mem_size) return 0;

    int host_af = translate_domain_g2h(af);
    if (host_af < 0) return 0;

    int res = inet_pton(host_af, (const char*)(mem + src_ptr), mem + dst_ptr);
    return (u32)res;
}

u32 w2c_env_socket_inet_ntop(struct w2c_env* env, u32 af, u32 src_ptr, u32 dst_ptr, u32 size) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (!src_ptr || !dst_ptr || dst_ptr + size > mem_size) return 0;

    int host_af = translate_domain_g2h(af);
    if (host_af < 0) return 0;

    const char* res = inet_ntop(host_af, mem + src_ptr, (char*)(mem + dst_ptr), (socklen_t)size);
    return res ? dst_ptr : 0;
}

u32 w2c_env_socket_select(struct w2c_env* env, u32 nfds, u32 readfds_ptr, u32 writefds_ptr, u32 exceptfds_ptr, u32 timeout_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;

    if (readfds_ptr && readfds_ptr + 128 > mem_size) return (u32)-1;
    if (writefds_ptr && writefds_ptr + 128 > mem_size) return (u32)-1;
    if (exceptfds_ptr && exceptfds_ptr + 128 > mem_size) return (u32)-1;

    fd_set h_rfds, h_wfds, h_efds;
    fd_set *host_rfds = NULL, *host_wfds = NULL, *host_efds = NULL;

    if (readfds_ptr) {
        host_rfds = &h_rfds;
        fd_set_g2h(mem + readfds_ptr, host_rfds, nfds);
    }
    if (writefds_ptr) {
        host_wfds = &h_wfds;
        fd_set_g2h(mem + writefds_ptr, host_wfds, nfds);
    }
    if (exceptfds_ptr) {
        host_efds = &h_efds;
        fd_set_g2h(mem + exceptfds_ptr, host_efds, nfds);
    }

    struct timeval host_tv;
    struct timeval* tv = NULL;
    if (timeout_ptr) {
        if (timeout_ptr + 16 > mem_size) return (u32)-1;
        host_tv.tv_sec  = *(int64_t*)(mem + timeout_ptr);
        host_tv.tv_usec = *(int64_t*)(mem + timeout_ptr + 8);
        tv = &host_tv;
    }

    int res = select((int)nfds, host_rfds, host_wfds, host_efds, tv);

    if (res >= 0) {
        if (readfds_ptr) fd_set_h2g(host_rfds, mem + readfds_ptr, nfds);
        if (writefds_ptr) fd_set_h2g(host_wfds, mem + writefds_ptr, nfds);
        if (exceptfds_ptr) fd_set_h2g(host_efds, mem + exceptfds_ptr, nfds);
    }

    if (timeout_ptr && res >= 0) {
        *(int64_t*)(mem + timeout_ptr)     = host_tv.tv_sec;
        *(int64_t*)(mem + timeout_ptr + 8) = host_tv.tv_usec;
    }

    return (u32)res;
}

u32 w2c_env_socket_getaddrinfo(struct w2c_env* env, u32 node_ptr, u32 service_ptr, u32 hints_ptr, u32 res_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    const char* node = node_ptr ? (const char*)(mem + node_ptr) : NULL;
    const char* service = service_ptr ? (const char*)(mem + service_ptr) : NULL;

    struct addrinfo host_hints;
    struct addrinfo* host_hints_ptr = NULL;
    if (hints_ptr) {
        memset(&host_hints, 0, sizeof(host_hints));
        host_hints.ai_flags    = *(int*)(mem + hints_ptr + 0);
        host_hints.ai_family   = translate_domain_g2h(*(int*)(mem + hints_ptr + 4));
        host_hints.ai_socktype = translate_socktype_g2h(*(int*)(mem + hints_ptr + 8));
        host_hints.ai_protocol = translate_protocol_g2h(*(int*)(mem + hints_ptr + 12));
        host_hints_ptr = &host_hints;
    }

    struct addrinfo* host_res = NULL;
    int ret = getaddrinfo(node, service, host_hints_ptr, &host_res);
    if (ret != 0) {
        return (u32)ret;
    }

    uint32_t prev_next_offset = 0;

    for (struct addrinfo* curr = host_res; curr != NULL; curr = curr->ai_next) {
        uint32_t wasm_ai = w2c_luxon__server_malloc(env->instance, 32);

        uint32_t wasm_addr = 0;
        uint32_t wasm_addr_len = 0;

        if (curr->ai_addr && curr->ai_addrlen > 0) {
            uint8_t guest_addr[128];
            if (sockaddr_h2g(curr->ai_addr, curr->ai_addrlen, guest_addr, sizeof(guest_addr), &wasm_addr_len) == 0) {
                wasm_addr = w2c_luxon__server_malloc(env->instance, wasm_addr_len);
                mem = w2c_luxon__server_memory(env->instance)->data;
                memcpy(mem + wasm_addr, guest_addr, wasm_addr_len);
            }
        }

        uint32_t wasm_canon = 0;
        if (curr->ai_canonname) {
            size_t clen = strlen(curr->ai_canonname) + 1;
            wasm_canon = w2c_luxon__server_malloc(env->instance, clen);
            mem = w2c_luxon__server_memory(env->instance)->data;
            memcpy(mem + wasm_canon, curr->ai_canonname, clen);
        }

        mem = w2c_luxon__server_memory(env->instance)->data;
        *(int*)(mem + wasm_ai + 0)       = curr->ai_flags;
        *(int*)(mem + wasm_ai + 4)       = translate_domain_h2g(curr->ai_family);
        *(int*)(mem + wasm_ai + 8)       = translate_socktype_h2g(curr->ai_socktype);
        *(int*)(mem + wasm_ai + 12)      = translate_protocol_h2g(curr->ai_protocol);
        *(uint32_t*)(mem + wasm_ai + 16) = wasm_addr_len;
        *(uint32_t*)(mem + wasm_ai + 20) = wasm_addr;
        *(uint32_t*)(mem + wasm_ai + 24) = wasm_canon;
        *(uint32_t*)(mem + wasm_ai + 28) = 0;

        if (prev_next_offset == 0) {
            *(uint32_t*)(mem + res_ptr) = wasm_ai;
        } else {
            *(uint32_t*)(mem + prev_next_offset) = wasm_ai;
        }
        prev_next_offset = wasm_ai + 28;
    }

    freeaddrinfo(host_res);
    return 0;
}

void w2c_env_socket_freeaddrinfo(struct w2c_env* env, u32 res_ptr) {
    uint32_t curr = res_ptr;
    while (curr != 0) {
        uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
        uint32_t addr_ptr  = *(uint32_t*)(mem + curr + 20);
        uint32_t canon_ptr = *(uint32_t*)(mem + curr + 24);
        uint32_t next_ptr  = *(uint32_t*)(mem + curr + 28);

        if (addr_ptr)  w2c_luxon__server_free(env->instance, addr_ptr);
        if (canon_ptr) w2c_luxon__server_free(env->instance, canon_ptr);
        w2c_luxon__server_free(env->instance, curr);

        curr = next_ptr;
    }
}

u32 w2c_env_socket_getsockname(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;

    if (addrlen_ptr && addrlen_ptr + 4 > mem_size) return (u32)-1;

    struct sockaddr_storage host_addr;
    socklen_t host_len = sizeof(host_addr);

    int res = getsockname((int)sockfd, (struct sockaddr*)&host_addr, &host_len);

    if (res >= 0 && addr_ptr && addrlen_ptr) {
        uint32_t guest_max_len = *(uint32_t*)(mem + addrlen_ptr);
        if (addr_ptr + guest_max_len > mem_size) return (u32)-1;

        uint32_t guest_len = 0;
        sockaddr_h2g((struct sockaddr*)&host_addr, host_len, mem + addr_ptr, guest_max_len, &guest_len);
        *(uint32_t*)(mem + addrlen_ptr) = guest_len;
    }

    return (u32)res;
}
