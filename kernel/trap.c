#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

void kernelvec(void);
static int devintr(void);
static void clockintr(void);

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// Install the supervisor-mode trap vector for this CPU.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// Called by kernelvec.S for traps that occur while the CPU is in S mode.
void
kerneltrap(void)
{
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if (devintr() == 0) {
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", r_scause(), sepc,
           r_stval());
    panic("kerneltrap");
  }

  // kernelvec.S returns with these values; no scheduler exists in stage 3.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

// Handle the supervisor timer interrupt and schedule the next one.
static void
clockintr(void)
{
  if (cpuid() == 0) {
    acquire(&tickslock);
    ticks++;
    if (ticks % 30 == 0)
      printf("T");
    release(&tickslock);
  }

  // 1,000,000 time units is approximately one tenth of a second in QEMU.
  w_stimecmp(r_time() + 1000000);
}

// Return non-zero only for devices supported in this stage.
static int
devintr(void)
{
  uint64 scause = r_scause();

  if (scause == 0x8000000000000009L) {
    int irq = plic_claim();

    if (irq == UART0_IRQ) {
      uartintr();
    } else if (irq) {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    if (irq)
      plic_complete(irq);
    return 1;
  }

  if (scause == 0x8000000000000005L) {
    clockintr();
    return 1;
  }

  return 0;
}
