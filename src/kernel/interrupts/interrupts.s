.intel_syntax noprefix
.section .text

.global isr_default_stub
.global isr_gp_stub
.global isr_pf_stub
.global isr_df_stub
.global irq0_stub
.global irq1_stub
.global syscall_stub

.extern default_interrupt_handler
.extern irq0_handler
.extern irq1_handler
.extern syscall_handle

# =====================================================
# COMMON MACROS (manual, żeby było czytelne)
# =====================================================

# push wszystkich rejestrów
.macro PUSH_ALL
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
.endm

.macro POP_ALL
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
.endm

# =====================================================
# DEFAULT EXCEPTION (NO ERROR CODE)
# =====================================================

isr_default_stub:
    cli
    mov rdi, rsp              # przekaż frame
    call default_interrupt_handler
    iretq

# =====================================================
# EXCEPTIONS WITH ERROR CODE
# =====================================================

isr_gp_stub:
    cli
    add rsp, 8                # ❗ usuń error code
    mov rdi, rsp
    call default_interrupt_handler
    iretq

isr_pf_stub:
    cli
    add rsp, 8
    mov rdi, rsp
    call default_interrupt_handler
    iretq

isr_df_stub:
    cli
    add rsp, 8
    mov rdi, rsp
    call default_interrupt_handler
    iretq

# =====================================================
# IRQ0 (TIMER)
# =====================================================

irq0_stub:
    PUSH_ALL

    mov rdi, rsp
    call irq0_handler

    POP_ALL
    iretq

# =====================================================
# IRQ1 (KEYBOARD)
# =====================================================

irq1_stub:
    PUSH_ALL

    mov rdi, rsp
    call irq1_handler

    POP_ALL
    iretq

# =====================================================
# SYSCALL (INT 0x80)
# =====================================================

syscall_stub:
    PUSH_ALL

    # rax = syscall number
    # rdi, rsi, rdx, r10, r8, r9 = args

    call syscall_handle

    # rax = return value

    POP_ALL
    iretq