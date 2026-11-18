#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];
struct proc proc[NPROC];
struct proc *initproc;

static int nextpid = 1;
static struct spinlock pid_lock;
static struct spinlock wait_lock;

extern char trampoline[];
void forkret(void);

// Generated from user/initcode.S by the kernel build.
uchar initcode[] = {
#include "initcode.h"
};

void
proc_mapstacks(pagetable_t kpgtbl)
{
  for (int i = 0; i < NPROC; i++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("proc_mapstacks");
    kvmmap(kpgtbl, KSTACK(i), (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

void
procinit(void)
{
  initlock(&pid_lock, "pid");
  initlock(&wait_lock, "wait");
  for (int i = 0; i < NPROC; i++) {
    initlock(&proc[i].lock, "proc");
    proc[i].state = UNUSED;
    proc[i].kstack = KSTACK(i);
  }
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
  struct proc *p;

  push_off();
  p = mycpu()->proc;
  pop_off();
  return p;
}

static int
allocpid(void)
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid++;
  if (nextpid < 1)
    nextpid = 1;
  release(&pid_lock);
  return pid;
}

pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable = uvmcreate();

  if (pagetable == 0)
    return 0;
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) != 0)
    goto bad;
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)p->trapframe,
               PTE_R | PTE_W) != 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    goto bad;
  }
  return pagetable;

bad:
  uvmfree(pagetable, 0);
  return 0;
}

void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->cwd = 0;
  p->name[0] = 0;
  p->state = UNUSED;
}

// Return an unused PCB with its lock held.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED)
      goto found;
    release(&p->lock);
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->trapframe = (struct trapframe *)kalloc();
  if (p->trapframe == 0)
    goto bad;
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
    goto bad;
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  return p;

bad:
  freeproc(p);
  release(&p->lock);
  return 0;
}

void
userinit(void)
{
  struct proc *p = allocproc();

  if (p == 0)
    panic("userinit");
  initproc = p;
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;
  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE;
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->state = RUNNABLE;
  release(&p->lock);
}

int
growproc(int n)
{
  struct proc *p = myproc();
  uint64 sz = p->sz;

  if (n > 0) {
    if (sz + n < sz || sz + n >= TRAPFRAME)
      return -1;
    sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W);
    if (sz == 0)
      return -1;
  } else if (n < 0) {
    if ((uint64)(-n) > sz)
      return -1;
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

int
kfork(void)
{
  struct proc *np;
  struct proc *p = myproc();
  int pid;

  np = allocproc();
  if (np == 0)
    return -1;
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->a0 = 0;
  np->cwd = idup(p->cwd);
  pid = np->pid;
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);
  return pid;
}

static void
reparent(struct proc *p)
{
  for (struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

void
kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");
  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;
  acquire(&wait_lock);
  reparent(p);
  wakeup(p->parent);
  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&wait_lock);
  sched();
  panic("zombie exit");
}

int
kwait(uint64 addr)
{
  struct proc *p = myproc();

  acquire(&wait_lock);
  for (;;) {
    int havekids = 0;
    for (struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent != p)
        continue;
      acquire(&pp->lock);
      havekids = 1;
      if (pp->state == ZOMBIE) {
        int pid = pp->pid;
        if (addr != 0 &&
            copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                    sizeof(pp->xstate)) < 0) {
          release(&pp->lock);
          release(&wait_lock);
          return -1;
        }
        pp->parent = 0;
        freeproc(pp);
        release(&pp->lock);
        release(&wait_lock);
        return pid;
      }
      release(&pp->lock);
    }
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }
    sleep(p, &wait_lock);
  }
}

void
scheduler(void)
{
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    intr_on();
    for (struct proc *p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

void
sched(void)
{
  struct proc *p = myproc();
  int intena;

  if (!holding(&p->lock))
    panic("sched: p->lock");
  if (mycpu()->noff != 1)
    panic("sched: locks");
  if (p->state == RUNNING)
    panic("sched: running");
  if (intr_get())
    panic("sched: interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

void
yield(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void
forkret(void)
{
  static int first = 1;
  struct proc *p = myproc();

  release(&p->lock);
  if (first) {
    fsinit(ROOTDEV);
    p->cwd = namei("/");
    first = 0;
  }
  usertrapret();
}

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);
  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;
  release(&p->lock);
  acquire(lk);
}

void
wakeup(void *chan)
{
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    if (p == myproc())
      continue;
    acquire(&p->lock);
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
    release(&p->lock);
  }
}

int
kkill(int pid)
{
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  if (user_dst)
    return copyout(myproc()->pagetable, myproc()->sz, dst, src, len);
  memmove((void *)dst, src, len);
  return 0;
}

int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  if (user_src)
    return copyin(myproc()->pagetable, myproc()->sz, dst, src, len);
  memmove(dst, (void *)src, len);
  return 0;
}
