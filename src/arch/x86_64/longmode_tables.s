.section .bss
.align 4096
.global pml4_table
pml4_table:
    .skip 4096
.global pdpt_table
pdpt_table:
    .skip 4096
.global pd_table
pd_table:
    .skip 4096
