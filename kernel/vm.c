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

  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

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

// Look up a user virtual address and return its physical-page base.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  if (va >= MAXVA)
    return 0;
  pte = walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
    return 0;
  return PTE2PA(*pte);
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

  if (sz > PGSIZE)
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

// Remove user mappings beginning at page-aligned va.  Stage 5 uses this for
// eager heap contraction; absent mappings are harmless.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if (va % PGSIZE)
    panic("uvmunmap: not aligned");
  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    pte = walk(pagetable, a, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if (do_free)
      kfree((void *)PTE2PA(*pte));
    *pte = 0;
  }
}

// Eagerly allocate user pages in [oldsz, newsz).
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;
  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if (mem == 0)
      goto err;
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem,
                 PTE_R | PTE_U | xperm) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return newsz;

err:
  uvmunmap(pagetable, oldsz, (a - oldsz) / PGSIZE, 1);
  return 0;
}

// Deallocate pages no longer covered by a shrunken user heap.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;
  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
    uvmunmap(pagetable, PGROUNDUP(newsz),
              (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE, 1);
  return newsz;
}

// Copy kernel data into user virtual memory.
int
copyout(pagetable_t pagetable, uint64 sz, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  (void)sz;
  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    pte = walk(pagetable, va0, 0);
    if ((*pte & PTE_W) == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + dstva - va0), src, n);
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy user data into kernel memory.
int
copyin(pagetable_t pagetable, uint64 sz, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  (void)sz;
  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + srcva - va0), n);
    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a NUL-terminated string from user memory.
int
copyinstr(pagetable_t pagetable, uint64 sz, char *dst, uint64 srcva,
          uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  (void)sz;
  while (!got_null && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;
    char *p = (char *)(pa0 + srcva - va0);
    while (n-- > 0) {
      if ((*dst++ = *p++) == '\0') {
        got_null = 1;
        break;
      }
      max--;
    }
    if (!got_null)
      srcva = va0 + PGSIZE;
  }
  return got_null ? 0 : -1;
}

// Recursively release page-table pages after all leaf mappings are gone.
static void
freewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      freewalk((pagetable_t)PTE2PA(pte));
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy a parent's ordinary user mappings into a child's page table.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    pte = walk(old, i, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    mem = kalloc();
    if (mem == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
