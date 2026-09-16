// Per-CPU state needed by stage 1 spin locks.
struct cpu {
  int noff;
  int intena;
};

extern struct cpu cpus[NCPU];
