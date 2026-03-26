#pragma once
#include <stdint.h>

struct IDT_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

void set_idt_gate(int n, uint32_t handler);
void set_idt_gate_user(int n, uint32_t handler);
void idt_install();
