#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if (cpuid() == 0) {
    uartinit();
    printfinit();
    kinit();
    kvminit();
    kvminithart();
    procinit();
    trapinit();
    trapinithart();
    plicinit();
    plicinithart();
    binit();
    virtio_disk_init();
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;
    kvminithart();
    trapinithart();
    plicinithart();
  }

  printf("cpu %d is booting!\n", cpuid());
  if (cpuid() == 0)
    userinit();

  scheduler();
}
