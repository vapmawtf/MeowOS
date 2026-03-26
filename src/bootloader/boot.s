.global _start
.global gdt64
.global gdt64_descriptor
.global tss64
.global tss64_end
.global kernel_stack_top
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

    # Use our dedicated kernel stack instead of a magic number
    lea kernel_stack_top(%rip), %rsp
    and $-16, %rsp

    mov multiboot_magic(%rip), %edi
    mov multiboot_info(%rip), %esi
    call kernel_main

.hlt_loop:
    hlt
    jmp .hlt_loop

# =============================================================
# GDT — 5 descriptors + 16-byte TSS descriptor (2 slots)
# Selector map:
#   0x00 = null
#   0x08 = kernel code  DPL=0
#   0x10 = kernel data  DPL=0
#   0x18 = user code    DPL=3  (CS for iretq = 0x1B = 0x18|3)
#   0x20 = user data    DPL=3  (SS for iretq = 0x23 = 0x20|3)
#   0x28 = TSS (16 bytes, base patched at runtime by install_tss())
# =============================================================
.align 16
gdt64:
    .quad 0x0000000000000000       # 0x00: null
    .quad 0x00209A0000000000       # 0x08: kernel code DPL=0
    .quad 0x0000920000000000       # 0x10: kernel data DPL=0
    .quad 0x0020FA0000000000       # 0x18: user code   DPL=3
    .quad 0x0000F20000000000       # 0x20: user data   DPL=3
    # 0x28: TSS descriptor — 16 bytes, base/limit patched by install_tss()
    .quad 0x0000000000000000
    .quad 0x0000000000000000
gdt64_end:

gdt64_descriptor:
    .word gdt64_end - gdt64 - 1   # limit = 0x37
    .quad gdt64                    # base  (.quad — not .long!)

# =============================================================
# TSS — 64-bit Task State Segment
# Required so CPU has a valid RSP0 (kernel stack) when an
# exception or syscall arrives from ring 3.
# RSP0 and the GDT TSS descriptor base are patched at runtime
# by install_tss() in longmode.c before entering user mode.
# =============================================================
.align 16
tss64:
    .long 0                        # +0:  reserved
    .quad 0                        # +4:  RSP0 — patched by install_tss()
    .quad 0                        # +12: RSP1
    .quad 0                        # +20: RSP2
    .quad 0                        # +28: reserved
    .quad 0                        # +36: IST1
    .quad 0                        # +44: IST2
    .quad 0                        # +52: IST3
    .quad 0                        # +60: IST4
    .quad 0                        # +68: IST5
    .quad 0                        # +76: IST6
    .quad 0                        # +84: IST7
    .quad 0                        # +92: reserved
    .word 0                        # +100: reserved
    .word 0x68                     # +102: IOPB offset (past end = no IOPB)
tss64_end:

# =============================================================
# Kernel stack — 16 KB
# Top address stored in kernel_stack_top, used as RSP0 in TSS.
# =============================================================
.align 16
    .skip 16384
kernel_stack_top:

# =============================================================
# Page tables — 4 GB identity mapped, user accessible
# Flags: bit0=Present, bit1=Writable, bit2=User, bit7=HugePage
# Bit 2 must be set at EVERY level (PML4, PDPT, PD) or ring 3
# gets a #PF on first memory access.
# =============================================================
.align 4096
pml4_table:
    .quad pdpt_table + 0x7         # P + W + U
    .zero 4096 - 8

.align 4096
pdpt_table:
    .quad pd_table0 + 0x7          # P + W + U
    .quad pd_table1 + 0x7
    .quad pd_table2 + 0x7
    .quad pd_table3 + 0x7
    .zero 4096 - 32

.align 4096
pd_table0:
    .set i, 0
    .rept 512
    .quad (i * 0x200000) + 0x87   # P + W + U + Huge
    .set i, i + 1
    .endr

.align 4096
pd_table1:
    .set i, 0
    .rept 512
    .quad 0x40000000 + (i * 0x200000) + 0x87
    .set i, i + 1
    .endr

.align 4096
pd_table2:
    .set i, 0
    .rept 512
    .quad 0x80000000 + (i * 0x200000) + 0x87
    .set i, i + 1
    .endr

.align 4096
pd_table3:
    .set i, 0
    .rept 512
    .quad 0xC0000000 + (i * 0x200000) + 0x87
    .set i, i + 1
    .endr

.align 4
multiboot_magic:
    .long 0
multiboot_info:
    .long 0