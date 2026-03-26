#pragma once

#include <stdint.h>

void gdt_init(void);
// enter_user_mode is not used in 64-bit mode; user mode transition is handled differently.
