#pragma once
#include <stdint.h>

void install_tss(void);
void syscall_init_cpu(void);
void enable_sse_for_userland(void);
void setup_long_mode(uint64_t entry, uint64_t stack);