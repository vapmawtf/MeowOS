.section .text
.global longmode_jump_to_64
.type longmode_jump_to_64, @function

# void longmode_jump_to_64(uint64_t trampoline, uint64_t entry, uint64_t stack, uint64_t gdt_ptr)
# rdi = trampoline, rsi = entry, rdx = stack, rcx = gdt_ptr
longmode_jump_to_64:
    lgdt (%rcx)
    mov %rdx, %rsp
    pushq $0x28           # 64-bit data segment selector (must match your GDT)
    pushq %rdx            # Stack pointer
    pushq $0x08           # 64-bit code segment selector (must match your GDT)
    pushq %rsi            # Entry point
    pushq $0               # RFLAGS
    mov %rdi, %rax        # Trampoline address
    lretq                 # Far return to 64-bit code
