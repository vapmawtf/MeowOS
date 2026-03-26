
    .globl enter_user_mode_asm
    .type enter_user_mode_asm, @function
enter_user_mode_asm:
    mov %rsi, %rsp 
    pushq $0x23 
    pushq %rsi 
    pushfq  
    popq %rax
    orq $0x200, %rax
    pushq %rax
    pushq $0x1B
    pushq %rdi
    swapgs 
    iretq 
    hlt 
