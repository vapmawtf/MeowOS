#pragma once
#include <stdint.h>


struct IDT_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDT_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void set_idt_gate(int n, uint64_t handler);
void set_idt_gate_user(int n, uint64_t handler);
void idt_install();
