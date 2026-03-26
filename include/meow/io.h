#pragma once

#include <stdint.h>
#include <stddef.h>

#define EOF (-1)

uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
void outb(uint16_t port, uint8_t val);
void outw(uint16_t port, uint16_t val);
void outl(uint16_t port, uint32_t val);
void io_wait(void);

void putchar(int c);
int puts(const char* str);
void printf(const char* format, ...);

char getchar();
void read_line(char* buffer, size_t max_length);
int atoi(const char* str);
void kb_push(char c);
int kb_pop();