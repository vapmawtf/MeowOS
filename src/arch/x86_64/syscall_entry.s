.global syscall_entry
.extern syscall_handle

# ─────────────────────────────────────────────────────────────────────────────
# syscall entry point — jumped to by the CPU when userland executes `syscall`
#
# On entry (Linux x86-64 ABI):
#   rax = syscall number
#   rdi = arg0,  rsi = arg1,  rdx = arg2
#   r10 = arg3,  r8  = arg4,  r9  = arg5
#   rcx = saved RIP (by CPU),  r11 = saved RFLAGS (by CPU)
#   CS/SS are set to kernel selectors by STAR MSR
#   interrupts are disabled (masked by SFMASK)
#   we are still on the USER stack — must switch to kernel stack
#
# syscall_handle(num, a0, a1, a2, a3, a4, a5) uses System V AMD64 ABI:
#   rdi, rsi, rdx, rcx, r8, r9
# So we need to shuffle: rax→rdi, rdi→rsi, rsi→rdx, rdx→rcx, r10→r8
# ─────────────────────────────────────────────────────────────────────────────

.section .text
.align 16
syscall_entry:
    # swap to kernel stack using swapgs + per-cpu scratch, OR simply use
    # the known kernel stack address. We use a simple per-CPU scratch MSR
    # (IA32_KERNEL_GS_BASE / swapgs) approach via a static kernel stack.
    #
    # Simple approach: save user RSP in a scratch slot, load kernel RSP.
    mov %rsp, user_rsp(%rip)
    lea kernel_syscall_stack_top(%rip), %rsp

    # Save all caller-saved + syscall-clobbered registers
    # We save rcx (user RIP) and r11 (user RFLAGS) so sysretq can restore them.
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11    # user RFLAGS
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rbx
    push %rcx    # user RIP
    push %rdx
    push %rsi
    push %rdi
    push %rax    # syscall number

    # Reenable interrupts while in kernel (SFMASK disabled them on entry)
    # (temporary debug: keep IF=0 to avoid nested IRQ during syscall)
    # sti

    # Set up arguments for syscall_handle:
    # syscall_handle(uint64_t num, a0, a1, a2, a3, a4, a5)
    # SysV:          rdi          rsi  rdx  rcx  r8  r9  (7th on stack)
    mov %rax, %rdi   # num
    # rsi = rdi (arg0) — already in rsi... but we pushed rdi, reload
    mov 8(%rsp),  %rsi   # arg0 = original rdi
    mov 16(%rsp), %rdx   # arg1 = original rsi
    mov 24(%rsp), %rcx   # arg2 = original rdx
    mov %r10,     %r8    # arg3 = r10
    # r9 already = arg4, push arg5 (r9 original) — use 0 for now
    push $0              # arg5 placeholder
    push %r9             # arg4

    call syscall_handle

    add $16, %rsp        # pop arg4+arg5

    # Store return value — will go back in rax via sysretq
    mov %rax, 0(%rsp)    # overwrite saved rax slot with return value

    cli

    # Restore registers
    pop %rax     # return value
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx     # user RIP for sysretq
    pop %rbx
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11     # user RFLAGS for sysretq
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    # Restore user stack
    mov user_rsp(%rip), %rsp

    sysretq      # returns to rcx (user RIP), restores rflags from r11, CPL→3

# ─────────────────────────────────────────────────────────────────────────────
# Per-entry scratch storage and kernel stack
# ─────────────────────────────────────────────────────────────────────────────
.align 8
user_rsp:
    .quad 0

.align 16
    .skip 32768          # 32 KB kernel syscall stack
kernel_syscall_stack_top: