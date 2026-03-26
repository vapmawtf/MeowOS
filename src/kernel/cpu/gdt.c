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
    uint32_t base_upper;
    uint32_t reserved;
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
    g_gdt[i].base_low = (uint16_t)(base & 0xFFFFu);
    g_gdt[i].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    g_gdt[i].base_high = (uint8_t)((base >> 24) & 0xFFu);
    g_gdt[i].base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFFu);
    g_gdt[i].limit_low = (uint16_t)(limit & 0xFFFFu);
    g_gdt[i].gran = (uint8_t)(((limit >> 16) & 0x0Fu) | (gran & 0xF0u));
    g_gdt[i].access = access;
    g_gdt[i].reserved = 0;
}

void gdt_init(void) {
    g_gdt_ptr.limit = (uint16_t)(sizeof(g_gdt) - 1u);
    g_gdt_ptr.base = (uint64_t)(uintptr_t)&g_gdt[0];

    gdt_set_gate64(0, 0, 0, 0, 0);       // null
    gdt_set_gate64(1, 0, 0, 0x9A, 0x20); // kernel code (64-bit)
    gdt_set_gate64(2, 0, 0, 0x92, 0x00); // kernel data
    gdt_set_gate64(3, 0, 0, 0xFA, 0x20); // user code (64-bit)
    gdt_set_gate64(4, 0, 0, 0xF2, 0x00); // user data

    // zerowanie TSS
    for (size_t i = 0; i < sizeof(TSS64); i++)
        ((uint8_t*)&g_tss)[i] = 0;

    g_tss.rsp0 = (uint64_t)(uintptr_t)(g_kernel_stack + sizeof(g_kernel_stack));
    g_tss.iomap_base = sizeof(TSS64);

    uint64_t tss_base = (uint64_t)(uintptr_t)&g_tss;
    uint32_t tss_limit = (uint32_t)sizeof(TSS64) - 1;
    gdt_set_gate64(5, tss_base, tss_limit, 0x89, 0x00); // TSS lower
    gdt_set_gate64(6, tss_base >> 32, 0, 0, 0);         // TSS upper

    __asm__ volatile("lgdt %0" : : "m"(g_gdt_ptr));

    // Reload kernel segmenty
    __asm__ volatile("mov $0x10, %%ax\n"
                     "mov %%ax, %%ds\n"
                     "mov %%ax, %%es\n"
                     "mov %%ax, %%ss\n"
                     "mov %%ax, %%fs\n"
                     "mov %%ax, %%gs\n"
                     :
                     :
                     : "ax", "memory");

    // Load TSS
    __asm__ volatile("ltr %%ax" : : "a"(0x28));
}