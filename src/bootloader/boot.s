.global _start
.global gdt64
.global gdt64_descriptor
.extern kernel_main

.section .text
_start:
.code32
    cli
    cld

    mov %eax, multiboot_magic
    mov %ebx, multiboot_info

    mov $0xB8000, %edx
    movw $0x1F33, (%edx)

    lgdt gdt64_descriptor

    mov %cr4, %eax
    or $((1 << 5) | (1 << 4)), %eax
    mov %eax, %cr4

    mov $pml4_table, %eax
    mov %eax, %cr3

    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    mov %cr0, %eax
    or $((1 << 0) | (1 << 31)), %eax
    mov %eax, %cr0

    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    mov $0xB8000, %rax
    movw $0x2F36, 2(%rax)

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    mov $0x200000, %rsp
    and $-16, %rsp
    mov multiboot_magic(%rip), %edi
    mov multiboot_info(%rip), %esi
    call kernel_main

.hlt_loop:
    hlt
    jmp .hlt_loop

.align 16
gdt64:
    .quad 0x0000000000000000
    .quad 0x00209a0000000000
    .quad 0x0000920000000000
gdt64_end:

gdt64_descriptor:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.align 4096
pml4_table:
    .quad pdpt_table + 0x3
    .zero 4096 - 8

.align 4096
pdpt_table:
    .quad pd_table0 + 0x3
    .quad pd_table1 + 0x3
    .quad pd_table2 + 0x3
    .quad pd_table3 + 0x3
    .zero 4096 - 32

.align 4096
pd_table0:
    .set i, 0
    .rept 512
    .quad (i * 0x200000) + 0x83
    .set i, i + 1
    .endr

.align 4096
pd_table1:
    .set i, 0
    .rept 512
    .quad 0x40000000 + (i * 0x200000) + 0x83
    .set i, i + 1
    .endr

.align 4096
pd_table2:
    .set i, 0
    .rept 512
    .quad 0x80000000 + (i * 0x200000) + 0x83
    .set i, i + 1
    .endr

.align 4096
pd_table3:
    .set i, 0
    .rept 512
    .quad 0xC0000000 + (i * 0x200000) + 0x83
    .set i, i + 1
    .endr

.align 4
multiboot_magic:
    .long 0
multiboot_info:
    .long 0