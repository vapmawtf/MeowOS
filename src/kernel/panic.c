#include <meow/io.h>
#include <meow/panic.h>

void kernel_panic(const char* message)
{
    printf("KERNEL PANIC: %s\n", message ? message : "No message");
    while (1)
    {
        __asm__ volatile ("hlt");
    }
}