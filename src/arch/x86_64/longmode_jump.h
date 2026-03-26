#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// trampoline: address of the 64-bit trampoline function
// entry: entry point of the ELF64 payload
// stack: stack pointer for the new context
// gdt_ptr: pointer to a GDT descriptor (struct { uint16_t limit; uint64_t base; })
void longmode_jump_to_64(uint64_t trampoline, uint64_t entry, uint64_t stack, uint64_t gdt_ptr);

#ifdef __cplusplus
}
#endif
