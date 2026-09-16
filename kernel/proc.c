#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"

// Stage 1 has no processes. spinlock.c still needs per-CPU interrupt state
// and the hart identifier while serializing kernel printf output.
struct cpu cpus[NCPU];

// Must be called with interrupts disabled once interrupts are introduced.
int
cpuid()
{
  return r_tp();
}

// Return this CPU's state. Stage 1 initializes tp in start() before mret.
struct cpu *
mycpu(void)
{
  return &cpus[cpuid()];
}
