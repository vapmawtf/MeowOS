#include <stdint.h>
#include <meow/io.h>
#include <meow/string.h>
#include <meow/syscall.h>
#include <meow/vfs.h>

typedef struct SysFD {
    uint8_t used;
    int vfs_handle;
} SysFD;

#define SYS_MAX_FDS 16
static SysFD g_fds[SYS_MAX_FDS];

static int alloc_fd(void) {
    for (int i = 3; i < SYS_MAX_FDS; i++) {
        if (!g_fds[i].used) {
            g_fds[i].used = 1;
            g_fds[i].vfs_handle = -1;
            return i;
        }
    }
    return -1;
}

static int sys_open(const char* path, uint32_t flags, uint32_t mode) {
    (void)mode;

    if (!path) {
        return -1;
    }

    if (flags != 0) {
        return -1;
    }

    int handle = vfs_file_open(path);
    if (handle < 0) {
        return -1;
    }

    int fd = alloc_fd();
    if (fd < 0) {
        (void)vfs_file_close(handle);
        return -1;
    }

    g_fds[fd].vfs_handle = handle;
    return fd;
}

static int sys_read(int fd, void* buffer, uint32_t count) {
    if (!buffer) {
        return -1;
    }

    if (fd == 0) {
        uint8_t* out = (uint8_t*)buffer;
        uint32_t got = 0;
        while (got < count) {
            int c = kb_pop();
            if (c < 0) {
                continue;
            }
            out[got++] = (uint8_t)c;
            if ((char)c == '\n') {
                break;
            }
        }
        return (int)got;
    }

    if (fd < 3 || fd >= SYS_MAX_FDS || !g_fds[fd].used) {
        return -1;
    }

    uint32_t out_read = 0;
    if (vfs_file_read(g_fds[fd].vfs_handle, buffer, count, &out_read) != 0) {
        return -1;
    }

    return (int)out_read;
}

static int sys_write(int fd, const void* buffer, uint32_t count) {
    if (!buffer) {
        return -1;
    }

    if (fd != 1 && fd != 2) {
        return -1;
    }

    const uint8_t* data = (const uint8_t*)buffer;
    for (uint32_t i = 0; i < count; i++) {
        putchar((char)data[i]);
    }

    return (int)count;
}

static int sys_close(int fd) {
    if (fd < 3 || fd >= SYS_MAX_FDS || !g_fds[fd].used) {
        return -1;
    }

    int rc = vfs_file_close(g_fds[fd].vfs_handle);
    g_fds[fd].used = 0;
    g_fds[fd].vfs_handle = -1;
    return rc == 0 ? 0 : -1;
}

static int sys_lseek(int fd, uint32_t offset, uint32_t whence) {
    if (fd < 3 || fd >= SYS_MAX_FDS || !g_fds[fd].used) {
        return -1;
    }

    if (whence != 0) {
        return -1;
    }

    return vfs_file_seek(g_fds[fd].vfs_handle, offset) == 0 ? (int)offset : -1;
}

void syscall_init(void) {
    memset(g_fds, 0, sizeof(g_fds));
    g_fds[0].used = 1;
    g_fds[1].used = 1;
    g_fds[2].used = 1;
}

int32_t syscall_handle(uint32_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                       uint32_t arg4, uint32_t arg5) {
    (void)arg3;
    (void)arg4;
    (void)arg5;

    switch (num) {
        case MEOW_SYS_READ:
            return sys_read((int)arg0, (void*)arg1, arg2);
        case MEOW_SYS_WRITE:
            return sys_write((int)arg0, (const void*)arg1, arg2);
        case MEOW_SYS_OPEN:
            return sys_open((const char*)arg0, arg1, arg2);
        case MEOW_SYS_CLOSE:
            return sys_close((int)arg0);
        case MEOW_SYS_LSEEK:
            return sys_lseek((int)arg0, arg1, arg2);
        case MEOW_SYS_GETPID:
            return 1;
        default:
            return -1;
    }
}
