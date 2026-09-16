#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];
struct proc proc;

extern char trampoline[];

// Provided as an array in the course task book.
uchar initcode[] = {
  0x13, 0x01, 0x01, 0xff, 0x23, 0x34, 0x11, 0x00,
  0x23, 0x30, 0x81, 0x00, 0x13, 0x04, 0x01, 0x01,
  0x13, 0x05, 0x40, 0x01, 0x97, 0x00, 0x00, 0x00,
  0xe7, 0x80, 0x80, 0x01, 0x13, 0x05, 0x40, 0x01,
  0x97, 0x00, 0x00, 0x00, 0xe7, 0x80, 0xc0, 0x00,
  0x6f, 0x00, 0x00, 0x00, 0x93, 0x08, 0xc0, 0x00,
  0x73, 0x00, 0x00, 0x00, 0x67, 0x80, 0x00, 0x00,
};

void
proc_mapstacks(pagetable_t kpgtbl)
{
  char *pa = kalloc();

  if (pa == 0)
    panic("proc_mapstacks");
  kvmmap(kpgtbl, KSTACK(0), (uint64)pa, PGSIZE, PTE_R | PTE_W);
}

void
procinit(void)
{
  proc.kstack = KSTACK(0);
}

int
cpuid(void)
{
  return r_tp();
}

struct cpu *
mycpu(void)
{
  return &cpus[cpuid()];
}

struct proc *
myproc(void)
{
  push_off();
  struct proc *p = mycpu()->proc;
  pop_off();
  return p;
}

pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable = uvmcreate();

  if (pagetable == 0)
    panic("proc_pagetable");
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) != 0)
    panic("proc_pagetable: trampoline");
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)p->trapframe,
               PTE_R | PTE_W) != 0)
    panic("proc_pagetable: trapframe");

  return pagetable;
}

static struct proc *
allocproc(void)
{
  struct proc *p = &proc;

  p->pid = 1;
  p->trapframe = (struct trapframe *)kalloc();
  if (p->trapframe == 0)
    panic("allocproc: trapframe");
  p->pagetable = proc_pagetable(p);
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)usertrapret;
  p->context.sp = p->kstack + PGSIZE;
  return p;
}

void
userinit(void)
{
  struct proc *p = allocproc();
  char *mem;

  uvmfirst(p->pagetable, initcode, sizeof(initcode));

  // Pages 1 and 2 are zero-initialized global data; page 3 is the user stack.
  for (int page = 1; page <= 3; page++) {
    mem = kalloc();
    if (mem == 0)
      panic("userinit: user page");
    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, page * PGSIZE, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_U) != 0)
      panic("userinit: mappages");
  }

  p->sz = 4 * PGSIZE;
  p->trapframe->epc = 0;
  p->trapframe->sp = 4 * PGSIZE;

  // No scheduler exists yet: make CPU 0 enter the first process directly.
  mycpu()->proc = p;
  swtch(&mycpu()->context, &p->context);
  panic("userinit returned");
}
