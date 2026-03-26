#include <meow/io.h>
#include <meow/vga.h>
#include <stdarg.h>

uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

void io_wait(void)
{
    outb(0x80, 0);
}

void putchar(int c)
{
    char out[2] = {(char)c, '\0'};
    printstr(out);
}

int puts(const char* str)
{
    printstr(str);
    printstr("\n");
    return 0;
}

static void print_unsigned(unsigned int value, int base)
{
    char buffer[32];
    int index = 0;

    if (value == 0)
    {
        putchar('0');
        return;
    }

    const char* digits = "0123456789ABCDEF";
    while (value > 0)
    {
        buffer[index++] = digits[value % base];
        value /= base;
    }

    while (index > 0)
    {
        putchar(buffer[--index]);
    }
}

static void print_int(int value)
{
    char buffer[12];
    int index = 0;
    unsigned int uvalue;

    if (value < 0)
    {
        putchar('-');
        uvalue = (unsigned int)(- (long)value);
    }
    else
    {
        uvalue = (unsigned int)value;
    }

    if (uvalue == 0)
    {
        putchar('0');
        return;
    }

    while (uvalue > 0)
    {
        buffer[index++] = '0' + (uvalue % 10);
        uvalue /= 10;
    }

    while (index > 0)
    {
        putchar(buffer[--index]);
    }
}

void printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    while (*format)
    {
        if (*format == '%')
        {
            format++;

            if (*format == '\0')
            {
                break;
            }

            if (*format == 's')
            {
                const char* str = va_arg(args, const char*);
                printstr(str ? str : "(null)");
            }
            else if (*format == 'd')
            {
                print_int(va_arg(args, int));
            }
            else if (*format == 'c')
            {
                putchar(va_arg(args, int));
            }
            else if (*format == '%')
            {
                putchar('%');
            }
            else if (*format == 'u')
            {
                print_unsigned(va_arg(args, unsigned int), 10);
            }
            else if (*format == 'x')
            {
                print_unsigned(va_arg(args, unsigned int), 16);
            }
            else
            {
                putchar('%');
                putchar(*format);
            }
        }
        else
        {
            putchar(*format);
        }

        format++;
    }

    va_end(args);
}

char getchar()
{
    while (1)
    {
        int c = kb_pop();
        if (c >= 0)
        {
            return (char)c;
        }
    }
}

void read_line(char* buffer, size_t max_length)
{
    size_t index = 0;
    while (index < max_length - 1)
    {
        char c = getchar();

        if (c == '\r' || c == '\n')
        {
            putchar('\n');
            break;
        }
        else if (c == '\b' || c == 127)
        {
            if (index > 0)
            {
                index--;
                printf("\b \b");
            }
        }
        else if (c >= ' ' && c <= '~')
        {
            buffer[index++] = c;
            putchar(c); // echo
        }
    }
    buffer[index] = '\0';
}

int atoi(const char* str)
{
    int result = 0;
    int sign = 1;

    while (*str == ' ' || *str == '\t') str++;

    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }

    while (*str >= '0' && *str <= '9')
    {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

#define KEYBOARD_BUFFER_SIZE 128

static volatile char kb_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

void kb_push(char c)
{
    int next = (kb_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != kb_tail) // jeśli nie przepełniony
    {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
}

int kb_pop()
{
    if (kb_head == kb_tail) return -1; // pusty
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}