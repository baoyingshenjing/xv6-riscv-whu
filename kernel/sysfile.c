// File-related system calls retained for stages 8 and 9. Console I/O is
// direct: this course does not expose file descriptors backed by disk files.
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "stat.h"
#include "sleeplock.h"
#include "buf.h"
#include "file.h"

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
sys_write(void)
{
  int fd, n;
  uint64 addr;

  argint(0, &fd);
  if (fd < 0)
    return -1;
  if (fd == 3)
    return blocktest();
  if (fd > 3)
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
  int fd, n;
  uint64 addr;

  argint(0, &fd);
  argaddr(1, &addr);
  argint(2, &n);
  if (fd < 0 || fd >= 3 || n < 0)
    return -1;
  return consoleread(1, addr, n);
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, sizeof(path)) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  p->cwd = ip;
  end_op();
  return 0;
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
