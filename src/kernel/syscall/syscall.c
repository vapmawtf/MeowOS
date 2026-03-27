#include <stdint.h>
#include <stddef.h>
#include <meow/io.h>
#include <meow/string.h>
#include <meow/syscall.h>
#include <meow/vfs.h>
#include <meow/panic.h>

// -----------------------------------------------------------------------------
// Linux x86-64 syscall numbers (subset needed for toybox/musl)
// -----------------------------------------------------------------------------
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_STAT 4
#define SYS_FSTAT 5
#define SYS_LSTAT 6
#define SYS_LSEEK 8
#define SYS_MMAP 9
#define SYS_MPROTECT 10
#define SYS_MUNMAP 11
#define SYS_BRK 12
#define SYS_IOCTL 16
#define SYS_WRITEV 20
#define SYS_ACCESS 21
#define SYS_EXIT 60
#define SYS_EXIT_GROUP 231
#define SYS_GETPID 39
#define SYS_GETUID 102
#define SYS_GETGID 104
#define SYS_GETEUID 107
#define SYS_GETEGID 108
#define SYS_ARCH_PRCTL 158
#define SYS_SET_TID_ADDRESS 218
#define SYS_CLOCK_GETTIME 228
#define SYS_OPENAT 257
#define SYS_NEWFSTATAT 262
#define SYS_READLINKAT 267
#define SYS_FCNTL 72
#define SYS_GETDENTS64 217
#define SYS_GETCWD 79
#define SYS_CHDIR 80
#define SYS_GETPPID 110
#define SYS_UNAME 63
#define SYS_SYSINFO 99
#define SYS_ISATTY /* not a real syscall, handled via ioctl */ 0

// -----------------------------------------------------------------------------
// File descriptor table
// -----------------------------------------------------------------------------
#define SYS_MAX_FDS 64
#define FD_TYPE_NONE 0
#define FD_TYPE_VFS 1
#define FD_TYPE_STDIN 2
#define FD_TYPE_STDOUT 3
#define FD_TYPE_STDERR 4

typedef struct {
    uint8_t type;
    int vfs_handle;
    uint32_t offset; // for files without vfs seek support
} SysFD;

static SysFD g_fds[SYS_MAX_FDS];

// Heap (brk) — simple bump allocator for mmap/brk
#define HEAP_START 0x1000000ULL // 16 MB — well above kernel
#define HEAP_MAX 0x4000000ULL   // 64 MB ceiling
static uint64_t g_brk = HEAP_START;

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

static int alloc_fd(void) {
    for (int i = 3; i < SYS_MAX_FDS; i++) {
        if (g_fds[i].type == FD_TYPE_NONE) {
            g_fds[i].type = FD_TYPE_VFS;
            g_fds[i].vfs_handle = -1;
            g_fds[i].offset = 0;
            return i;
        }
    }
    return -38; // -ENOSPC
}

static int fd_valid(int fd) {
    return fd >= 0 && fd < SYS_MAX_FDS && g_fds[fd].type != FD_TYPE_NONE;
}

// Resolve AT_FDCWD-relative path (we only support absolute paths for now)
#define AT_FDCWD -100
static const char* resolve_at(int dirfd, const char* path) {
    (void)dirfd; // only absolute paths supported
    return path;
}

// -----------------------------------------------------------------------------
// stat structs (Linux x86-64 layout)
// -----------------------------------------------------------------------------
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    uint64_t st_atime;
    uint64_t st_atime_ns;
    uint64_t st_mtime;
    uint64_t st_mtime_ns;
    uint64_t st_ctime;
    uint64_t st_ctime_ns;
    int64_t __unused[3];
} __attribute__((packed)) linux_stat;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} linux_timespec;

typedef struct {
    int8_t sysname[65];
    int8_t nodename[65];
    int8_t release[65];
    int8_t version[65];
    int8_t machine[65];
    int8_t domainname[65];
} linux_utsname;

// iovec for writev
typedef struct {
    uint64_t iov_base;
    uint64_t iov_len;
} linux_iovec;

// -----------------------------------------------------------------------------
// Syscall implementations
// -----------------------------------------------------------------------------

static int64_t sys_read(int fd, void* buf, uint64_t count) {
    if (!buf || count == 0)
        return -22; // -EINVAL

    if (fd == 0) {
        // stdin — read from keyboard buffer (non-blocking with small delay)
        uint8_t* out = (uint8_t*)buf;
        uint64_t got = 0;
        
        // Try to read up to count bytes from keyboard
        while (got < count) {
            int c = kb_pop();
            if (c < 0) {
                // No more keyboard input available
                if (got > 0) {
                    // Return what we have so far
                    break;
                }
                // If we haven't gotten anything yet, do a quick spin
                for (int i = 0; i < 100; i++) {
                    __asm__ volatile("pause");
                    c = kb_pop();
                    if (c >= 0) break;
                }
                if (c < 0) {
                    // Still nothing, return what we have or 0 for EOF
                    break;
                }
            }
            out[got++] = (uint8_t)c;
            if ((char)c == '\n')
                break; // Line buffered
        }
        return (int64_t)got;
    }

    if (!fd_valid(fd))
        return -9; // -EBADF

    uint32_t out_read = 0;
    if (vfs_file_read(g_fds[fd].vfs_handle, buf, (uint32_t)count, &out_read) != 0)
        return -5; // -EIO
    return (int64_t)out_read;
}

static int64_t sys_write(int fd, const void* buf, uint64_t count) {
    if (!buf)
        return -22;

    if (fd == 1 || fd == 2) {
        const uint8_t* data = (const uint8_t*)buf;
        for (uint64_t i = 0; i < count; i++)
            putchar((char)data[i]);
        return (int64_t)count;
    }

    if (!fd_valid(fd))
        return -9;
    // write to VFS files not yet implemented
    return -9;
}

static int64_t sys_writev(int fd, const linux_iovec* iov, int iovcnt) {
    if (!iov || iovcnt <= 0)
        return -22;
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0)
            continue;
        int64_t r = sys_write(fd, (const void*)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (r < 0)
            return r;
        total += r;
    }
    return total;
}

static int64_t sys_open_inner(const char* path, uint64_t flags, uint64_t mode) {
    (void)mode;
    if (!path)
        return -22;

    // O_WRONLY=1, O_RDWR=2, O_CREAT=0x40 — not supported yet
    // O_RDONLY=0 is fine
    int handle = vfs_file_open(path);
    if (handle < 0)
        return -2; // -ENOENT

    int fd = alloc_fd();
    if (fd < 0) {
        vfs_file_close(handle);
        return -24; // -EMFILE
    }

    g_fds[fd].vfs_handle = handle;
    g_fds[fd].offset = 0;
    return fd;
}

static int64_t sys_openat(int dirfd, const char* path, uint64_t flags, uint64_t mode) {
    return sys_open_inner(resolve_at(dirfd, path), flags, mode);
}

static int64_t sys_close(int fd) {
    if (fd < 3 || !fd_valid(fd))
        return -9;
    vfs_file_close(g_fds[fd].vfs_handle);
    g_fds[fd].type = FD_TYPE_NONE;
    g_fds[fd].vfs_handle = -1;
    return 0;
}

static int64_t sys_lseek(int fd, int64_t offset, int whence) {
    if (!fd_valid(fd))
        return -9;
    // whence: 0=SEEK_SET 1=SEEK_CUR 2=SEEK_END
    if (whence != 0)
        return -22; // only SEEK_SET for now
    if (vfs_file_seek(g_fds[fd].vfs_handle, (uint32_t)offset) != 0)
        return -22;
    return offset;
}

static int64_t sys_fstat(int fd, linux_stat* st) {
    if (!st)
        return -22;
    memset(st, 0, sizeof(*st));

    if (fd == 0 || fd == 1 || fd == 2) {
        // report as character device
        st->st_mode = 0020666; // S_IFCHR | 0666
        st->st_ino = (uint64_t)fd + 1;
        return 0;
    }
    if (!fd_valid(fd))
        return -9;
    // report as regular file with unknown size
    st->st_mode = 0100444; // S_IFREG | 0444
    st->st_blksize = 512;
    return 0;
}

static int64_t sys_stat(const char* path, linux_stat* st) {
    if (!path || !st)
        return -22;
    memset(st, 0, sizeof(*st));
    int handle = vfs_file_open(path);
    if (handle < 0)
        return -2;
    vfs_file_close(handle);
    st->st_mode = 0100444;
    st->st_blksize = 512;
    return 0;
}

static int64_t sys_brk(uint64_t addr) {
    if (addr == 0 || addr < g_brk)
        return (int64_t)g_brk;
    if (addr > HEAP_MAX)
        return (int64_t)g_brk; // refuse
    // zero new pages (they're already identity-mapped and writable)
    memset((void*)(uintptr_t)g_brk, 0, (size_t)(addr - g_brk));
    g_brk = addr;
    return (int64_t)g_brk;
}

static int64_t sys_mmap(uint64_t addr, uint64_t len, int prot, int flags, int fd, uint64_t off) {
    (void)prot;
    (void)flags;
    (void)fd;
    (void)off;
    // anonymous mmap — bump allocate from heap
    if (len == 0)
        return -22;
    // align up to 4KB
    uint64_t base = (g_brk + 0xFFF) & ~0xFFFULL;
    uint64_t end = base + len;
    if (end > HEAP_MAX)
        return -12; // -ENOMEM
    memset((void*)(uintptr_t)base, 0, (size_t)len);
    g_brk = end;
    return (int64_t)base;
}

static int64_t sys_ioctl(int fd, uint64_t req, uint64_t arg) {
    (void)arg;
    // TIOCGWINSZ = 0x5413 — return dummy terminal size
    if (req == 0x5413) {
        // struct winsize: ws_row, ws_col, ws_xpixel, ws_ypixel (all uint16_t)
        uint16_t* ws = (uint16_t*)(uintptr_t)arg;
        if (ws) {
            ws[0] = 24;
            ws[1] = 80;
            ws[2] = 0;
            ws[3] = 0;
        }
        return 0;
    }
    // TCGETS/TCSETS — pretend success so isatty() works
    if (req == 0x5401 || req == 0x5402)
        return 0;
    return -25; // -ENOTTY
}

static int64_t sys_fcntl(int fd, int cmd, uint64_t arg) {
    (void)arg;
    if (!fd_valid(fd) && fd > 2)
        return -9;
    // F_GETFL=3 — return O_RDONLY
    if (cmd == 3)
        return 0;
    // F_SETFL=4 — ignore flags
    if (cmd == 4)
        return 0;
    // F_GETFD=1, F_SETFD=2 — FD_CLOEXEC, we don't do exec so ignore
    if (cmd == 1)
        return 0;
    if (cmd == 2)
        return 0;
    return -22;
}

static int64_t sys_getcwd(char* buf, uint64_t size) {
    const char* cwd = "/";
    if (!buf || size < 2)
        return -22;
    buf[0] = '/';
    buf[1] = '\0';
    return (int64_t)(uintptr_t)buf;
}

static int64_t sys_uname(linux_utsname* u) {
    if (!u)
        return -22;
    memset(u, 0, sizeof(*u));
    memcpy(u->sysname, "MeowOS", 7);
    memcpy(u->nodename, "meow", 5);
    memcpy(u->release, "0.1.0", 6);
    memcpy(u->version, "#1", 3);
    memcpy(u->machine, "x86_64", 7);
    return 0;
}

// Simple monotonic clock for userland
static uint64_t g_monotonic_ticks = 0;

static int64_t sys_clock_gettime(int clk, linux_timespec* ts) {
    if (!ts)
        return -22;
    
    // Increment time with each call (fake monotonic clock)
    g_monotonic_ticks += 1000; // 1 microsecond per call
    
    uint64_t total_ns = g_monotonic_ticks;
    ts->tv_sec = total_ns / 1000000000ULL;
    ts->tv_nsec = total_ns % 1000000000ULL;
    return 0;
}

static int64_t sys_arch_prctl(int code, uint64_t addr) {
    // ARCH_SET_FS = 0x1002 — set FS base (used by musl for TLS)
    if (code == 0x1002) {
        __asm__ volatile("wrmsr" ::"c"(0xC0000100U), // IA32_FS_BASE
                         "a"((uint32_t)(addr & 0xFFFFFFFFU)), "d"((uint32_t)(addr >> 32)));
        return 0;
    }
    // ARCH_GET_FS = 0x1003
    if (code == 0x1003) {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100U));
        *(uint64_t*)(uintptr_t)addr = ((uint64_t)hi << 32) | lo;
        return 0;
    }
    return -22;
}

// -----------------------------------------------------------------------------
// Init & dispatch
// -----------------------------------------------------------------------------

void syscall_init(void) {
    memset(g_fds, 0, sizeof(g_fds));
    // pre-open stdin/stdout/stderr
    g_fds[0].type = FD_TYPE_STDIN;
    g_fds[1].type = FD_TYPE_STDOUT;
    g_fds[2].type = FD_TYPE_STDERR;
}

// Called from the syscall ASM stub.
// Linux x86-64 ABI: rax=num, rdi=arg0, rsi=arg1, rdx=arg2,
//                   r10=arg3, r8=arg4, r9=arg5
int64_t syscall_handle(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5) {
    (void)a4;
    (void)a5;

    // Syscall tracing disabled for cleaner output
    // Uncomment the lines below for debugging

    switch (num) {
        case SYS_READ:
            return sys_read((int)a0, (void*)(uintptr_t)a1, a2);
        case SYS_WRITE:
            return sys_write((int)a0, (const void*)(uintptr_t)a1, a2);
        case SYS_OPEN:
            return sys_open_inner((const char*)(uintptr_t)a0, a1, a2);
        case SYS_CLOSE:
            return sys_close((int)a0);
        case SYS_STAT:
            return sys_stat((const char*)(uintptr_t)a0, (linux_stat*)(uintptr_t)a1);
        case SYS_FSTAT:
            return sys_fstat((int)a0, (linux_stat*)(uintptr_t)a1);
        case SYS_LSTAT:
            return sys_stat((const char*)(uintptr_t)a0, (linux_stat*)(uintptr_t)a1);
        case SYS_LSEEK:
            return sys_lseek((int)a0, (int64_t)a1, (int)a2);
        case SYS_MMAP:
            return sys_mmap(a0, a1, (int)a2, (int)a3, (int)a4, a5);
        case SYS_MPROTECT:
            return 0; // ignore, all memory RWX
        case SYS_MUNMAP:
            return 0; // ignore, no real VMM
        case SYS_BRK:
            return sys_brk(a0);
        case SYS_IOCTL:
            return sys_ioctl((int)a0, a1, a2);
        case SYS_WRITEV:
            return sys_writev((int)a0, (const linux_iovec*)(uintptr_t)a1, (int)a2);
        case SYS_ACCESS:
            return -2; // -ENOENT — file not found is safe default
        case SYS_FCNTL:
            return sys_fcntl((int)a0, (int)a1, a2);
        case SYS_GETCWD:
            return sys_getcwd((char*)(uintptr_t)a0, a1);
        case SYS_CHDIR:
            return 0; // pretend success
        case SYS_UNAME:
            return sys_uname((linux_utsname*)(uintptr_t)a0);
        case SYS_GETPID:
            return 1;
        case SYS_GETPPID:
            return 0;
        case SYS_GETUID:
            return 0;
        case SYS_GETGID:
            return 0;
        case SYS_GETEUID:
            return 0;
        case SYS_GETEGID:
            return 0;
        case SYS_ARCH_PRCTL:
            return sys_arch_prctl((int)a0, a1);
        case SYS_SET_TID_ADDRESS:
            return 1; // return fake tid
        case SYS_CLOCK_GETTIME:
            return sys_clock_gettime((int)a0, (linux_timespec*)(uintptr_t)a1);
        case SYS_OPENAT:
            return sys_openat((int)a0, (const char*)(uintptr_t)a1, a2, a3);
        case SYS_NEWFSTATAT:
            return sys_stat((const char*)(uintptr_t)a1, (linux_stat*)(uintptr_t)a2);
        case SYS_GETDENTS64:
            return -38; // -ENOSYS — no dir listing yet
        case SYS_READLINKAT:
            return -2;
        case 273: // set_robust_list
            return 0;

        case 302:     // prlimit64
            return 0; // pretend success (no resource limit enforcement)

        case 318: // getrandom
            if (a1 && a2) {
                memset((void*)(uintptr_t)a1, 0, (size_t)a2); // return zeros for now
            }
            return (int64_t)a2; // claim we returned the requested bytes

        case 334:       // rseq
            return -38; // -ENOSYS — musl can live without it for basic execution
        case SYS_EXIT:
        case SYS_EXIT_GROUP:
            printf("\n[kernel] process exited with code %lld\n", (long long)a0);
            for (;;)
                __asm__ volatile("hlt");
        default:
            printf("[syscall] unhandled #%llu (args %llx %llx %llx)\n", (unsigned long long)num,
                   (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2);
            return -38; // -ENOSYS
    }
}