.global _start
.extern kernel_main

.section .text
_start:
    cli
    xor %ebp, %ebp
    mov $0x200000, %esp
    and $0xFFFFFFF0, %esp
    push %ebx
    push %eax
    call kernel_main
    add $8, %esp

.hlt_loop:
    hlt
    jmp .hlt_loop