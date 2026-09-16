#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// The stage 3 PLIC configuration enables only the UART receive interrupt.
void
plicinit(void)
{
  *(uint32 *)(PLIC + UART0_IRQ * 4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();

  *(uint32 *)PLIC_SENABLE(hart) = (1 << UART0_IRQ);
  *(uint32 *)PLIC_SPRIORITY(hart) = 0;
}

int
plic_claim(void)
{
  return *(uint32 *)PLIC_SCLAIM(cpuid());
}

void
plic_complete(int irq)
{
  *(uint32 *)PLIC_SCLAIM(cpuid()) = irq;
}
