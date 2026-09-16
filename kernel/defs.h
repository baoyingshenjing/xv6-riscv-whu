// Stage 1 kernel interfaces.
struct cpu;
struct spinlock;

// printf.c
int             printf(char*, ...) __attribute__((format(printf, 1, 2)));
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);

// proc.c
int             cpuid(void);
struct cpu*     mycpu(void);

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

// string.c
void*           memset(void*, int, uint);

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
