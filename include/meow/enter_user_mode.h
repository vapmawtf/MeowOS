#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call from kernel after loading ELF64 and mapping stack
// entry: user entry point (RIP)
// stack: user stack pointer (RSP)
void enter_user_mode(uint64_t entry, uint64_t stack);

#ifdef __cplusplus
}
#endif
