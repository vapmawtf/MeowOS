#include <stdint.h>
#include <meow/paging.h>
#include <meow/string.h>

// Kernel ma już PML4 z ustawionymi pozycjami identity w bootloaderze: CR3 = 0x10f000
#define PAGE_SIZE 4096ULL

static uint8_t page_table_pool[PAGE_SIZE * 64] __attribute__((aligned(4096)));
static uint32_t page_table_pool_offset = 0;

static void* alloc_page_table(void) {
    if (page_table_pool_offset + PAGE_SIZE > sizeof(page_table_pool)) {
        // Brak miejsca na kolejną stronę tabeli
        // Jeśli wolisz, dodaj kernel panic lub zatrzymaj.
        return NULL;
    }
    void* res = (void*)(uintptr_t)(page_table_pool + page_table_pool_offset);
    page_table_pool_offset += PAGE_SIZE;
    memset(res, 0, PAGE_SIZE);
    return res;
}

void map_page(uint64_t phys, uint64_t virt, uint64_t flags) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t*)(uintptr_t)(cr3 & ~0xFFFULL);
    uint64_t pml4e = pml4[(virt >> 39) & 0x1FF];

    uint64_t perm = (flags & 0x04) ? 0x04 : 0x00;

    uint64_t* pdpt;
    if (!(pml4e & 1)) {
        pdpt = alloc_page_table();
        if (!pdpt)
            return;
        pml4[(virt >> 39) & 0x1FF] = ((uint64_t)(uintptr_t)pdpt) | 0x03 | perm;
    } else {
        pdpt = (uint64_t*)(uintptr_t)(pml4e & ~0xFFFULL);
    }

    uint64_t pdpte = pdpt[(virt >> 30) & 0x1FF];
    uint64_t* pd;
    if (!(pdpte & 1)) {
        pd = alloc_page_table();
        if (!pd)
            return;
        pdpt[(virt >> 30) & 0x1FF] = ((uint64_t)(uintptr_t)pd) | 0x03 | perm;
    } else {
        pd = (uint64_t*)(uintptr_t)(pdpte & ~0xFFFULL);
    }

    uint64_t pde = pd[(virt >> 21) & 0x1FF];
    uint64_t* pt;
    if (!(pde & 1)) {
        pt = alloc_page_table();
        if (!pt)
            return;
        pd[(virt >> 21) & 0x1FF] = ((uint64_t)(uintptr_t)pt) | 0x03 | perm;
    } else {
        pt = (uint64_t*)(uintptr_t)(pde & ~0xFFFULL);
    }

    pt[(virt >> 12) & 0x1FF] = (phys & ~0xFFFULL) | (flags & 0xFFFULL) | 0x1;
}
