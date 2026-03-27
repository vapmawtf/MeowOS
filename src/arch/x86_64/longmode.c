#include <stdint.h>
#include <stddef.h>
#include <meow/io.h>
#include <meow/syscall.h>

// Symbols from boot.s
extern uint8_t gdt64[];
extern uint8_t gdt64_descriptor[];
extern uint8_t tss64[];
extern uint8_t tss64_end[];
extern uint8_t kernel_stack_top[];

#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_SFMASK 0xC0000084
#define EFER_SCE (1ULL << 0)
#define EFER_LME (1ULL << 8)

extern void syscall_entry(void);

// TSS descriptor occupies GDT slot at byte offset 0x28
// (after null + kernel code + kernel data + user code + user data)
#define TSS_GDT_OFFSET 0x28

static inline void write_msr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" ::"c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void syscall_init_cpu(void) {
    uint64_t efer = read_msr(IA32_EFER);
    if (!(efer & EFER_LME)) {
        printf("[cpu] ERROR: Not in long mode!\n");
        return;
    }
    efer |= EFER_SCE;
    write_msr(IA32_EFER, efer);

    // STAR MSR
    uint64_t star = 0;
    star |= (uint64_t)0x0008ULL << 32; // syscall:  kernel CS = 0x08, SS = 0x10
    star |= (uint64_t)0x0018ULL << 48; // sysretq: user CS = 0x1B, SS = 0x23
    write_msr(IA32_STAR, star);

    // LSTAR
    uint64_t entry = (uint64_t)(uintptr_t)syscall_entry;
    write_msr(IA32_LSTAR, entry);

    // SFMASK: clear IF, DF, TF on syscall entry
    write_msr(IA32_SFMASK, (1ULL << 9) | (1ULL << 10) | (1ULL << 8));

    printf("[cpu] syscall enabled: LSTAR=0x%llx STAR=0x%llx\n", (unsigned long long)entry,
           (unsigned long long)star);
}

void install_tss(void) {
    uint64_t tss_base = (uint64_t)tss64;
    uint32_t tss_limit = (uint32_t)(tss64_end - tss64 - 1);

    // Set RSP0 (required for ring 0 stack on privilege change)
    *(uint64_t*)(tss64 + 4) = (uint64_t)kernel_stack_top;

    printf("[tss] === TSS SETUP START ===\n");
    printf("[tss] tss64 symbol = %p, tss_end = %p, limit = 0x%x\n", (void*)tss64, (void*)tss64_end,
           tss_limit);
    printf("[tss] kernel_stack_top = %p\n", (void*)kernel_stack_top);
    printf("[tss] gdt64 symbol = %p\n", (void*)gdt64);

    // Write the 16-byte (two GDT slots) TSS descriptor
    uint64_t* desc = (uint64_t*)(gdt64 + TSS_GDT_OFFSET); // 0x28

    uint64_t low = 0;
    low |= (tss_limit & 0xFFFFULL);
    low |= ((tss_base & 0xFFFFFFULL) << 16);
    low |= (0x89ULL << 40); // Present, DPL=0, 64-bit TSS available
    low |= (((tss_base >> 24) & 0xFFULL) << 56);

    uint64_t high = (tss_base >> 32);

    desc[0] = low;
    desc[1] = high;

    printf("[tss] Wrote TSS descriptor: low=0x%llx high=0x%llx\n", low, high);

    // CRITICAL: Reload the GDTR from the descriptor in memory
    __asm__ volatile("lgdt (%0)" ::"r"(gdt64_descriptor) : "memory");

    // Load the Task Register
    __asm__ volatile("ltr %0" ::"r"((uint16_t)0x28) : "memory");

    uint16_t tr = 0;
    __asm__ volatile("str %0" : "=r"(tr));

    printf("[tss] After ltr: TR = 0x%04x  (EXPECTED: 0x0028)\n", tr);

    if (tr != 0x28) {
        printf("[tss] CRITICAL ERROR: TR not loaded! Halting.\n");
        while (1)
            __asm__ volatile("hlt");
    }

    printf("[tss] === TSS SETUP SUCCESS ===\n");
}

void setup_long_mode(uint64_t entry, uint64_t stack) {
    // Already in long mode — nothing to do here.
    // install_tss() is called from kernel_main before enter_user_mode.
    (void)entry;
    (void)stack;
}

void enable_sse_for_userland(void) {
    uint64_t cr0, cr4;

    // CR0: clear EM (bit 2 = no FPU emulation), set MP (bit 1)
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // clear EM
    cr0 |= (1ULL << 1);  // set MP
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

    // CR4: set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // OSFXSR   — enables SSE instructions
    cr4 |= (1ULL << 10); // OSXMMEXCPT — enables SSE exception handling
    __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

    // Initialize x87 FPU state
    __asm__ volatile("fninit");

    printf("[cpu] SSE enabled for userland\n");
}