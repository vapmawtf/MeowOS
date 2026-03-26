#pragma once

#include <stdint.h>

enum {
    MEOW_SYS_READ = 0,
    MEOW_SYS_WRITE = 1,
    MEOW_SYS_OPEN = 2,
    MEOW_SYS_CLOSE = 3,
    MEOW_SYS_LSEEK = 8,
    MEOW_SYS_GETPID = 39
};

void syscall_init(void);
int32_t syscall_handle(
    uint32_t num,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t arg4,
    uint32_t arg5);