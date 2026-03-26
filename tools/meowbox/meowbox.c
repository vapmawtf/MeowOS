#include <stdint.h>

#define MEOW_SYS_READ 0u
#define MEOW_SYS_WRITE 1u
#define MEOW_SYS_OPEN 2u
#define MEOW_SYS_CLOSE 3u
#define MEOW_SYS_LSEEK 8u
#define MEOW_SYS_GETPID 39u

#define O_RDONLY 0u

static int32_t meow_syscall(uint32_t num,
                            uint32_t a0,
                            uint32_t a1,
                            uint32_t a2,
                            uint32_t a3,
                            uint32_t a4,
                            uint32_t a5)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a0), "c"(a1), "d"(a2), "S"(a3), "D"(a4), "r"(a5)
        : "memory"
    );
    return ret;
}

static int32_t meow_read(int32_t fd, void* buf, uint32_t len)
{
    return meow_syscall(MEOW_SYS_READ, (uint32_t)fd, (uint32_t)(uintptr_t)buf, len, 0, 0, 0);
}

static int32_t meow_write(int32_t fd, const void* buf, uint32_t len)
{
    return meow_syscall(MEOW_SYS_WRITE, (uint32_t)fd, (uint32_t)(uintptr_t)buf, len, 0, 0, 0);
}

static int32_t meow_open(const char* path)
{
    return meow_syscall(MEOW_SYS_OPEN, (uint32_t)(uintptr_t)path, O_RDONLY, 0, 0, 0, 0);
}

static int32_t meow_close(int32_t fd)
{
    return meow_syscall(MEOW_SYS_CLOSE, (uint32_t)fd, 0, 0, 0, 0, 0);
}

static int32_t meow_getpid(void)
{
    return meow_syscall(MEOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
}

static uint32_t c_strlen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n])
    {
        n++;
    }
    return n;
}

static int c_streq(const char* a, const char* b)
{
    uint32_t i = 0;
    while (a[i] && b[i])
    {
        if (a[i] != b[i])
        {
            return 0;
        }
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int c_starts_with(const char* s, const char* p)
{
    uint32_t i = 0;
    while (p[i])
    {
        if (s[i] != p[i])
        {
            return 0;
        }
        i++;
    }
    return 1;
}

static void puts1(const char* s)
{
    (void)meow_write(1, s, c_strlen(s));
}

static void put_u32(uint32_t v)
{
    char buf[16];
    uint32_t i = 0;

    if (v == 0)
    {
        (void)meow_write(1, "0", 1);
        return;
    }

    while (v > 0 && i < sizeof(buf))
    {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }

    while (i > 0)
    {
        i--;
        (void)meow_write(1, &buf[i], 1);
    }
}

static void cmd_help(void)
{
    puts1("meowbox applets:\n");
    puts1("  help\n");
    puts1("  echo <text>\n");
    puts1("  pid\n");
    puts1("  cat <path>\n");
    puts1("  exit\n");
}

static void cmd_echo(const char* args)
{
    if (!args)
    {
        puts1("\n");
        return;
    }
    puts1(args);
    puts1("\n");
}

static void cmd_pid(void)
{
    int32_t pid = meow_getpid();
    puts1("pid=");
    if (pid < 0)
    {
        puts1("-1\n");
        return;
    }
    put_u32((uint32_t)pid);
    puts1("\n");
}

static void cmd_cat(const char* path)
{
    char buf[128];
    int32_t fd;

    if (!path || !path[0])
    {
        puts1("cat: missing path\n");
        return;
    }

    fd = meow_open(path);
    if (fd < 0)
    {
        puts1("cat: open failed\n");
        return;
    }

    for (;;)
    {
        int32_t n = meow_read(fd, buf, (uint32_t)sizeof(buf));
        if (n < 0)
        {
            puts1("cat: read failed\n");
            (void)meow_close(fd);
            return;
        }
        if (n == 0)
        {
            break;
        }
        (void)meow_write(1, buf, (uint32_t)n);
    }

    (void)meow_close(fd);
    puts1("\n");
}

static void trim_left(const char** p)
{
    while (**p == ' ' || **p == '\t')
    {
        (*p)++;
    }
}

static void copy_token(char* out, uint32_t out_sz, const char** p)
{
    uint32_t i = 0;

    trim_left(p);
    while (**p && **p != ' ' && **p != '\t' && i + 1 < out_sz)
    {
        out[i++] = **p;
        (*p)++;
    }
    out[i] = '\0';
}

void _start(void)
{
    char line[160];

    puts1("meowbox: syscall multicall shell\n");
    puts1("type 'help'\n");

    for (;;)
    {
        uint32_t n = 0;

        puts1("meowbox$ ");

        while (n + 1 < sizeof(line))
        {
            char c = 0;
            int32_t got = meow_read(0, &c, 1);
            if (got <= 0)
            {
                continue;
            }

            if (c == '\r' || c == '\n')
            {
                puts1("\n");
                break;
            }

            line[n++] = c;
            (void)meow_write(1, &c, 1);
        }

        line[n] = '\0';

        {
            const char* p = line;
            char cmd[24];

            copy_token(cmd, sizeof(cmd), &p);
            trim_left(&p);

            if (cmd[0] == '\0')
            {
                continue;
            }

            if (c_streq(cmd, "exit"))
            {
                puts1("bye\n");
                for (;;)
                {
                    __asm__ volatile ("pause");
                }
            }
            else if (c_streq(cmd, "help"))
            {
                cmd_help();
            }
            else if (c_streq(cmd, "echo"))
            {
                cmd_echo(p);
            }
            else if (c_streq(cmd, "pid"))
            {
                cmd_pid();
            }
            else if (c_streq(cmd, "cat"))
            {
                char path[120];
                copy_token(path, sizeof(path), &p);
                cmd_cat(path);
            }
            else if (c_starts_with(cmd, "meowbox"))
            {
                cmd_help();
            }
            else
            {
                puts1("unknown applet\n");
            }
        }
    }
}
