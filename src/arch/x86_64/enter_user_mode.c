#include <stdint.h>
#include <meow/enter_user_mode.h>

// Assembly stub
extern void enter_user_mode_asm(uint64_t entry, uint64_t stack);

void enter_user_mode(uint64_t entry, uint64_t stack) {
    enter_user_mode_asm(entry, stack);
    __builtin_unreachable();
}
