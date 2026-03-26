#include <stdint.h>
#include <meow/runtime.h>
#include <meow/syscall.h>

static int32_t rt_syscall(
    uint32_t num,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t arg4,
    uint32_t arg5)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4), "r"(arg5)
        : "memory"
    );
    return ret;
}

int32_t rt_open(const char* path, uint32_t flags, uint32_t mode)
{
    return rt_syscall(MEOW_SYS_OPEN, (uint32_t)(uintptr_t)path, flags, mode, 0, 0, 0);
}

int32_t rt_read(int32_t fd, void* buffer, uint32_t count)
{
    return rt_syscall(MEOW_SYS_READ, (uint32_t)fd, (uint32_t)(uintptr_t)buffer, count, 0, 0, 0);
}

int32_t rt_write(int32_t fd, const void* buffer, uint32_t count)
{
    return rt_syscall(MEOW_SYS_WRITE, (uint32_t)fd, (uint32_t)(uintptr_t)buffer, count, 0, 0, 0);
}

int32_t rt_close(int32_t fd)
{
    return rt_syscall(MEOW_SYS_CLOSE, (uint32_t)fd, 0, 0, 0, 0, 0);
}

int32_t rt_lseek(int32_t fd, uint32_t offset, uint32_t whence)
{
    return rt_syscall(MEOW_SYS_LSEEK, (uint32_t)fd, offset, whence, 0, 0, 0);
}

int32_t rt_getpid(void)
{
    return rt_syscall(MEOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
}
