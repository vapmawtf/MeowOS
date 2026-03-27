#include <meow/io.h>
#include <meow/vga.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────
// Port I/O
// ─────────────────────────────────────────────

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" ::"a"(val), "Nd"(port));
}

void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" ::"a"(val), "Nd"(port));
}

void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" ::"a"(val), "Nd"(port));
}

void io_wait(void) {
    outb(0x80, 0);
}

// ─────────────────────────────────────────────
// Serial (COM1) — debug output
// ─────────────────────────────────────────────

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); // disable interrupts
    outb(COM1 + 3, 0x80); // enable DLAB
    outb(COM1 + 0, 0x03); // baud divisor low  (38400)
    outb(COM1 + 1, 0x00); // baud divisor high
    outb(COM1 + 3, 0x03); // 8N1, clear DLAB
    outb(COM1 + 2, 0xC7); // enable + clear FIFO
    outb(COM1 + 4, 0x0B); // RTS/DSR
}

static void serial_putchar(char c) {
    // wait for transmit holding register empty
    while ((inb(COM1 + 5) & 0x20) == 0)
        ;
    outb(COM1, (uint8_t)c);
}

static void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n')
            serial_putchar('\r');
        serial_putchar(*s++);
    }
}

// ─────────────────────────────────────────────
// printf — full rewrite
// Supports: %c %s %d %u %x %X %p %zu %llu %02x padding
// ─────────────────────────────────────────────

void putchar(int c) {
    char out[2] = { (char)c, '\0' };
    printstr(out);
    serial_putchar((char)c);
}

int puts(const char* str) {
    printstr(str ? str : "(null)");
    printstr("\n");
    serial_puts(str ? str : "(null)");
    serial_putchar('\n');
    return 0;
}

// Write a single char to both VGA and serial without going through putchar
static void emit(char c) {
    char buf[2] = { c, '\0' };
    printstr(buf);
    serial_putchar(c);
}

static void emit_str(const char* s) {
    if (!s)
        s = "(null)";
    while (*s)
        emit(*s++);
}

// Render an unsigned 64-bit integer into buf (null-terminated).
// Returns length. buf must be at least 22 bytes.
static int fmt_uint64(char* buf, uint64_t val, unsigned base, int upper) {
    const char* lo = "0123456789abcdef";
    const char* hi = "0123456789ABCDEF";
    const char* digits = upper ? hi : lo;
    char tmp[22];
    int i = 0;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    while (val) {
        tmp[i++] = digits[val % base];
        val /= base;
    }
    int len = i;
    for (int j = 0; j < len; j++)
        buf[j] = tmp[len - 1 - j];
    buf[len] = '\0';
    return len;
}

void printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            emit(*fmt++);
            continue;
        }
        fmt++; // skip '%'

        // ── flags ──
        int zero_pad = 0;
        int left_align = 0;
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0')
                zero_pad = 1;
            if (*fmt == '-')
                left_align = 1;
            fmt++;
        }

        // ── width ──
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        // ── length modifier ──
        int is_long = 0; // l
        int is_size = 0; // z
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            } // ll
        } else if (*fmt == 'z') {
            is_size = 1;
            fmt++;
        }

        char spec = *fmt++;
        if (spec == '\0')
            break;

        char numbuf[24];
        const char* s = NULL;
        int neg = 0;

        switch (spec) {
            case 'c':
                emit((char)va_arg(ap, int));
                continue;

            case 's':
                s = va_arg(ap, const char*);
                if (!s)
                    s = "(null)";
                if (width == 0 || left_align) {
                    emit_str(s);
                } else {
                    int slen = 0;
                    for (const char* p = s; *p; p++)
                        slen++;
                    for (int i = slen; i < width; i++)
                        emit(' ');
                    emit_str(s);
                }
                continue;

            case 'd': {
                int64_t val;
                if (is_long == 2)
                    val = va_arg(ap, long long);
                else if (is_long == 1)
                    val = va_arg(ap, long);
                else
                    val = va_arg(ap, int);
                if (val < 0) {
                    neg = 1;
                    val = -val;
                }
                fmt_uint64(numbuf, (uint64_t)val, 10, 0);
                break;
            }
            case 'u': {
                uint64_t val;
                if (is_long == 2)
                    val = va_arg(ap, unsigned long long);
                else if (is_long == 1)
                    val = va_arg(ap, unsigned long);
                else if (is_size)
                    val = va_arg(ap, size_t);
                else
                    val = va_arg(ap, unsigned int);
                fmt_uint64(numbuf, val, 10, 0);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t val;
                if (is_long == 2)
                    val = va_arg(ap, unsigned long long);
                else if (is_long == 1)
                    val = va_arg(ap, unsigned long);
                else if (is_size)
                    val = va_arg(ap, size_t);
                else
                    val = va_arg(ap, unsigned int);
                fmt_uint64(numbuf, val, 16, spec == 'X');
                break;
            }
            case 'p': {
                uintptr_t val = (uintptr_t)va_arg(ap, void*);
                emit('0');
                emit('x');
                fmt_uint64(numbuf, (uint64_t)val, 16, 0);
                // always pad pointers to 16 hex digits
                int plen = 0;
                for (char* p = numbuf; *p; p++)
                    plen++;
                for (int i = plen; i < 16; i++)
                    emit('0');
                emit_str(numbuf);
                continue;
            }
            case '%':
                emit('%');
                continue;
            default:
                emit('%');
                emit(spec);
                continue;
        }

        // ── emit number with padding ──
        s = numbuf;
        int nlen = 0;
        for (const char* p = s; *p; p++)
            nlen++;
        int total = nlen + neg;
        char pad = (zero_pad && !left_align) ? '0' : ' ';

        if (!left_align)
            for (int i = total; i < width; i++)
                emit(pad);
        if (neg)
            emit('-');
        emit_str(s);
        if (left_align)
            for (int i = total; i < width; i++)
                emit(' ');
    }

    va_end(ap);
}

// ─────────────────────────────────────────────
// Keyboard — IRQ-driven with interrupt handler
// ─────────────────────────────────────────────

#define KEYBOARD_BUFFER_SIZE 256
#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

// US QWERTY scancode set 1 — unshifted
static const char kbd_map_normal[128] = {
    [0x02] = '1',  [0x03] = '2',  [0x04] = '3',  [0x05] = '4',  [0x06] = '5', [0x07] = '6',
    [0x08] = '7',  [0x09] = '8',  [0x0A] = '9',  [0x0B] = '0',  [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'q',  [0x11] = 'w',  [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't',  [0x15] = 'y',  [0x16] = 'u',  [0x17] = 'i',  [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[',  [0x1B] = ']',  [0x1C] = '\n', [0x1E] = 'a',  [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f',  [0x22] = 'g',  [0x23] = 'h',  [0x24] = 'j',  [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';',  [0x28] = '\'', [0x29] = '`',  [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
    [0x2E] = 'c',  [0x2F] = 'v',  [0x30] = 'b',  [0x31] = 'n',  [0x32] = 'm', [0x33] = ',',
    [0x34] = '.',  [0x35] = '/',  [0x39] = ' ',
};

// US QWERTY scancode set 1 — shifted
static const char kbd_map_shift[128] = {
    [0x02] = '!',  [0x03] = '@',  [0x04] = '#',  [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&',  [0x09] = '*',  [0x0A] = '(',  [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'Q',  [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T',  [0x15] = 'Y',  [0x16] = 'U',  [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{',  [0x1B] = '}',  [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F',  [0x22] = 'G',  [0x23] = 'H',  [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':',  [0x28] = '"',  [0x29] = '~',  [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
    [0x2E] = 'C',  [0x2F] = 'V',  [0x30] = 'B',  [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>',  [0x35] = '?',  [0x39] = ' ',
};

static volatile char kb_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;
static volatile int kb_shift = 0;
static volatile int kb_caps = 0;

void kb_push(char c) {
    uint32_t next = (kb_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != kb_tail) {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
    // if full, silently drop — never block in an IRQ handler
}

int kb_pop(void) {
    // drain hardware buffer even if ring buffer is full
    if ((inb(KBD_STATUS_PORT) & 0x01u) != 0) {
        uint8_t sc = inb(KBD_DATA_PORT);
        int release = (sc & 0x80u) != 0;
        uint8_t key = sc & 0x7Fu;

        // track shift keys (left=0x2A right=0x36)
        if (key == 0x2A || key == 0x36) {
            kb_shift = !release;
            return -1;
        }
        // caps lock toggle on press
        if (key == 0x3A && !release) {
            kb_caps = !kb_caps;
            return -1;
        }

        if (!release) {
            char c = kb_shift ? kbd_map_shift[key] : kbd_map_normal[key];
            // apply caps lock to letters
            if (kb_caps && c >= 'a' && c <= 'z')
                c -= 32;
            if (kb_caps && c >= 'A' && c <= 'Z' && !kb_shift)
                c += 32;
            if (c != 0) {
                kb_push(c);
            }
        }
    }

    if (kb_head == kb_tail)
        return -1;
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return (int)(unsigned char)c;
}

char getchar(void) {
    int c;
    while ((c = kb_pop()) < 0)
        __asm__ volatile("pause");
    return (char)c;
}

void read_line(char* buffer, size_t max_length) {
    if (!buffer || max_length == 0)
        return;
    size_t idx = 0;
    while (idx < max_length - 1) {
        char c = getchar();
        if (c == '\r' || c == '\n') {
            emit('\n');
            break;
        }
        if ((c == '\b' || c == 127) && idx > 0) {
            idx--;
            emit('\b');
            emit(' ');
            emit('\b'); // erase character on screen
            continue;
        }
        if (c >= ' ' && (unsigned char)c <= 126) {
            buffer[idx++] = c;
            emit(c);
        }
    }
    buffer[idx] = '\0';
}

int atoi(const char* str) {
    if (!str)
        return 0;
    int result = 0, sign = 1;
    while (*str == ' ' || *str == '\t')
        str++;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+')
        str++;
    while (*str >= '0' && *str <= '9')
        result = result * 10 + (*str++ - '0');
    return result * sign;
}