#include <meow/vga.h>
#include <meow/userland/shell.h>
#include <meow/io.h>
#include <meow/string.h>
#include <meow/storage.h>
#include <meow/vfs.h>

static char to_hex_digit(uint8_t v)
{
    return (char)(v < 10 ? ('0' + v) : ('A' + (v - 10)));
}

static void print_hex8(uint8_t v)
{
    putchar(to_hex_digit((uint8_t)((v >> 4) & 0x0Fu)));
    putchar(to_hex_digit((uint8_t)(v & 0x0Fu)));
}

static void print_hex32(uint32_t v)
{
    for (int i = 7; i >= 0; i--)
    {
        uint8_t nib = (uint8_t)((v >> (i * 4)) & 0x0Fu);
        putchar(to_hex_digit(nib));
    }
}

static int parse_u32(const char* s, uint32_t* out)
{
    uint32_t value = 0;

    if (!s || !*s || !out)
    {
        return -1;
    }

    while (*s)
    {
        if (*s < '0' || *s > '9')
        {
            return -1;
        }

        value = (value * 10u) + (uint32_t)(*s - '0');
        s++;
    }

    *out = value;
    return 0;
}

static int print_dir_entry(const FAT32_DirEntry* entry, void* user)
{
    (void)user;
    if (entry->attr & 0x10u)
    {
        printf("<DIR>  %s\n", entry->short_name);
    }
    else
    {
        printf("%6u %s\n", entry->size, entry->short_name);
    }
    return 0;
}

static void cmd_lsblk(void)
{
    size_t n = vfs_block_device_count();
    printf("NAME | SECTOR_SIZE | SECTORS | READ\n");
    if (n == 0)
    {
        printf("(no block devices registered)\n");
        return;
    }

    for (size_t i = 0; i < n; i++)
    {
        VFS_BlockDeviceInfo info;
        if (vfs_get_block_device(i, &info) == 0)
        {
            printf("%s | %u | %u | %s\n",
                   info.name,
                   info.sector_size,
                   info.sector_count,
                   info.readable ? "yes" : "no");
        }
    }
}

static void cmd_ls(const char* path)
{
    const char* p = (path && *path) ? path : "/";

    if (!vfs_is_root_mounted())
    {
        printf("No filesystem mounted. Use: mount fat <device> [partition_lba]\n");
        return;
    }

    if (vfs_list_dir(p, print_dir_entry, 0) != 0)
    {
        printf("ls: cannot list '%s'\n", p);
    }
}

static void cmd_mount_fat(const char* args)
{
    char dev[16];
    uint32_t lba = 0;
    int has_lba = 0;
    size_t i = 0;

    if (!args || !*args)
    {
        printf("usage: mount fat <device> [partition_lba]\n");
        return;
    }

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(dev) - 1)
    {
        dev[i++] = *args++;
    }
    dev[i] = '\0';

    if (dev[0] == '\0' || strchr(dev, '/') != 0)
    {
        printf("usage: mount fat <device> [partition_lba]\n");
        return;
    }

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    if (*args)
    {
        const char* p = args;
        while (*p && *p != ' ' && *p != '\t')
        {
            if (*p < '0' || *p > '9')
            {
                printf("mountfat: partition_lba must be a decimal number\n");
                return;
            }
            p++;
        }

        has_lba = 1;
        lba = (uint32_t)atoi(args);
    }

    if (vfs_mount_fat32_root(dev, lba) == 0)
    {
        if (has_lba)
        {
            printf("Mounted FAT32 root from %s (LBA %u)\n", dev, lba);
        }
        else
        {
            printf("Mounted FAT32 root from %s\n", dev);
        }
    }
    else
    {
        printf("mount fat failed for device %s (LBA %u)\n", dev, lba);
    }
}

static void cmd_diskutil_format(const char* args)
{
    char dev[16];
    char fs[16];
    size_t i = 0;
    size_t j = 0;

    if (!args || !*args)
    {
        printf("usage: diskutil format <device> fat\n");
        return;
    }

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(dev) - 1)
    {
        dev[i++] = *args++;
    }
    dev[i] = '\0';

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    while (*args && *args != ' ' && *args != '\t' && j < sizeof(fs) - 1)
    {
        fs[j++] = *args++;
    }
    fs[j] = '\0';

    if (dev[0] == '\0' || fs[0] == '\0')
    {
        printf("usage: diskutil format <device> fat\n");
        return;
    }

    if (strcmp(fs, "fat") != 0)
    {
        printf("diskutil: only 'fat' format is supported\n");
        return;
    }

    if (storage_format_fat32(dev) != 0)
    {
        printf("diskutil: format failed for %s\n", dev);
        return;
    }

    printf("diskutil: formatted %s as FAT32\n", dev);
}

static void cmd_cat(const char* args)
{
    char path[96];
    uint8_t buffer[128];
    uint32_t offset = 0;
    size_t i = 0;

    while (args && (*args == ' ' || *args == '\t'))
    {
        args++;
    }

    if (!args || !*args)
    {
        printf("usage: cat <path>\n");
        return;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(path) - 1)
    {
        path[i++] = *args++;
    }
    path[i] = '\0';

    if (!vfs_is_root_mounted())
    {
        printf("No filesystem mounted. Use: mount fat <device> [partition_lba]\n");
        return;
    }

    while (1)
    {
        uint32_t got = 0;
        if (vfs_read_file(path, offset, buffer, sizeof(buffer), &got) != 0)
        {
            printf("cat: failed to read '%s'\n", path);
            return;
        }

        if (got == 0)
        {
            break;
        }

        for (uint32_t j = 0; j < got; j++)
        {
            putchar((char)buffer[j]);
        }

        offset += got;
    }

    putchar('\n');
}

static void cmd_hexdump(const char* args)
{
    char path[96];
    uint8_t buffer[16];
    uint32_t max_bytes = 256;
    uint32_t offset = 0;
    size_t i = 0;

    while (args && (*args == ' ' || *args == '\t'))
    {
        args++;
    }

    if (!args || !*args)
    {
        printf("usage: hexdump <path> [max_bytes]\n");
        return;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(path) - 1)
    {
        path[i++] = *args++;
    }
    path[i] = '\0';

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    if (*args)
    {
        max_bytes = (uint32_t)atoi(args);
        if (max_bytes == 0)
        {
            max_bytes = 256;
        }
    }

    if (!vfs_is_root_mounted())
    {
        printf("No filesystem mounted. Use: mount fat <device> [partition_lba]\n");
        return;
    }

    while (offset < max_bytes)
    {
        uint32_t got = 0;
        uint32_t want = (max_bytes - offset > sizeof(buffer)) ? (uint32_t)sizeof(buffer) : (max_bytes - offset);

        if (vfs_read_file(path, offset, buffer, want, &got) != 0)
        {
            printf("hexdump: failed to read '%s'\n", path);
            return;
        }

        if (got == 0)
        {
            break;
        }

        print_hex32(offset);
        printf(": ");

        for (uint32_t j = 0; j < got; j++)
        {
            print_hex8(buffer[j]);
            putchar(' ');
        }

        for (uint32_t j = got; j < 16; j++)
        {
            printf("   ");
        }

        printf("|");
        for (uint32_t j = 0; j < got; j++)
        {
            char c = (char)buffer[j];
            if (c < ' ' || c > '~')
            {
                c = '.';
            }
            putchar(c);
        }
        printf("|\n");

        offset += got;
    }
}

static void cmd_readsec(const char* args)
{
    char dev[16];
    uint32_t lba = 0;
    uint8_t sector[512];
    size_t i = 0;

    while (args && (*args == ' ' || *args == '\t'))
    {
        args++;
    }

    if (!args || !*args)
    {
        printf("usage: readsec <device> <lba>\n");
        return;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(dev) - 1)
    {
        dev[i++] = *args++;
    }
    dev[i] = '\0';

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    if (!*args)
    {
        printf("usage: readsec <device> <lba>\n");
        return;
    }

    lba = (uint32_t)atoi(args);

    if (vfs_read_block_device(dev, lba, 1, sector) != 0)
    {
        printf("readsec: failed for %s LBA %u\n", dev, lba);
        return;
    }

    for (uint32_t off = 0; off < 512; off += 16)
    {
        print_hex32(off);
        printf(": ");
        for (uint32_t j = 0; j < 16; j++)
        {
            print_hex8(sector[off + j]);
            putchar(' ');
        }
        printf("|\n");
    }
}

static void cmd_head(const char* args)
{
    char path[96];
    uint8_t buffer[128];
    uint32_t max_bytes = 128;
    uint32_t offset = 0;
    size_t i = 0;

    while (args && (*args == ' ' || *args == '\t'))
    {
        args++;
    }

    if (!args || !*args)
    {
        printf("usage: head <path> [bytes]\n");
        return;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(path) - 1)
    {
        path[i++] = *args++;
    }
    path[i] = '\0';

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    if (*args)
    {
        if (parse_u32(args, &max_bytes) != 0 || max_bytes == 0)
        {
            printf("head: bytes must be a positive decimal number\n");
            return;
        }
    }

    if (!vfs_is_root_mounted())
    {
        printf("No filesystem mounted. Use: mount fat <device> [partition_lba]\n");
        return;
    }

    while (offset < max_bytes)
    {
        uint32_t got = 0;
        uint32_t want = max_bytes - offset;

        if (want > sizeof(buffer))
        {
            want = (uint32_t)sizeof(buffer);
        }

        if (vfs_read_file(path, offset, buffer, want, &got) != 0)
        {
            printf("head: failed to read '%s'\n", path);
            return;
        }

        if (got == 0)
        {
            break;
        }

        for (uint32_t j = 0; j < got; j++)
        {
            char c = (char)buffer[j];
            if (c == '\r' || c == '\n' || (c >= ' ' && c <= '~'))
            {
                putchar(c);
            }
            else
            {
                putchar('.');
            }
        }

        offset += got;
    }

    putchar('\n');
}

static void cmd_read_range(const char* args)
{
    char path[96];
    char off_str[16];
    char len_str[16];
    uint8_t buffer[64];
    uint32_t offset;
    uint32_t length;
    uint32_t done = 0;
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;

    while (args && (*args == ' ' || *args == '\t'))
    {
        args++;
    }

    if (!args || !*args)
    {
        printf("usage: read <path> <offset> <length>\n");
        return;
    }

    while (*args && *args != ' ' && *args != '\t' && i < sizeof(path) - 1)
    {
        path[i++] = *args++;
    }
    path[i] = '\0';

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    while (*args && *args != ' ' && *args != '\t' && j < sizeof(off_str) - 1)
    {
        off_str[j++] = *args++;
    }
    off_str[j] = '\0';

    while (*args == ' ' || *args == '\t')
    {
        args++;
    }

    while (*args && *args != ' ' && *args != '\t' && k < sizeof(len_str) - 1)
    {
        len_str[k++] = *args++;
    }
    len_str[k] = '\0';

    if (path[0] == '\0' || off_str[0] == '\0' || len_str[0] == '\0')
    {
        printf("usage: read <path> <offset> <length>\n");
        return;
    }

    if (parse_u32(off_str, &offset) != 0 || parse_u32(len_str, &length) != 0 || length == 0)
    {
        printf("read: offset and length must be positive decimal numbers\n");
        return;
    }

    if (!vfs_is_root_mounted())
    {
        printf("No filesystem mounted. Use: mount fat <device> [partition_lba]\n");
        return;
    }

    while (done < length)
    {
        uint32_t got = 0;
        uint32_t want = length - done;

        if (want > sizeof(buffer))
        {
            want = (uint32_t)sizeof(buffer);
        }

        if (vfs_read_file(path, offset + done, buffer, want, &got) != 0)
        {
            printf("read: failed to read '%s'\n", path);
            return;
        }

        if (got == 0)
        {
            break;
        }

        for (uint32_t n = 0; n < got; n++)
        {
            char c = (char)buffer[n];
            if (c == '\r' || c == '\n' || (c >= ' ' && c <= '~'))
            {
                putchar(c);
            }
            else
            {
                putchar('.');
            }
        }

        done += got;
    }

    putchar('\n');
}

void shell_main()
{
    char command[128];

    printf("Welcome to MeowOS Shell!\nType 'help' for a list of commands.\n");

    while (1)
    {
        printf("> ");
        read_line(command, sizeof(command));

        if (strcmp(command, "help") == 0)
        {
            printf("Available commands:\n");
            printf("  help - Show this help message\n");
            printf("  echo [text] - Print the text back to the console\n");
            printf("  clear - Clear the screen\n");
            printf("  lsblk - List block devices\n");
            printf("  mount fat <device> [partition_lba] - Mount FAT32 root\n");
            printf("  diskutil format <device> fat - Format disk as FAT32\n");
            printf("  cat <path> - Print file contents\n");
            printf("  hexdump <path> [max_bytes] - Hex dump file bytes\n");
            printf("  head <path> [bytes] - Print first bytes from file\n");
            printf("  read <path> <offset> <length> - Print a file range\n");
            printf("  readsec <device> <lba> - Hex dump raw disk sector\n");
            printf("  ls [path] - List directory entries\n");
        }
        else if (strncmp(command, "echo ", 5) == 0)
        {
            printf("%s\n", command + 5);
        }
        else if (strcmp(command, "clear") == 0)
        {
            // Clear the screen by printing newlines
            for (int i = 0; i < HEIGHT; i++)
            {
                printf("\n");
            }
        }
        else if (strcmp(command, "lsblk") == 0)
        {
            cmd_lsblk();
        }
        else if (strncmp(command, "mount fat", 9) == 0)
        {
            cmd_mount_fat(command + 9);
        }
        else if (strncmp(command, "diskutil format", 15) == 0)
        {
            cmd_diskutil_format(command + 15);
        }
        else if (strncmp(command, "cat ", 4) == 0)
        {
            cmd_cat(command + 4);
        }
        else if (strncmp(command, "hexdump ", 8) == 0)
        {
            cmd_hexdump(command + 8);
        }
        else if (strncmp(command, "readsec ", 8) == 0)
        {
            cmd_readsec(command + 8);
        }
        else if (strncmp(command, "head ", 5) == 0)
        {
            cmd_head(command + 5);
        }
        else if (strncmp(command, "read ", 5) == 0)
        {
            cmd_read_range(command + 5);
        }
        else if (strcmp(command, "ls") == 0)
        {
            cmd_ls("/");
        }
        else if (strncmp(command, "ls ", 3) == 0)
        {
            cmd_ls(command + 3);
        }
        else if (strlen(command) > 0)
        {
            printf("Unknown command: %s\n", command);
        }
    }
}