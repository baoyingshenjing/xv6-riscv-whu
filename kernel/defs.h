// Stage 1 kernel interfaces.
struct cpu;
struct context;
struct proc;
struct spinlock;
struct buf;
struct sleeplock;

// printf.c
int             printf(char*, ...) __attribute__((format(printf, 1, 2)));
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);

// proc.c
int             cpuid(void);
struct cpu*     mycpu(void);
struct proc*    myproc(void);
void            procinit(void);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc*);
void            proc_freepagetable(pagetable_t, uint64);
void            userinit(void);
int             growproc(int);
int             kfork(void);
void            kexit(int) __attribute__((noreturn));
int             kwait(uint64);
int             kkill(int);
int             killed(struct proc*);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            yield(void);
void            sleep(void*, struct spinlock*);
void            wakeup(void*);

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);

// sleeplock.c
void            initsleeplock(struct sleeplock*, char*);
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc_sync(int);
void            uartwrite(char*, int);

// string.c
void*           memset(void*, int, uint);
void*           memmove(void*, const void*, uint);
int             strlen(const char*);

// kalloc.c
void            kfree(void*);
void*           kalloc(void);
void            kinit(void);

// vm.c
extern pagetable_t kernel_pagetable;
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
pte_t*          walk(pagetable_t, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
void            uvmfirst(pagetable_t, uchar*, uint);
void            uvmunmap(pagetable_t, uint64, uint64, int);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, uint64, char*, uint64);
int             copyin(pagetable_t, uint64, char*, uint64, uint64);
int             copyinstr(pagetable_t, uint64, char*, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);

// trap.c
void            trapinit(void);
void            trapinithart(void);
void            usertrapret(void) __attribute__((noreturn));
extern uint     ticks;
extern struct spinlock tickslock;

// syscall.c
int             fetchaddr(uint64, uint64*);
int             fetchstr(uint64, char*, int);
void            argint(int, int*);
void            argaddr(int, uint64*);
int             argstr(int, char*, int);
void            syscall(void);

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf*, int);
void            virtio_disk_intr(void);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// swtch.S
void            swtch(struct context*, struct context*);
