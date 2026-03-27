#pragma once
#include <stdint.h>

struct interrupt_frame
{
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};