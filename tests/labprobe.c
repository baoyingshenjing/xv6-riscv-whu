// Built only in the test runner's temporary disk image.
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/riscv.h"
#include "user/user.h"

static void
check(int ok, char *name)
{
  if (!ok) {
    printf("labcheck: FAIL %s\n", name);
    exit(1);
  }
}

int
main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "child") == 0) {
    check(strcmp(argv[0], "custom-argv0") == 0, "exec argv[0]");
    exit(29);
  }
  if (argc == MAXARG - 1 && strcmp(argv[1], "limit") == 0)
    exit(17);

  check(argc == 3 && strcmp(argv[1], "alpha") == 0 &&
          strcmp(argv[2], "beta") == 0, "shell arguments");
  check(chdir("README") == -1, "reject chdir to file");
  check(chdir("/missing-directory") == -1, "reject missing directory");
  check(chdir("/") == 0 && chdir(".") == 0, "chdir to directory");

  char *old = sbrk(0);
  char *p = sbrk(2 * PGSIZE);
  check(p != SBRK_ERROR && p == old, "grow heap");
  p[0] = 'a';
  p[PGSIZE] = 'b';
  p[2 * PGSIZE - 1] = 'c';
  check(p[0] == 'a' && p[PGSIZE] == 'b' && p[2 * PGSIZE - 1] == 'c',
        "heap data");
  check(sbrk(-2 * PGSIZE) != SBRK_ERROR && sbrk(0) == old, "shrink heap");

  int pid = fork();
  check(pid >= 0, "fork");
  if (pid == 0) {
    char *args[] = {"custom-argv0", "child", 0};
    exec("labprobe", args);
    exit(1);
  }
  int status = -99;
  check(wait(&status) == pid && status == 29, "exec and wait status");

  char *many[MAXARG + 1];
  for (int i = 0; i < MAXARG; i++)
    many[i] = "x";
  many[0] = "labprobe";
  many[1] = "limit";
  many[MAXARG - 1] = 0;
  pid = fork();
  check(pid >= 0, "argument limit fork");
  if (pid == 0) {
    exec("labprobe", many);
    exit(1);
  }
  check(wait(&status) == pid && status == 17, "maximum valid argument count");
  many[MAXARG - 1] = "x";
  many[MAXARG] = 0;

  static char longarg[PGSIZE + 1];
  memset(longarg, 'x', PGSIZE);
  char *badstring[] = {(char *)MAXVA, 0};
  char *toolong[] = {longarg, 0};
  for (int i = 0; i < 8; i++) {
    check(exec("labprobe", (char **)MAXVA) == -1, "invalid argv pointer");
    check(exec("labprobe", badstring) == -1, "invalid argument pointer");
    check(exec("labprobe", many) == -1, "too many arguments");
    check(exec("labprobe", toolong) == -1, "overlong argument");
  }

  pid = fork();
  check(pid >= 0, "kill test fork");
  if (pid == 0) {
    sleep(1000);
    exit(1);
  }
  sleep(1);
  check(kill(pid) == 0, "kill sleeping child");
  check(wait(&status) == pid && status == -1, "reap killed child");
  int before = uptime();
  check(sleep(2) == 0 && uptime() - before >= 2, "sleep and uptime");
  printf("labcheck: ALL CHECKS PASSED\n");
  exit(0);
}
