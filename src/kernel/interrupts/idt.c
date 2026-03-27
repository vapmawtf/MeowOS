#include <stdint.h>
#include <meow/io.h>

struct IDT_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDT_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void isr_default_stub();
extern void isr_gp_stub();
extern void isr_pf_stub();
extern void isr_df_stub();
extern void irq0_stub();
extern void irq1_stub();
extern void syscall_stub();

static struct IDT_entry idt[256];
static struct IDT_ptr idt_ptr;
static uint16_t code_selector;

static void set_gate(int n, uint64_t handler, uint8_t flags)
{
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = code_selector;
    idt[n].ist         = 0;
    idt[n].type_attr   = flags;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero        = 0;
}

void set_idt_gate(int n, uint64_t handler)
{
    set_gate(n, handler, 0x8E);
}

void set_idt_gate_user(int n, uint64_t handler)
{
    set_gate(n, handler, 0xEE);
}

void idt_install()
{
    __asm__ volatile("mov %%cs, %0" : "=r"(code_selector));

    for (int i = 0; i < 256; i++)
    {
        set_gate(i, (uint64_t)isr_default_stub, 0x8E);
    }

    // wyjątki
    set_gate(13, (uint64_t)isr_gp_stub, 0x8E);
    set_gate(14, (uint64_t)isr_pf_stub, 0x8E);
    set_gate(8,  (uint64_t)isr_df_stub, 0x8E);

    // IRQ
    set_gate(0x20, (uint64_t)irq0_stub, 0x8E);
    set_gate(0x21, (uint64_t)irq1_stub, 0x8E);

    // syscall (user)
    set_gate(0x80, (uint64_t)syscall_stub, 0xEE);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    // debug
    struct IDT_ptr check;
    __asm__ volatile("sidt %0" : "=m"(check));
    printf("IDT loaded at: %llx\n", check.base);
}