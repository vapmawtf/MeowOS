void _start(void) {
    asm volatile("mov $60, %rax"); // syscall number for exit
    asm volatile("mov $42, %rdi"); // exit code
    asm volatile("syscall");
    while (1); // should never reach here
}