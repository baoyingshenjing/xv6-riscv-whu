#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int status;

  argint(0, &status);
  kexit(status);
  return 0;
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 addr;

  argaddr(0, &addr);
  return kwait(addr);
}
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;

  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < (uint)n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// The stage-6 write path has no files yet: every descriptor is the console.
uint64
sys_write(void)
{
  uint64 addr;
  int n;
  int done = 0;
  char buf[32];
  struct proc *p = myproc();

  argaddr(1, &addr);
  argint(2, &n);
  if (n < 0)
    return -1;
  while (done < n) {
    int m = n - done;
    if (m > (int)sizeof(buf))
      m = sizeof(buf);
    if (copyin(p->pagetable, p->sz, buf, addr + done, m) < 0)
      return -1;
    uartwrite(buf, m);
    done += m;
  }
  return done;
}
