#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// The page table shared by all CPUs while running in the kernel.
pagetable_t kernel_pagetable;

extern char etext[]; // first address after kernel text, from kernel.ld
extern char trampoline[];

// Make the direct-map page table needed in stage 2.
static pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  if (kpgtbl == 0)
    panic("kvmmake");
  memset(kpgtbl, 0, PGSIZE);

  // UART registers used by the stage 1 synchronous output path.
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // PLIC registers. The interrupt driver is introduced in stage 3.
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // Kernel text is executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE,
         PTE_R | PTE_X);

  // Kernel data and all remaining usable physical memory are writable.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // The trampoline is also mapped at the top of every user page table.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // Stage 4 has exactly one process and therefore one kernel stack.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Add one direct mapping to a kernel page table during boot.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Enable Sv39 translation for the current CPU.
void
kvminithart(void)
{
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the PTE for va. Allocate intermediate page-table pages if requested.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }

  return &pagetable[PX(0, va)];
}

// Install leaf PTEs for a page-aligned virtual-address range.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");
  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");
  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }

  return 0;
}

// Create an empty user page table.
pagetable_t
uvmcreate(void)
{
  pagetable_t pagetable = (pagetable_t)kalloc();

  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Allocate and map the first user code page at virtual address zero.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("uvmfirst");
  mem = kalloc();
  if (mem == 0)
    panic("uvmfirst: kalloc");
  memset(mem, 0, PGSIZE);
  memmove(mem, src, sz);
  if (mappages(pagetable, 0, PGSIZE, (uint64)mem,
               PTE_R | PTE_W | PTE_X | PTE_U) != 0)
    panic("uvmfirst: mappages");
}
