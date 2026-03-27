#pragma once
#include <stdint.h>

// Linux x86-64 syscall numbers — must match what toybox/musl expects
// DO NOT use custom numbers here; userland binaries are compiled against Linux ABI
enum {
    MEOW_SYS_READ            = 0,
    MEOW_SYS_WRITE           = 1,
    MEOW_SYS_OPEN            = 2,
    MEOW_SYS_CLOSE           = 3,
    MEOW_SYS_STAT            = 4,
    MEOW_SYS_FSTAT           = 5,
    MEOW_SYS_LSTAT           = 6,
    MEOW_SYS_LSEEK           = 8,
    MEOW_SYS_MMAP            = 9,
    MEOW_SYS_MPROTECT        = 10,
    MEOW_SYS_MUNMAP          = 11,
    MEOW_SYS_BRK             = 12,
    MEOW_SYS_IOCTL           = 16,
    MEOW_SYS_WRITEV          = 20,
    MEOW_SYS_ACCESS          = 21,
    MEOW_SYS_FCNTL           = 72,
    MEOW_SYS_GETCWD          = 79,
    MEOW_SYS_CHDIR           = 80,
    MEOW_SYS_UNAME           = 63,
    MEOW_SYS_GETPID          = 39,
    MEOW_SYS_GETPPID         = 110,
    MEOW_SYS_GETUID          = 102,
    MEOW_SYS_GETGID          = 104,
    MEOW_SYS_GETEUID         = 107,
    MEOW_SYS_GETEGID         = 108,
    MEOW_SYS_SYSINFO         = 99,
    MEOW_SYS_GETDENTS64      = 217,
    MEOW_SYS_SET_TID_ADDRESS = 218,
    MEOW_SYS_CLOCK_GETTIME   = 228,
    MEOW_SYS_EXIT            = 60,
    MEOW_SYS_EXIT_GROUP      = 231,
    MEOW_SYS_ARCH_PRCTL      = 158,
    MEOW_SYS_OPENAT          = 257,
    MEOW_SYS_NEWFSTATAT      = 262,
    MEOW_SYS_READLINKAT      = 267,
};

void    syscall_init(void);

int64_t syscall_handle(uint64_t num,
                       uint64_t a0, uint64_t a1, uint64_t a2,
                       uint64_t a3, uint64_t a4, uint64_t a5);