#include <stdint.h>
#include <stddef.h>

// Symbols from boot.s
extern uint8_t gdt64[];
extern uint8_t gdt64_descriptor[];
extern uint8_t tss64[];
extern uint8_t tss64_end[];
extern uint8_t kernel_stack_top[];

// TSS descriptor occupies GDT slot at byte offset 0x28
// (after null + kernel code + kernel data + user code + user data)
#define TSS_GDT_OFFSET 0x28

void install_tss(void) {
    uint64_t base  = (uint64_t)tss64;
    uint16_t limit = (uint16_t)(tss64_end - tss64 - 1);

    // Patch RSP0 in the TSS — this is the kernel stack the CPU
    // switches to when an exception/syscall arrives from ring 3.
    // RSP0 is at TSS byte offset +4 (after the reserved .long).
    uint64_t *rsp0 = (uint64_t *)(tss64 + 4);
    *rsp0 = (uint64_t)kernel_stack_top;

    // Encode the 16-byte 64-bit TSS descriptor into GDT[0x28].
    // Intel SDM Vol.3 Figure 8-4 layout:
    //   [1:0]   limit[15:0]
    //   [4:2]   base[23:0]
    //   [5]     type=0x89 (Present, DPL=0, 64-bit TSS Available)
    //   [6]     G=0, limit[19:16]=0
    //   [7]     base[31:24]
    //   [11:8]  base[63:32]
    //   [15:12] reserved (zero)
    uint8_t *d = gdt64 + TSS_GDT_OFFSET;
    d[0]  = (uint8_t)(limit & 0xFF);
    d[1]  = (uint8_t)((limit >> 8) & 0xFF);
    d[2]  = (uint8_t)(base & 0xFF);
    d[3]  = (uint8_t)((base >> 8) & 0xFF);
    d[4]  = (uint8_t)((base >> 16) & 0xFF);
    d[5]  = 0x89;   // Present=1, DPL=0, Type=9 (64-bit TSS available)
    d[6]  = 0x00;
    d[7]  = (uint8_t)((base >> 24) & 0xFF);
    d[8]  = (uint8_t)((base >> 32) & 0xFF);
    d[9]  = (uint8_t)((base >> 40) & 0xFF);
    d[10] = (uint8_t)((base >> 48) & 0xFF);
    d[11] = (uint8_t)((base >> 56) & 0xFF);
    d[12] = 0;
    d[13] = 0;
    d[14] = 0;
    d[15] = 0;

    // Reload GDT so CPU sees the patched TSS descriptor
    __asm__ volatile("lgdt (%0)" :: "r"(gdt64_descriptor) : "memory");

    // Load Task Register with TSS selector 0x28
    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x28));
}

void setup_long_mode(uint64_t entry, uint64_t stack) {
    // Already in long mode — nothing to do here.
    // install_tss() is called from kernel_main before enter_user_mode.
    (void)entry;
    (void)stack;
}