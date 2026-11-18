// Stage 8: exec from disk and the special block-test write path.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
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

// The stage-6 write path has no files yet: every descriptor is the console.
uint64
sys_write(void)
{
  int fd;
  uint64 addr;
  int n;
  int done = 0;
  char buf[32];
  struct proc *p = myproc();

  argint(0, &fd);
  if (fd == 3)
    return blocktest();
  if (fd >= 3)
    return 0;
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

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  uint64 uargv, uarg;
  int ret = -1;

  if (argstr(0, path, sizeof(path)) < 0)
    return -1;
  argaddr(1, &uargv);
  memset(argv, 0, sizeof(argv));
  for (int i = 0; i < MAXARG; i++) {
    if (fetchaddr(uargv + i * sizeof(uint64), &uarg) < 0)
      break;
    if (uarg == 0) {
      ret = kexec(path, argv);
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0 || fetchstr(uarg, argv[i], PGSIZE) < 0)
      break;
  }
  for (int i = 0; i < MAXARG; i++)
    if (argv[i])
      kfree(argv[i]);
  return ret;
}
