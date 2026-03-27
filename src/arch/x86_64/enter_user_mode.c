#include <stdint.h>
#include <meow/enter_user_mode.h>
#include <meow/io.h>
#include <meow/panic.h>

// Assembly stub
extern void enter_user_mode_asm(uint64_t entry, uint64_t stack);

void enter_user_mode(uint64_t entry, uint64_t stack) {
    // Align the user stack to 16 bytes (x86-64 ABI requirement)
    uint64_t aligned_stack = stack & ~0xFULL;

    printf("Entering user mode at 0x%llx with aligned stack 0x%llx...\n", entry, aligned_stack);

    enter_user_mode_asm(entry, aligned_stack);
    kernel_panic("enter_user_mode_asm returned, which should never happen");
}