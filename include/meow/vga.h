#pragma once

#include <stdint.h>

#define VIDEO 0xB8000
#define WIDTH 80
#define HEIGHT 25

void vga_init(uint32_t multiboot_magic, uint32_t multiboot_info_addr);
void printstr(const char* str);
void vga_clear();
