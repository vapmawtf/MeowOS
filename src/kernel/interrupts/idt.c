#include <meow/idt.h>

extern void default_interrupt_stub();


struct IDT_entry idt[256];
static uint16_t idt_code_selector;

void set_idt_gate(int n, uint64_t handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = idt_code_selector;
    idt[n].ist = 0;
    idt[n].type_attr = 0x8E; // 64-bit interrupt gate, present
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

void set_idt_gate_user(int n, uint64_t handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = idt_code_selector;
    idt[n].ist = 0;
    idt[n].type_attr = 0xEE; // 64-bit interrupt gate, present, DPL=3
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}


static struct IDT_ptr idt_ptr;

void idt_install() {
    uint16_t cs;
    __asm__ volatile("movw %%cs, %0" : "=r"(cs));
    idt_code_selector = cs;

    for (int i = 0; i < 256; i++) {
        set_idt_gate(i, (uint64_t)default_interrupt_stub);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)(uintptr_t)&idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}