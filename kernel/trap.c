#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

void kernelvec(void);
static int devintr(void);
static void clockintr(void);

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

void
usertrap(void)
{
  int which_dev = 0;
  struct proc *p = myproc();

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");
  w_stvec((uint64)kernelvec);
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8) {
    if (killed(p))
      kexit(-1);
    p->trapframe->epc += 4;
    intr_on();
    syscall();
  } else if ((which_dev = devintr()) == 0) {
    printf("usertrap: scause=0x%lx sepc=0x%lx stval=0x%lx\n", r_scause(),
           r_sepc(), r_stval());
    panic("usertrap");
  }

  if (killed(p))
    kexit(-1);
  if (which_dev == 2)
    yield();
  usertrapret();
}

void
usertrapret(void)
{
  struct proc *p = myproc();

  intr_off();
  w_stvec(TRAMPOLINE + (uservec - trampoline));
  p->trapframe->kernel_satp = r_satp();
  p->trapframe->kernel_sp = p->kstack + PGSIZE;
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();

  uint64 x = r_sstatus();
  x &= ~SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);
  w_sepc(p->trapframe->epc);

  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(MAKE_SATP(p->pagetable));
  panic("usertrapret returned");
}

void
kerneltrap(void)
{
  int which_dev;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");
  which_dev = devintr();
  if (which_dev == 0) {
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", r_scause(), sepc,
           r_stval());
    panic("kerneltrap");
  }
  if (which_dev == 2 && myproc() != 0)
    yield();

  w_sepc(sepc);
  w_sstatus(sstatus);
}

static void
clockintr(void)
{
  if (cpuid() == 0) {
    acquire(&tickslock);
    ticks++;
    if (ticks % 30 == 0)
      printf("T");
    wakeup(&ticks);
    release(&tickslock);
  }
  w_stimecmp(r_time() + 1000000);
}

static int
devintr(void)
{
  uint64 scause = r_scause();

  if (scause == 0x8000000000000009L) {
    int irq = plic_claim();
    if (irq == UART0_IRQ)
      uartintr();
    else if (irq)
      printf("unexpected interrupt irq=%d\n", irq);
    if (irq)
      plic_complete(irq);
    return 1;
  }
  if (scause == 0x8000000000000005L) {
    clockintr();
    return 2;
  }
  return 0;
}
