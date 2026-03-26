#pragma once

#include <stdint.h>

enum {
    RT_O_RDONLY = 0,
    RT_SEEK_SET = 0
};

int32_t rt_open(const char* path, uint32_t flags, uint32_t mode);
int32_t rt_read(int32_t fd, void* buffer, uint32_t count);
int32_t rt_write(int32_t fd, const void* buffer, uint32_t count);
int32_t rt_close(int32_t fd);
int32_t rt_lseek(int32_t fd, uint32_t offset, uint32_t whence);
int32_t rt_getpid(void);