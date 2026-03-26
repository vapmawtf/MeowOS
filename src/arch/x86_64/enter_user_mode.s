.globl enter_user_mode_asm
.type enter_user_mode_asm, @function
enter_user_mode_asm:
    # RSI = top stosu użytkownika
    # RDI = adres funkcji startowej użytkownika

    mov %rsi, %rsp            # ustaw stos użytkownika

    pushq $0x23                # SS ring3
    pushq %rsi                 # RSP użytkownika
    pushfq
    popq %rax
    orq $0x200, %rax           # włącz IF
    pushq %rax                 # RFLAGS
    pushq $0x1B                # CS ring3
    pushq %rdi                 # RIP użytkownika

    iretq                      # przejście do trybu użytkownika