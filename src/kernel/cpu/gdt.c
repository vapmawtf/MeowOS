#include <stddef.h>
#include <stdint.h>
#include <meow/gdt.h>

typedef struct GDTEntry64 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
} __attribute__((packed)) GDTEntry64;

typedef struct GDTPtr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GDTPtr64;

typedef struct TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) TSS64;

static GDTEntry64 g_gdt[7];
static GDTPtr64 g_gdt_ptr;
static TSS64 g_tss;
static uint8_t g_kernel_stack[8192] __attribute__((aligned(16)));

static void gdt_set_gate64(uint32_t i, uint64_t base, uint32_t limit, uint8_t access,
                           uint8_t gran) {
    g_gdt[i].base_low = (uint16_t)(base & 0xFFFF);
    g_gdt[i].base_mid = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[i].base_high = (uint8_t)((base >> 24) & 0xFF);
    g_gdt[i].limit_low = (uint16_t)(limit & 0xFFFF);
    g_gdt[i].gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    g_gdt[i].access = access;
}

void gdt_init(void) {
    // Setup GDT pointer
    g_gdt_ptr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdt_ptr.base = (uint64_t)(uintptr_t)&g_gdt[0];

    // Clear GDT and TSS
    for (size_t i = 0; i < sizeof(g_gdt); i++)
        ((uint8_t*)&g_gdt[0])[i] = 0;

    for (size_t i = 0; i < sizeof(TSS64); i++)
        ((uint8_t*)&g_tss)[i] = 0;

    // Kernel stack
    g_tss.rsp0 = (uint64_t)(uintptr_t)(g_kernel_stack + sizeof(g_kernel_stack));
    g_tss.iomap_base = sizeof(TSS64);

    // === GDT Entries ===
    gdt_set_gate64(0, 0, 0, 0, 0); // 0x00  Null descriptor

    gdt_set_gate64(1, 0, 0, 0x9A, 0x20); // 0x08  Kernel Code (64-bit)
    gdt_set_gate64(2, 0, 0, 0x92, 0x00); // 0x10  Kernel Data

    gdt_set_gate64(3, 0, 0, 0xFA, 0x20); // 0x18  User Code (64-bit)
    gdt_set_gate64(4, 0, 0, 0xF2, 0x00); // 0x20  User Data

    // TSS descriptor (must be 16 bytes total → two GDT entries)
    uint64_t tss_base = (uint64_t)(uintptr_t)&g_tss;
    uint32_t tss_limit = sizeof(TSS64) - 1;

    // TSS lower descriptor (index 5 → selector 0x28)
    gdt_set_gate64(5, tss_base, tss_limit, 0x89, 0x00);

    // TSS upper descriptor (index 6) — top 32 bits of base and zero flags
    g_gdt[6].base_low = (uint16_t)((tss_base >> 32) & 0xFFFF);
    g_gdt[6].base_mid = (uint8_t)((tss_base >> 48) & 0xFF);
    g_gdt[6].base_high = (uint8_t)((tss_base >> 56) & 0xFF);
    g_gdt[6].limit_low = 0;
    g_gdt[6].gran = 0;
    g_gdt[6].access = 0;

    // Load GDT
    __asm__ volatile("lgdt %0" : : "m"(g_gdt_ptr) : "memory");

    // Reload segment registers
    __asm__ volatile("mov $0x10, %%ax\n\t"
                     "mov %%ax, %%ds\n\t"
                     "mov %%ax, %%es\n\t"
                     "mov %%ax, %%ss\n\t"
                     "mov %%ax, %%fs\n\t"
                     "mov %%ax, %%gs\n\t" ::
                         : "ax", "memory");

    // Far jump to reload CS (recommended after lgdt)
    __asm__ volatile("pushq $0x08\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n\t"
                     "1:" ::
                         : "rax", "memory");

    // Load TSS
    __asm__ volatile("ltr %0" : : "r"((uint16_t)0x28) : "memory");
}