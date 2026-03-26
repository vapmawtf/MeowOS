.global irq0_stub
.global irq1_stub
.global default_interrupt_stub
.global syscall_stub
.extern irq0_handler
.extern irq1_handler
.extern syscall_handle

.section .text
irq0_stub:
    pushal
    call irq0_handler
    popal
    iret

irq1_stub:
    pushal
    call irq1_handler
    popal
    iret

default_interrupt_stub:
    cli
.default_halt:
    hlt
    jmp .default_halt

syscall_stub:
    pushal
    movl %esp, %ebp

    # C signature: syscall_handle(num, arg0, arg1, arg2, arg3, arg4, arg5)
    pushl 8(%ebp)   # arg5: ebp
    pushl 0(%ebp)   # arg4: edi
    pushl 4(%ebp)   # arg3: esi
    pushl 20(%ebp)  # arg2: edx
    pushl 24(%ebp)  # arg1: ecx
    pushl 16(%ebp)  # arg0: ebx
    pushl 28(%ebp)  # num : eax
    call syscall_handle
    addl $28, %esp

    movl %eax, 28(%esp)
    popal
    iret
