#include <stddef.h>
#include <stdint.h>
#include <meow/vga.h>
#include <meow/io.h>
#include <meow/isr.h>
#include <meow/userland/init.h>
#include <meow/panic.h>

__attribute__((section(".multiboot")))
const unsigned int multiboot_header[] =
{
    0x1BADB002,
    0x07,
    -(0x1BADB002 + 0x07),
    0,
    1024,
    768,
    32
};

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    vga_init(multiboot_magic, multiboot_info_addr);
    vga_clear();

    interrupts_init();

    init_userland();

    kernel_panic("Returned from userland init, which should never happen");
}