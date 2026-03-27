.globl enter_user_mode_asm
.type enter_user_mode_asm, @function
enter_user_mode_asm:
    # RDI = user RIP
    # RSI = user RSP

    cli

    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    cld

    # Prepare RFLAGS for iretq
    pushfq
    pop %rax
    orq $0x200, %rax        # IF = 1 (enable interrupts)
    andq $~0x400, %rax      # DF = 0 (clear direction flag)

    # Build correct iretq stack frame
    # iretq expects (from bottom to top): RIP, CS, RFLAGS, RSP, SS
    pushq $0x23             # SS (data segment selector for ring 3)
    pushq %rsi              # RSP (user mode stack pointer)
    pushq %rax              # RFLAGS
    pushq $0x1B             # CS (code segment selector for ring 3)
    pushq %rdi              # RIP (user entry point)

    iretq

# Jump to user code within kernel mode (for development/debugging)
# RDI = user entry point
# RSI = user RSP (with argc/argv already on stack)
.globl call_user_code_kernel_mode
.type call_user_code_kernel_mode, @function
call_user_code_kernel_mode:
    # Set RSP to user stack, then jump to entry point
    mov %rsi, %rsp         # Set stack pointer
    xor %rbp, %rbp         # Clear frame pointer (entry point expects this)
    jmp *%rdi              # Jump to entry point (no return address pushed)