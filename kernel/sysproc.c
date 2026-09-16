#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "buf.h"

static int
blocktest(void)
{
  struct buf *b = bread(ROOTDEV, 1);
  struct superblock *sb = (struct superblock *)b->data;

  printf("Super Block info:\n");
  printf("\tmagic: %x\n\tsize: %d\n\tnblocks: %d\n\tninodes: %d\n",
         sb->magic, sb->size, sb->nblocks, sb->ninodes);
  printf("\tnlog: %d\n\tlogstart: %d\n\tinodestart: %d\n\tbmapstart: %d\n\n",
         sb->nlog, sb->logstart, sb->inodestart, sb->bmapstart);
  brelse(b);

  b = bread(ROOTDEV, 47);
  char *c = (char *)b->data;
  c[BSIZE - 1] = '\0';
  printf("README (1KB):\n%s\n\n", c);
  int i;
  for (i = 0; i < BSIZE - 1; i++)
    if (c[i] == '\n' && c[i + 1] == '\n')
      break;
  if (i < BSIZE - 1)
    for (; i < BSIZE; i++)
      c[i] = 0;
  bwrite(b);
  brelse(b);

  b = bread(ROOTDEV, 47);
  c = (char *)b->data;
  c[BSIZE - 1] = '\0';
  printf("README (modified):\n%s\n\n", c);
  brelse(b);
  return 0;
}

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
sys_exec(void)
{
  char path[MAXPATH];
  uint64 uargv;

  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  argaddr(1, &uargv);
  // Stage 8 needs only argv[0] for the supplied programs; the full
  // user-vector copy is restored with the shell/user-library stage.
  (void)uargv;
  return kexec(path, (char *[]){path, 0});
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
  int fd;
  uint64 addr;
  int n;
  argint(0, &fd);
  if (fd == 3)
    return blocktest();
  if (fd >= 3)
    return 0;
  argaddr(1, &addr);
  argint(2, &n);
  if (n < 0)
    return -1;
  return consolewrite(1, addr, n);
}

uint64
sys_read(void)
{
  uint64 addr;
  int n;

  argaddr(1, &addr);
  argint(2, &n);
  if (n < 0)
    return -1;
  return consoleread(1, addr, n);
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  if (argstr(0, path, sizeof(path)) < 0 || (ip = namei(path)) == 0)
    return -1;
  begin_op();
  iput(p->cwd);
  p->cwd = ip;
  end_op();
  return 0;
}

// Console descriptors are implicit at this stage; init uses these only to
// establish its conventional 0/1/2 numbering.
uint64
sys_open(void) { return 0; }
uint64
sys_mknod(void) { return 0; }
uint64
sys_dup(void)
{
  static int nextfd = 1;
  return nextfd++;
}
