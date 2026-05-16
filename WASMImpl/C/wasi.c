#include "luxon_server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif

#ifndef S_ISSOCK
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifdef __DJGPP__
#include <sys/time.h>

typedef int clockid_t;
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3

static int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        tp->tv_sec = tv.tv_sec;
        tp->tv_nsec = tv.tv_usec * 1000;
        return 0;
    }
    return -1;
}
#endif

// Safe fake FD that won't collide
#define FAKE_RANDOM_FD 0x7FFFFFFF

struct w2c_wasi__snapshot__preview1 {
    w2c_luxon__server* instance;
};

static u32 errno_to_wasi(int err) {
    switch (err) {
        case 0: return 0;
        case EPERM: return 63;
        case ENOENT: return 44;
        case EBADF: return 8;
        case EAGAIN: return 6;
        case ENOMEM: return 48;
        case EACCES: return 2;
        case EBUSY: return 10;
        case EEXIST: return 20;
        case EINVAL: return 28;
        case ENOSPC: return 51;
        case EPIPE: return 64;
        case ENOSYS: return 52;
        case ECONNRESET: return 15;
        default: return 29; // EIO fallback
    }
}

static clockid_t translate_clock_id(u32 clock_id) {
    switch (clock_id) {
        case 0: return CLOCK_REALTIME;
        case 1: return CLOCK_MONOTONIC;
        case 2: return CLOCK_PROCESS_CPUTIME_ID;
        case 3: return CLOCK_THREAD_CPUTIME_ID;
        default: return CLOCK_REALTIME;
    }
}

static int translate_whence_w2h(u32 wasi_whence) {
    switch (wasi_whence) {
        case 0: return SEEK_SET;
        case 1: return SEEK_CUR;
        case 2: return SEEK_END;
        default: return SEEK_SET;
    }
}

static int translate_oflags_w2h(u32 oflags) {
    int host_flags = 0;
    if (oflags & 1) host_flags |= O_CREAT;
    if (oflags & 2) host_flags |= O_DIRECTORY;
    if (oflags & 4) host_flags |= O_EXCL;
    if (oflags & 8) host_flags |= O_TRUNC;
    return host_flags;
}

static int translate_fdflags_w2h(u32 fdflags) {
    int host_flags = 0;
    if (fdflags & 1) host_flags |= O_APPEND;
    if (fdflags & 4) host_flags |= O_NONBLOCK;
    return host_flags;
}

u32 w2c_wasi__snapshot__preview1_clock_time_get(struct w2c_wasi__snapshot__preview1* wasi, u32 clock_id, u64 precision, u32 time_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (time_ptr + 8 > mem_size) return 28;

    struct timespec ts;
    clockid_t cid = translate_clock_id(clock_id);

    if (clock_gettime(cid, &ts) < 0) return errno_to_wasi(errno);
    uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    *(uint64_t*)(mem + time_ptr) = nanos;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_environ_get(struct w2c_wasi__snapshot__preview1* wasi, u32 environ_ptr, u32 environ_buf_ptr) {
    return 0;
}

u32 w2c_wasi__snapshot__preview1_environ_sizes_get(struct w2c_wasi__snapshot__preview1* wasi, u32 environ_count_ptr, u32 environ_buf_size_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (environ_count_ptr + 4 > mem_size || environ_buf_size_ptr + 4 > mem_size) return 28;

    *(uint32_t*)(mem + environ_count_ptr) = 0;
    *(uint32_t*)(mem + environ_buf_size_ptr) = 0;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_close(struct w2c_wasi__snapshot__preview1* wasi, u32 fd) {
    if (fd <= 2) return 0;
    if (fd == FAKE_RANDOM_FD) return 0;
    if (close((int)fd) < 0) return errno_to_wasi(errno);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 stat_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (stat_ptr + 24 > mem_size) return 28;

    if (fd == 3) {
        memset(mem + stat_ptr, 0, 24);
        mem[stat_ptr] = 3;
        memset(mem + stat_ptr + 8, 0xff, 16);
        return 0;
    }

    if (fd == FAKE_RANDOM_FD) {
        memset(mem + stat_ptr, 0, 24);
        mem[stat_ptr] = 2;
        memset(mem + stat_ptr + 8, 0xff, 16);
        return 0;
    }

    struct stat st;
    if (fstat((int)fd, &st) < 0) return errno_to_wasi(errno);

    uint8_t filetype = 0;
    if (S_ISCHR(st.st_mode)) filetype = 2;
    else if (S_ISDIR(st.st_mode)) filetype = 3;
    else if (S_ISREG(st.st_mode)) filetype = 4;
    else if (S_ISSOCK(st.st_mode)) filetype = 6;

    memset(mem + stat_ptr, 0, 24);
    mem[stat_ptr] = filetype;
    memset(mem + stat_ptr + 8, 0xff, 16);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_set_flags(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 flags) {
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 buf_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (buf_ptr + 64 > mem_size) return 28;

    if (fd == FAKE_RANDOM_FD) {
        memset(mem + buf_ptr, 0, 64);
        mem[buf_ptr + 16] = 2;
        return 0;
    }

    struct stat st;
    if (fstat((int)fd, &st) < 0) return errno_to_wasi(errno);

    uint8_t filetype = 0;
    if (S_ISCHR(st.st_mode)) filetype = 2;
    else if (S_ISDIR(st.st_mode)) filetype = 3;
    else if (S_ISREG(st.st_mode)) filetype = 4;
    else if (S_ISSOCK(st.st_mode)) filetype = 6;

    memset(mem + buf_ptr, 0, 64);
    *(uint64_t*)(mem + buf_ptr)      = st.st_dev;
    *(uint64_t*)(mem + buf_ptr + 8)  = st.st_ino;
    mem[buf_ptr + 16]                = filetype;
    *(uint64_t*)(mem + buf_ptr + 24) = st.st_nlink;
    *(uint64_t*)(mem + buf_ptr + 32) = st.st_size;
    *(uint64_t*)(mem + buf_ptr + 40) = (uint64_t)st.st_atime * 1000000000ULL;
    *(uint64_t*)(mem + buf_ptr + 48) = (uint64_t)st.st_mtime * 1000000000ULL;
    *(uint64_t*)(mem + buf_ptr + 56) = (uint64_t)st.st_ctime * 1000000000ULL;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_set_size(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u64 size) {
    if (fd == FAKE_RANDOM_FD) return 28;
    if (ftruncate((int)fd, (off_t)size) < 0) return errno_to_wasi(errno);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_prestat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 buf_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (buf_ptr + 8 > mem_size) return 28;

    if (fd == 3) {
        mem[buf_ptr] = 0;
        *(uint32_t*)(mem + buf_ptr + 4) = 2;
        return 0;
    }
    return 8;
}

u32 w2c_wasi__snapshot__preview1_fd_prestat_dir_name(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (path_ptr + path_len > mem_size) return 28;

    if (fd == 3) {
        if (path_len >= 2) {
            memcpy(mem + path_ptr, ".", 2);
            return 0;
        }
        return 28;
    }
    return 8;
}

u32 w2c_wasi__snapshot__preview1_path_open(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 dirflags, u32 path_ptr, u32 path_len, u32 oflags, u64 fs_rights_base, u64 fs_rights_inheriting, u32 fdflags, u32 opened_fd_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (path_ptr + path_len > mem_size || opened_fd_ptr + 4 > mem_size) return 28;

    char* host_path = (char*)malloc(path_len + 1);
    if (!host_path) return 48;
    memcpy(host_path, mem + path_ptr, path_len);
    host_path[path_len] = '\0';

    if (strcmp(host_path, "dev/urandom") == 0 || strcmp(host_path, "/dev/urandom") == 0 ||
        strcmp(host_path, "dev/random") == 0 || strcmp(host_path, "/dev/random") == 0) {

        const char* dev_path = strstr(host_path, "urandom") ? "/dev/urandom" : "/dev/random";
        free(host_path);

        int new_fd = open(dev_path, O_RDONLY | ((fdflags & 4) ? O_NONBLOCK : 0));
        if (new_fd < 0) {
            fprintf(stderr, "WARNING: %s could not be opened. Providing standard C PRNG fallback.\n", dev_path);
            new_fd = FAKE_RANDOM_FD;
        }

        mem = w2c_luxon__server_memory(wasi->instance)->data;
        *(uint32_t*)(mem + opened_fd_ptr) = new_fd;
        return 0;
    }

    int host_flags = O_RDWR;
    host_flags |= translate_oflags_w2h(oflags);
    host_flags |= translate_fdflags_w2h(fdflags);

    int new_fd = open(host_path, host_flags, 0666);

    if (new_fd < 0) {
        if ((errno == EACCES || errno == EPERM || errno == EISDIR) && !(oflags & (1 | 8)) && !(fdflags & 1)) {
            new_fd = open(host_path, O_RDONLY | (host_flags & ~O_RDWR));
        }
    }

    free(host_path);
    if (new_fd < 0) return 44;

    mem = w2c_luxon__server_memory(wasi->instance)->data;
    *(uint32_t*)(mem + opened_fd_ptr) = new_fd;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_read(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 iovs_ptr, u32 iovs_len, u32 nread_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (nread_ptr + 4 > mem_size) return 28;

    uint32_t total_read = 0;

    if (fd == FAKE_RANDOM_FD) {
        for (u32 i = 0; i < iovs_len; i++) {
            uint32_t iov_ptr = iovs_ptr + i * 8;
            if (iov_ptr + 8 > mem_size) return 28;
            uint32_t buf_ptr = *(uint32_t*)(mem + iov_ptr);
            uint32_t buf_len = *(uint32_t*)(mem + iov_ptr + 4);
            if (buf_ptr + buf_len > mem_size) return 28;

            for (u32 j = 0; j < buf_len; j++) {
                mem[buf_ptr + j] = rand() & 0xff;
            }
            total_read += buf_len;
        }
        *(uint32_t*)(mem + nread_ptr) = total_read;
        return 0;
    }

    for (u32 i = 0; i < iovs_len; i++) {
        uint32_t iov_ptr = iovs_ptr + i * 8;
        if (iov_ptr + 8 > mem_size) return 28;
        uint32_t buf_ptr = *(uint32_t*)(mem + iov_ptr);
        uint32_t buf_len = *(uint32_t*)(mem + iov_ptr + 4);
        if (buf_ptr + buf_len > mem_size) return 28;

        ssize_t res = read((int)fd, mem + buf_ptr, buf_len);
        if (res < 0) {
            if (total_read > 0) break;
            return errno_to_wasi(errno);
        }
        total_read += res;
        if ((size_t)res < buf_len || res == 0) break;
    }
    *(uint32_t*)(mem + nread_ptr) = total_read;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_seek(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u64 offset, u32 whence, u32 newoffset_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (newoffset_ptr + 8 > mem_size) return 28;

    if (fd == FAKE_RANDOM_FD) return 28;

    int w = translate_whence_w2h(whence);

    off_t res = lseek((int)fd, (off_t)offset, w);
    if (res == (off_t)-1) return errno_to_wasi(errno);
    *(uint64_t*)(mem + newoffset_ptr) = (uint64_t)res;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_sync(struct w2c_wasi__snapshot__preview1* wasi, u32 fd) {
    if (fd == FAKE_RANDOM_FD) return 0;
    if (fsync((int)fd) < 0) return errno_to_wasi(errno);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_write(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 iovs_ptr, u32 iovs_len, u32 nwritten_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (nwritten_ptr + 4 > mem_size) return 28;

    if (fd == FAKE_RANDOM_FD) return 28;

    uint32_t total_written = 0;
    for (u32 i = 0; i < iovs_len; i++) {
        uint32_t iov_ptr = iovs_ptr + i * 8;
        if (iov_ptr + 8 > mem_size) return 28;
        uint32_t buf_ptr = *(uint32_t*)(mem + iov_ptr);
        uint32_t buf_len = *(uint32_t*)(mem + iov_ptr + 4);
        if (buf_ptr + buf_len > mem_size) return 28;

        ssize_t res = write((int)fd, mem + buf_ptr, buf_len);
        if (res < 0) {
            if (total_written > 0) break;
            return errno_to_wasi(errno);
        }
        total_written += res;
        if ((size_t)res < buf_len) break;
    }
    *(uint32_t*)(mem + nwritten_ptr) = total_written;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_path_create_directory(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_filestat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 flags, u32 path_ptr, u32 path_len, u32 buf_ptr) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_filestat_set_times(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 flags, u32 path_ptr, u32 path_len, u64 atim, u64 mtim, u32 fst_flags) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_readlink(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len, u32 buf_ptr, u32 buf_len, u32 bufused_ptr) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_remove_directory(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_unlink_file(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) { return 52; }
u32 w2c_wasi__snapshot__preview1_poll_oneoff(struct w2c_wasi__snapshot__preview1* wasi, u32 in_ptr, u32 out_ptr, u32 nsubscriptions, u32 nevents_ptr) { return 52; }

void w2c_wasi__snapshot__preview1_proc_exit(struct w2c_wasi__snapshot__preview1* wasi, u32 rval) {
    exit((int)rval);
}

u32 w2c_wasi__snapshot__preview1_random_get(struct w2c_wasi__snapshot__preview1* wasi, u32 buf_ptr, u32 buf_len) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (buf_ptr + buf_len > mem_size) return 28;

    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t ignored = fread(mem + buf_ptr, 1, buf_len, f);
        (void)ignored;
        fclose(f);
    } else {
        fprintf(stderr, "WARNING: /dev/urandom not found. Falling back to PRNG for random_get.\n");
        for (u32 i = 0; i < buf_len; i++) {
            mem[buf_ptr + i] = rand() & 0xff;
        }
    }
    return 0;
}
