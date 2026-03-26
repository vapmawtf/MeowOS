#include <stddef.h>
#include <stdint.h>
#include <meow/vga.h>
#include <meow/io.h>
#include <meow/gdt.h>
#include <meow/isr.h>
#include <meow/userland/init.h>
#include <meow/panic.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_INFO_MODS (1u << 3)

typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
} multiboot_info;

typedef struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} multiboot_module;

__attribute__((section(".multiboot"))) const unsigned int multiboot_header[] = {
    0x1BADB002, 0x07, -(0x1BADB002 + 0x07), 0, 1024, 768, 32
};

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    uint32_t initramfs_addr = 0;
    uint32_t initramfs_size = 0;

    vga_init(multiboot_magic, multiboot_info_addr);
    vga_clear();

    if (multiboot_magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        const multiboot_info* mbi = (const multiboot_info*)(uintptr_t)multiboot_info_addr;
        if (mbi && (mbi->flags & MULTIBOOT_INFO_MODS) && mbi->mods_count > 0) {
            const multiboot_module* mods = (const multiboot_module*)(uintptr_t)mbi->mods_addr;
            if (mods[0].mod_end > mods[0].mod_start) {
                initramfs_addr = mods[0].mod_start;
                initramfs_size = mods[0].mod_end - mods[0].mod_start;
            }
        }
    }

    gdt_init();
    interrupts_init();

    init_userland(initramfs_addr, initramfs_size);

    kernel_panic("Returned from userland init, which should never happen");
}