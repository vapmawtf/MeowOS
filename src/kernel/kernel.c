#include <stddef.h>
#include <stdint.h>

#include <meow/vga.h>
#include <meow/io.h>
#include <meow/gdt.h>
#include <meow/idt.h>
#include <meow/isr.h>
#include <meow/userland/init.h>
#include <meow/panic.h>
#include <meow/longmode.h>
#include <meow/syscall.h>
#include <meow/scheduler.h>

// -----------------------------------------------------------------------------
// Multiboot
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Externs from assembly / other files
// -----------------------------------------------------------------------------
extern uint8_t kernel_stack_top[];
extern void enter_user_mode(uint64_t entry, uint64_t stack); // your wrapper

// map_page must be implemented elsewhere (paging.c or longmode.c)
extern void map_page(uint64_t phys, uint64_t virt, uint64_t flags);

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

    printf("Initializing IDT...");
    idt_install();
    printf("done\n");

    printf("Initializing GDT..");
    gdt_init(); // kernel + user descriptors + lgdt
    printf("done\n");

    printf("Reinitializing interrupts (PIC + IRQ/syscall)");
    interrupts_init();
    printf("done\n");

    printf("Enabling SSE...");
    enable_sse_for_userland();
    printf("done\n");

    printf("Enabling syscall...");
    syscall_init_cpu();
    printf("done\n");

    printf("Initializing syscall table...");
    syscall_init();
    printf("done\n");

    // After syscall_init() ...
    printf("Initializing scheduler...");
    scheduler_init();
    printf("done\n"); // toybox entry + user stack

    // === TSS must be installed before entering user mode ===
    printf("Installing TSS for user mode...");
    install_tss();
    printf("done\n");

    // === Critical: Map memory so user mode + exceptions work ===
    printf("[paging] Brute-force mapping memory regions...\n");

    // 1. Kernel + low memory (32 MB)
    for (uint64_t addr = 0; addr < 0x2000000; addr += 0x1000) {
        map_page(addr, addr, 0x03); // supervisor RW
    }

    // 2. User code + data from ELF
    for (uint64_t addr = 0x400000; addr < 0x5c0000; addr += 0x1000) {
        map_page(addr, addr, 0x07); // Present + Writable + User
    }

    // 3. User stack
    for (uint64_t addr = 0x700000; addr < 0x900000; addr += 0x1000) {
        map_page(addr, addr, 0x07); // Present + Writable + User
    }

    printf("[paging] Memory mapping completed.\n");

    // Now load and enter userland
    init_userland(initramfs_addr, initramfs_size);

    kernel_panic("Returned from userland init, which should never happen");
}