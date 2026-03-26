.section .text

# void longmode_trampoline(uint64_t entry, uint64_t stack)
# rdi = entry point, rsi = stack pointer
.global longmode_trampoline
.type longmode_trampoline, @function
longmode_trampoline:
    mov %rsi, %rsp
    and $-16, %rsp
    jmp *%rdi