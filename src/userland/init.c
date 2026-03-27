#include <meow/userland/init.h>
#include <meow/vga.h>
#include <meow/io.h>
#include <meow/runtime.h>
#include <meow/string.h>
#include <meow/storage.h>
#include <meow/vfs.h>
#include <meow/gdt.h>
#include <meow/panic.h>
#include <meow/enter_user_mode.h>

#include <stdint.h>
#include <stddef.h>
#include <meow/string.h>
#include <meow/elf64.h>

typedef struct ELF32_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) ELF32_Ehdr;

typedef struct ELF32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) ELF32_Phdr;

#define ELF_MAGIC0 0x7Fu
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELFCLASS32 1u
#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define EM_386 3u
#define EM_X86_64 62u
#define PT_LOAD 1u

#define CPIO_S_IFMT 0170000
#define CPIO_S_IFREG 0100000
#define CPIO_S_IFLNK 0120000

#define ELF_LOAD_MIN_VADDR 0x00400000u

typedef struct CPIO_NewcHeader {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
} __attribute__((packed)) CPIO_NewcHeader;

typedef struct ELF64_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) ELF64_Ehdr;

typedef struct ELF64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) ELF64_Phdr;

#define CPIO_NEWC_MAGIC "070701"
#define CPIO_NEWC_HEADER_SIZE 110u

static uint32_t align_up_4(uint32_t x) {
    return (x + 3u) & ~3u;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int parse_hex_u32_8(const char in[8], uint32_t* out) {
    uint32_t value = 0;

    if (!in || !out) {
        return -1;
    }

    for (size_t i = 0; i < 8; i++) {
        int n = hex_nibble(in[i]);
        if (n < 0) {
            return -1;
        }
        value = (value << 4) | (uint32_t)n;
    }

    *out = value;
    return 0;
}

static int is_cpio_candidate(const char* name, const char* want) {
    if (!name || !want) {
        return 0;
    }

    if (strcmp(name, want) == 0) {
        return 1;
    }

    if (name[0] == '/' && strcmp(name + 1, want) == 0) {
        return 1;
    }

    if (name[0] == '.' && name[1] == '/' && strcmp(name + 2, want) == 0) {
        return 1;
    }

    return 0;
}

static int find_file_in_initramfs_internal(const uint8_t* image, uint32_t image_size,
                                           const char* path, const uint8_t** out_data,
                                           uint32_t* out_size, int depth);

static int find_file_in_initramfs(const uint8_t* image, uint32_t image_size, const char* path,
                                  const uint8_t** out_data, uint32_t* out_size) {
    return find_file_in_initramfs_internal(image, image_size, path, out_data, out_size, 0);
}

static int find_file_in_initramfs_internal(const uint8_t* image, uint32_t image_size,
                                           const char* path, const uint8_t** out_data,
                                           uint32_t* out_size, int depth) {
    uint32_t off = 0;

    if (!image || image_size < CPIO_NEWC_HEADER_SIZE || !path || !out_data || !out_size) {
        return -1;
    }

    if (depth > 8) {
        return -1;
    }

    while (off + CPIO_NEWC_HEADER_SIZE <= image_size) {
        const CPIO_NewcHeader* hdr = (const CPIO_NewcHeader*)(const void*)(image + off);

        uint32_t namesz = 0;
        uint32_t filesz = 0;
        uint32_t mode = 0;

        if (memcmp(hdr->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
            return -1;
        }

        if (parse_hex_u32_8(hdr->c_namesize, &namesz) != 0 ||
            parse_hex_u32_8(hdr->c_filesize, &filesz) != 0 ||
            parse_hex_u32_8(hdr->c_mode, &mode) != 0) {
            return -1;
        }

        if (namesz == 0) {
            return -1;
        }

        uint32_t name_off = off + CPIO_NEWC_HEADER_SIZE;
        if (name_off + namesz > image_size) {
            return -1;
        }

        const char* name = (const char*)(const void*)(image + name_off);

        if (strcmp(name, "TRAILER!!!") == 0) {
            return -1;
        }

        uint32_t data_off = align_up_4(name_off + namesz);
        if (data_off + filesz > image_size) {
            return -1;
        }

        if (is_cpio_candidate(name, path)) {
            if ((mode & CPIO_S_IFMT) == CPIO_S_IFLNK) {
                const char* target = (const char*)(image + data_off);

                char tmp[128];
                uint32_t len = (filesz < sizeof(tmp) - 1) ? filesz : (sizeof(tmp) - 1);

                memcpy(tmp, target, len);
                tmp[len] = '\0';

                return find_file_in_initramfs_internal(image, image_size, tmp, out_data, out_size,
                                                       depth + 1);
            }

            *out_data = image + data_off;
            *out_size = filesz;
            return 0;
        }

        off = align_up_4(data_off + filesz);
    }

    return -1;
}


void load_elf64(const uint8_t* elf_data)
{
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_data;

    // Sprawdzenie magic
    if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        puts("Invalid ELF file");
        return;
    }

    // Przejście po wszystkich program headers
    Elf64_Phdr* phdr = (Elf64_Phdr*)(elf_data + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        // Kopiowanie segmentu do pamięci docelowej
        uint8_t* dest = (uint8_t*)(uintptr_t)phdr[i].p_vaddr;
        const uint8_t* src = elf_data + phdr[i].p_offset;

        for (uint64_t j = 0; j < phdr[i].p_filesz; j++)
            dest[j] = src[j];

        // Zerowanie części BSS
        for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++)
            dest[j] = 0;
    }

    // Wywołanie entry point
    void (*entry)() = (void(*)())ehdr->e_entry;
    entry();
}

#define ET_DYN  3u
#define PIE_LOAD_BASE 0x400000u   // load PIE binaries here

static int load_elf64_from_memory(const uint8_t* image, uint32_t image_size, uint64_t* out_entry) {
    const ELF64_Ehdr* eh;

    if (!image || !out_entry || image_size < sizeof(ELF64_Ehdr)) return -1;

    eh = (const ELF64_Ehdr*)(const void*)image;

    if (eh->e_ident[0] != ELF_MAGIC0 || eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 || eh->e_ident[3] != ELF_MAGIC3 ||
        eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_machine != EM_X86_64) {
        return -1;
    }

    // Accept both ET_EXEC and ET_DYN (PIE)
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        return -1;
    }

    if (eh->e_phentsize != sizeof(ELF64_Phdr)) return -1;

    // PIE binaries have vaddrs relative to 0; give them a base
    uint64_t load_base = (eh->e_type == ET_DYN) ? PIE_LOAD_BASE : 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        uint64_t phoff = eh->e_phoff + ((uint64_t)i * eh->e_phentsize);
        const ELF64_Phdr* ph;

        if (phoff + sizeof(ELF64_Phdr) > image_size) return -1;

        ph = (const ELF64_Phdr*)(const void*)(image + (uint32_t)phoff);
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_memsz < ph->p_filesz) return -1;
        if (ph->p_offset + ph->p_filesz > image_size) return -1;

        // For ET_EXEC enforce vaddr >= 0x100000; for PIE vaddr is relative so skip that check
        if (eh->e_type == ET_EXEC && ph->p_vaddr < 0x100000u) return -1;

        uint8_t* dest = (uint8_t*)(uintptr_t)(ph->p_vaddr + load_base);
        if (ph->p_filesz > 0) {
            memcpy(dest, image + (uint32_t)ph->p_offset, (uint32_t)ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            memset(dest + ph->p_filesz, 0, (uint32_t)(ph->p_memsz - ph->p_filesz));
        }
    }

    *out_entry = eh->e_entry + load_base;
    return 0;
}

static int load_elf64_from_initramfs(const uint8_t* image, uint32_t image_size, uint64_t* out_entry,
                                     const char** out_path) {
    static const char* const candidates[] = { "bin/toybox", "bin/sh", "BIN/SH" };

    for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++) {
        const uint8_t* data = NULL;
        uint32_t size = 0;

        if (find_file_in_initramfs(image, image_size, candidates[i], &data, &size) != 0) {
            printf("[initramfs] '%s' not found in cpio\n", candidates[i]);
            continue;
        }

        printf("[initramfs] found '%s' (%u bytes), trying ELF load...\n", candidates[i], size);

        if (load_elf64_from_memory(data, size, out_entry) == 0) {
            if (out_path) *out_path = candidates[i];
            return 0;
        }
    }

    return -1;
}

static int read_exact_fd(int fd, uint32_t offset, void* out, uint32_t size) {
    uint8_t* dst = (uint8_t*)out;
    uint32_t done = 0;

    if (vfs_file_seek(fd, offset) != 0) {
        return -1;
    }

    while (done < size) {
        uint32_t got = 0;
        uint32_t want = size - done;
        if (vfs_file_read(fd, dst + done, want, &got) != 0) {
            return -1;
        }
        if (got == 0) {
            return -1;
        }
        done += got;
    }

    return 0;
}

static int load_elf32_from_vfs(const char* path, uint32_t* out_entry) {
    ELF32_Ehdr eh;
    int fd;

    if (!path || !out_entry) {
        return -1;
    }

    fd = vfs_file_open(path);
    if (fd < 0) {
        return -1;
    }

    if (read_exact_fd(fd, 0, &eh, sizeof(eh)) != 0) {
        (void)vfs_file_close(fd);
        return -1;
    }

    if (eh.e_ident[0] != ELF_MAGIC0 || eh.e_ident[1] != ELF_MAGIC1 || eh.e_ident[2] != ELF_MAGIC2 ||
        eh.e_ident[3] != ELF_MAGIC3 || eh.e_ident[4] != ELFCLASS32 ||
        eh.e_ident[5] != ELFDATA2LSB || eh.e_type != ET_EXEC || eh.e_machine != EM_386) {
        (void)vfs_file_close(fd);
        return -1;
    }

    if (eh.e_phentsize != sizeof(ELF32_Phdr)) {
        (void)vfs_file_close(fd);
        return -1;
    }

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        ELF32_Phdr ph;
        uint32_t phoff = eh.e_phoff + ((uint32_t)i * eh.e_phentsize);

        if (read_exact_fd(fd, phoff, &ph, sizeof(ph)) != 0) {
            (void)vfs_file_close(fd);
            return -1;
        }

        if (ph.p_type != PT_LOAD) {
            continue;
        }

        if (ph.p_memsz < ph.p_filesz || ph.p_vaddr < ELF_LOAD_MIN_VADDR) {
            (void)vfs_file_close(fd);
            return -1;
        }

        uint8_t* dest = (uint8_t*)(uintptr_t)ph.p_vaddr;

        if (ph.p_filesz > 0) {
            if (read_exact_fd(fd, ph.p_offset, dest, ph.p_filesz) != 0) {
                (void)vfs_file_close(fd);
                return -1;
            }
        }

        if (ph.p_memsz > ph.p_filesz) {
            memset(dest + ph.p_filesz, 0, ph.p_memsz - ph.p_filesz);
        }
    }

    *out_entry = eh.e_entry;
    (void)vfs_file_close(fd);
    return 0;
}

static void rt_puts(const char* s) {
    if (!s) {
        return;
    }
    (void)rt_write(1, s, (uint32_t)strlen(s));
}

static void rt_read_line(char* out, size_t max_len) {
    size_t n = 0;

    if (!out || max_len == 0) {
        return;
    }

    while (n + 1 < max_len) {
        char c = 0;
        int32_t got = rt_read(0, &c, 1);
        if (got <= 0) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            break;
        }

        out[n++] = c;
    }

    out[n] = '\0';
}

static void user_cmd_cat(const char* path) {
    char buf[128];

    if (!path || !*path) {
        rt_puts("usage: cat <path>\n");
        return;
    }

    int32_t fd = rt_open(path, RT_O_RDONLY, 0);
    if (fd < 0) {
        rt_puts("cat: open failed\n");
        return;
    }

    while (1) {
        int32_t n = rt_read(fd, buf, (uint32_t)sizeof(buf));
        if (n < 0) {
            rt_puts("cat: read failed\n");
            (void)rt_close(fd);
            return;
        }
        if (n == 0) {
            break;
        }
        (void)rt_write(1, buf, (uint32_t)n);
    }

    (void)rt_close(fd);
    rt_puts("\n");
}

static void auto_mount_first_root(void) {
    size_t n = vfs_block_device_count();

    for (size_t i = 0; i < n; i++) {
        VFS_BlockDeviceInfo info;
        if (vfs_get_block_device(i, &info) != 0) {
            continue;
        }

        if (strncmp(info.name, "cd", 2) == 0) {
            continue;
        }

        if (vfs_mount_fat32_root(info.name, 0) == 0) {
            printf("Auto-mounted FAT root on %s\n", info.name);
            return;
        }
    }

    for (size_t i = 0; i < n; i++) {
        VFS_BlockDeviceInfo info;
        if (vfs_get_block_device(i, &info) != 0) {
            continue;
        }

        if (strncmp(info.name, "cd", 2) != 0) {
            continue;
        }

        if (vfs_mount_iso_root(info.name, 0) == 0) {
            printf("Auto-mounted ISO root on %s\n", info.name);
            return;
        }
    }
}

void init_userland(uint32_t initramfs_addr, uint32_t initramfs_size)
{
    vfs_init();
    storage_init();
    auto_mount_first_root();

    if (initramfs_addr != 0 && initramfs_size != 0) {
        const uint8_t* ramfs = (const uint8_t*)(uintptr_t)initramfs_addr;
        int looks_like_newc =
            (initramfs_size >= 6 && ramfs[0] == '0' && ramfs[1] == '7' && ramfs[2] == '0' &&
             ramfs[3] == '7' && ramfs[4] == '0' && ramfs[5] == '1');
        printf("Initramfs module loaded at 0x%x (%u bytes)%s\n", initramfs_addr, initramfs_size,
               looks_like_newc ? "" : " (unexpected header)");
    } else {
        printf("Initramfs module not present\n");
    }

    printf("Initializing userland...\n");
    printf("Welcome to MeowOS!\n");

#ifdef MEOW_KERNEL_64
    if (initramfs_addr != 0 && initramfs_size != 0) {
        const uint8_t* ramfs = (const uint8_t*)(uintptr_t)initramfs_addr;
        uint64_t elf64_entry = 0;
        const char* payload_path = NULL;

        if (load_elf64_from_initramfs(ramfs, initramfs_size, &elf64_entry, &payload_path) == 0) {
            printf("Loaded 64-bit %s from initramfs entry=0x%x\n",
                   payload_path ? payload_path : "payload", (uint32_t)elf64_entry);

            uint64_t user_stack_top = 0x800000;
            user_stack_top &= ~0xFul; // align to 16 bytes
            uint64_t* stack = (uint64_t*)user_stack_top;

            *(--stack) = 0;              // NULL
            *(--stack) = (uint64_t)"sh"; // argv[0]
            *(--stack) = (uint64_t)1;    // argc

            printf("Entering user mode at 0x%x with stack 0x%x...\n",
                   (uint32_t)elf64_entry, (uint32_t)user_stack_top);
            enter_user_mode(elf64_entry, (uint64_t)stack);

            kernel_panic("Initramfs payload returned unexpectedly");
        }
    }

    kernel_panic("No runnable initramfs payload (expected /bin/sh)");

#else   // 32-bit path
    if (initramfs_addr != 0 && initramfs_size != 0) {
        const uint8_t* ramfs = (const uint8_t*)(uintptr_t)initramfs_addr;
        uint32_t elf32_entry = 0;

        if (load_elf32_from_initramfs(ramfs, initramfs_size, &elf32_entry) == 0) {
            printf("Loaded 32-bit ELF from initramfs, entry=0x%x\n", elf32_entry);
            void (*entry_fn)(void) = (void (*)(void))(uintptr_t)elf32_entry;
            entry_fn();
            kernel_panic("Initramfs payload returned unexpectedly");
        }
    }

    // fallback: load from VFS
    {
        uint32_t elf32_entry = 0;
        if (load_elf32_from_vfs("/boot/init.elf", &elf32_entry) == 0 ||
            load_elf32_from_vfs("/init.elf", &elf32_entry) == 0) {
            printf("Loaded 32-bit ELF from VFS, entry=0x%x\n", elf32_entry);
            void (*entry_fn)(void) = (void (*)(void))(uintptr_t)elf32_entry;
            entry_fn();
            kernel_panic("VFS payload returned unexpectedly");
        }
    }

    kernel_panic("No valid userland entry point found");
#endif
}