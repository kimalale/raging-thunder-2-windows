#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "shims.h"
#include "gl_wrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <time.h>
#include <sys/stat.h>

static int shim_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[%s] ", tag ? tag : "ENGINE");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
    return 0;
}

static int shim_cxa_atexit(void (*func)(void*), void *arg, void *dso) {
    return 0;
}

static int *shim_errno(void) {
    return _errno();
}

static int shim_open(const char *raw_path, int flags, ...) {
    int pmode = _S_IREAD | _S_IWRITE;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    if (!raw_path) return -1;
    
    char path[512];
    strncpy(path, raw_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    
    char *p = path;
    if (strncmp(p, "/sdcard", 7) == 0) p += 7;
    if (strncmp(p, "(null)", 6) == 0) p += 6;
    if (p[0] == '/') p++;
    
    int fd = _open(p, flags | _O_BINARY, pmode);
    if (fd < 0 && (strstr(p, "Data.vfs") != NULL)) {
        fd = _open("Data.vfs", flags | _O_BINARY, pmode);
    }
    if (fd < 0 && (strstr(p, "raging-thunder-21-0-16.apk") != NULL)) {
        fd = _open("raging-thunder-21-0-16.apk", flags | _O_BINARY, pmode);
    }
    
    printf("[FS] open(\"%s\" -> \"%s\", 0x%x) -> fd %d\n", raw_path, p, flags, fd);
    return fd;
}

static int shim_close(int fd) {
    return _close(fd);
}

static int shim_read(int fd, void *buf, size_t count) {
    return _read(fd, buf, count);
}

static int shim_write(int fd, const void *buf, size_t count) {
    return _write(fd, buf, count);
}

static FILE *shim_fopen(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    printf("[FS] fopen(\"%s\", \"%s\") -> %p\n", path ? path : "(null)", mode ? mode : "(null)", f);
    return f;
}

static long shim_lseek(int fd, long offset, int whence) {
    return _lseek(fd, offset, whence);
}

static int shim_fstat(int fd, void *buf) {
    if (!buf) return -1;
    memset(buf, 0, 128);
    
    long cur = _lseek(fd, 0, SEEK_CUR);
    long sz = _lseek(fd, 0, SEEK_END);
    _lseek(fd, cur, SEEK_SET);
    
    /* Bionic x86 struct stat offsets */
    *(uint32_t*)((char*)buf + 0x08) = 0100644; /* S_IFREG | 0644 */
    *(uint32_t*)((char*)buf + 0x20) = (uint32_t)sz;
    *(uint32_t*)((char*)buf + 0x2c) = (uint32_t)sz; /* Offset 44: PFile::Size() reads here */
    *(uint32_t*)((char*)buf + 0x30) = (uint32_t)sz;
    return 0;
}

static int shim_mkdir(const char *path, int mode) {
    return _mkdir(path);
}

static int shim_rmdir(const char *path) {
    return _rmdir(path);
}

static int shim_setenv(const char *name, const char *val, int overwrite) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=%s", name, val);
    return _putenv(buf);
}

static int shim_gettimeofday(struct timeval *tv, void *tz) {
    if (!tv) return 0;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10;
    t -= 11644473600000000ULL; /* 1601 to 1970 */
    tv->tv_sec = (long)(t / 1000000ULL);
    tv->tv_usec = (long)(t % 1000000ULL);
    return 0;
}

/* Mutex implementation */
static int shim_pthread_mutex_init(volatile long *mutex, void *attr) {
    if (mutex) *mutex = 0;
    return 0;
}

static int shim_pthread_mutex_destroy(volatile long *mutex) {
    if (mutex) *mutex = 0;
    return 0;
}

static int shim_pthread_mutex_lock(volatile long *mutex) {
    if (!mutex) return 0;
    DWORD tid = GetCurrentThreadId();
    if (*mutex == (long)tid) return 0;
    while (InterlockedCompareExchange(mutex, (long)tid, 0) != 0) {
        SwitchToThread();
    }
    return 0;
}

static int shim_pthread_mutex_unlock(volatile long *mutex) {
    if (!mutex) return 0;
    *mutex = 0;
    return 0;
}

/* dlopen / dlsym / dlclose */
static void *shim_dlopen(const char *filename, int flag) {
    printf("[DL] dlopen(\"%s\", 0x%x)\n", filename ? filename : "self", flag);
    return (void*)0x12345678;
}

static int shim_dlclose(void *handle) {
    return 0;
}

static void *shim_dlsym(void *handle, const char *symbol) {
    return gl_get_proc(symbol);
}

static int shim_ret0(void) {
    return 0;
}

static const host_symbol_t g_host_symbols[] = {
    { "__android_log_print", (uintptr_t)shim_log_print },
    { "__cxa_atexit",        (uintptr_t)shim_cxa_atexit },
    { "__errno",             (uintptr_t)shim_errno },
    { "malloc",              (uintptr_t)malloc },
    { "free",                (uintptr_t)free },
    { "realloc",             (uintptr_t)realloc },
    { "memcpy",              (uintptr_t)memcpy },
    { "memset",              (uintptr_t)memset },
    { "sprintf",             (uintptr_t)sprintf },
    { "strcpy",              (uintptr_t)strcpy },
    { "bsearch",             (uintptr_t)bsearch },
    { "open",                (uintptr_t)shim_open },
    { "close",               (uintptr_t)shim_close },
    { "read",                (uintptr_t)shim_read },
    { "write",               (uintptr_t)shim_write },
    { "lseek",               (uintptr_t)shim_lseek },
    { "fopen",               (uintptr_t)shim_fopen },
    { "fclose",              (uintptr_t)fclose },
    { "fflush",              (uintptr_t)fflush },
    { "fwrite",              (uintptr_t)fwrite },
    { "fstat",               (uintptr_t)shim_fstat },
    { "mkdir",               (uintptr_t)shim_mkdir },
    { "remove",              (uintptr_t)remove },
    { "rename",              (uintptr_t)rename },
    { "rmdir",               (uintptr_t)shim_rmdir },
    { "socket",              (uintptr_t)socket },
    { "bind",                (uintptr_t)bind },
    { "connect",             (uintptr_t)connect },
    { "listen",              (uintptr_t)listen },
    { "accept",              (uintptr_t)accept },
    { "send",                (uintptr_t)send },
    { "sendto",              (uintptr_t)sendto },
    { "recv",                (uintptr_t)recv },
    { "recvfrom",            (uintptr_t)recvfrom },
    { "select",              (uintptr_t)select },
    { "shutdown",            (uintptr_t)shutdown },
    { "getsockopt",          (uintptr_t)getsockopt },
    { "setsockopt",          (uintptr_t)setsockopt },
    { "getpeername",         (uintptr_t)getpeername },
    { "getsockname",         (uintptr_t)getsockname },
    { "getaddrinfo",         (uintptr_t)getaddrinfo },
    { "gai_strerror",        (uintptr_t)gai_strerror },
    { "fcntl",               (uintptr_t)shim_ret0 },
    { "ioctl",               (uintptr_t)shim_ret0 },
    { "pthread_mutex_init",    (uintptr_t)shim_pthread_mutex_init },
    { "pthread_mutex_destroy", (uintptr_t)shim_pthread_mutex_destroy },
    { "pthread_mutex_lock",    (uintptr_t)shim_pthread_mutex_lock },
    { "pthread_mutex_unlock",  (uintptr_t)shim_pthread_mutex_unlock },
    { "dlopen",              (uintptr_t)shim_dlopen },
    { "dlsym",               (uintptr_t)shim_dlsym },
    { "dlclose",             (uintptr_t)shim_dlclose },
    { "getenv",              (uintptr_t)getenv },
    { "setenv",              (uintptr_t)shim_setenv },
    { "time",                (uintptr_t)time },
    { "gettimeofday",        (uintptr_t)shim_gettimeofday }
};

void shims_init(void) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

const host_symbol_t *shims_get_table(size_t *count) {
    if (count) *count = sizeof(g_host_symbols) / sizeof(g_host_symbols[0]);
    return g_host_symbols;
}
