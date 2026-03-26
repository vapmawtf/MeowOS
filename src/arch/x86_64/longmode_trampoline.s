    .section .text
    .global longmode_trampoline
    .type longmode_trampoline, @function

longmode_trampoline:
    mov %rsi, %rsp        # Set up stack pointer
    xor %rbp, %rbp        # Clear base pointer
    jmp *%rdi             # Jump to entry point
