#include <meow/io.h>
#include <meow/pic.h>
#include <meow/idt.h>
#include <meow/syscall.h>

#define KEYBOARD_DATA 0x60
#define PIC_EOI 0x20
extern void irq0_stub();
extern void irq1_stub();
extern void syscall_stub();

static const char kbd_map[128] = {
    [0x01] = 27,  [0x02] = '1',  [0x03] = '2',  [0x04] = '3',  [0x05] = '4',  [0x06] = '5',
    [0x07] = '6', [0x08] = '7',  [0x09] = '8',  [0x0A] = '9',  [0x0B] = '0',  [0x0C] = '-',
    [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'q',  [0x11] = 'w',  [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't',  [0x15] = 'y',  [0x16] = 'u',  [0x17] = 'i',  [0x18] = 'o',
    [0x19] = 'p', [0x1A] = '[',  [0x1B] = ']',  [0x1C] = '\n', [0x1E] = 'a',  [0x1F] = 's',
    [0x20] = 'd', [0x21] = 'f',  [0x22] = 'g',  [0x23] = 'h',  [0x24] = 'j',  [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';',  [0x28] = '\'', [0x29] = '`',  [0x2B] = '\\', [0x2C] = 'z',
    [0x2D] = 'x', [0x2E] = 'c',  [0x2F] = 'v',  [0x30] = 'b',  [0x31] = 'n',  [0x32] = 'm',
    [0x33] = ',', [0x34] = '.',  [0x35] = '/',  [0x39] = ' '
};

static const char kbd_shift_map[128] = {
    [0x01] = 27,  [0x02] = '!',  [0x03] = '@',  [0x04] = '#',  [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&',  [0x09] = '*',  [0x0A] = '(',  [0x0B] = ')', [0x0C] = '_',
    [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'Q',  [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T',  [0x15] = 'Y',  [0x16] = 'U',  [0x17] = 'I', [0x18] = 'O',
    [0x19] = 'P', [0x1A] = '{',  [0x1B] = '}',  [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S',
    [0x20] = 'D', [0x21] = 'F',  [0x22] = 'G',  [0x23] = 'H',  [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':',  [0x28] = '"',  [0x29] = '~',  [0x2B] = '|', [0x2C] = 'Z',
    [0x2D] = 'X', [0x2E] = 'C',  [0x2F] = 'V',  [0x30] = 'B',  [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>',  [0x35] = '?',  [0x39] = ' '
};

static uint8_t shift_pressed;
static uint8_t caps_lock;
static uint8_t extended_prefix;

static char apply_caps_lock(char c) {
    if (!caps_lock) {
        return c;
    }

    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }

    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }

    return c;
}

void default_interrupt_handler() {
    __asm__ volatile("cli");
    printf("\nUnhandled interrupt - system halted\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}

void irq0_handler() {
    outb(PIC1_COMMAND, PIC_EOI);
}

void irq1_handler() {
    uint8_t scancode = inb(KEYBOARD_DATA);

    if (scancode == 0xE0) {
        extended_prefix = 1;
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    if (extended_prefix) {
        extended_prefix = 0;
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    uint8_t released = (scancode & 0x80u) != 0;
    uint8_t code = scancode & 0x7Fu;

    if (code == 0x2A || code == 0x36) {
        shift_pressed = (uint8_t)!released;
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    if (released) {
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    if (code == 0x3A) {
        caps_lock ^= 1u;
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    char c = shift_pressed ? kbd_shift_map[code] : kbd_map[code];
    c = apply_caps_lock(c);

    if (c) {
        kb_push(c);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}

void interrupts_init() {
    __asm__ volatile("cli");

    idt_install();
    pic_remap();
    set_idt_gate(0x20, (uint32_t)irq0_stub);
    set_idt_gate(0x21, (uint32_t)irq1_stub);
    set_idt_gate_user(0x80, (uint32_t)syscall_stub);
    syscall_init();

    // Enable IRQ0/IRQ1 and keep IRQ2 (cascade to slave PIC) unmasked.
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xFF);

    __asm__ volatile("sti");
}