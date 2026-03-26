#include <stdint.h>
#include <stddef.h>

// Extern symbols for page tables and trampoline
extern void longmode_trampoline(uint64_t entry, uint64_t stack);
extern uint64_t pml4_table[];
extern uint64_t pdpt_table[];
extern uint64_t pd_table[];

// MSR addresses
#define IA32_EFER 0xC0000080
#define EFER_LME (1ULL << 8)

static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" ::"c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

#include "longmode_jump.h"

// GDT and descriptor for 64-bit mode
extern uint64_t gdt64[];
extern uint16_t gdt64_descriptor[];

void setup_long_mode(uint64_t entry, uint64_t stack) {
    // Already in long mode. Just jump to the 64-bit ELF entry with a clean stack.
    longmode_trampoline(entry, stack);
}
