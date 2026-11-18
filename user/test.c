#include "user.h"

static void
forkfailed(void)
{
  printf("test: fork failed\n");
  exit(1);
}

int
main(void)
{
  int child1 = fork();
  if (child1 < 0)
    forkfailed();
  if (child1 == 0) {
    char *argv[] = {"execchild1", 0};
    exec("execchild1", argv);
    printf("test: execchild1 failed\n");
    exit(1);
  }

  int child2 = fork();
  if (child2 < 0)
    forkfailed();
  if (child2 == 0) {
    char *argv[] = {"execchild2", 0};
    exec("execchild2", argv);
    printf("test: execchild2 failed\n");
    exit(1);
  }

  int child3 = fork();
  if (child3 < 0)
    forkfailed();
  if (child3 == 0) {
    sleep(10);
    printf("[pid: %d] sleep test - 1s. uptime: %d\n", getpid(), uptime());
    sleep(20);
    printf("[pid: %d] sleep test - 2s. uptime: %d\n", getpid(), uptime());
    sleep(30);
    printf("[pid: %d] sleep test - 3s. uptime: %d\n", getpid(), uptime());
    if (kill(child1) < 0) {
      printf("test: kill failed\n");
      exit(1);
    }
    exit(0);
  }

  int pid, status, reaped = 0, failed = 0;
  while ((pid = wait(&status)) >= 0) {
    printf("[pid: %d] wait - pid: %d - exit code: %d\n", getpid(), pid, status);
    if (!((pid == child1 && status == -1) ||
          ((pid == child2 || pid == child3) && status == 0)))
      failed = 1;
    reaped++;
  }
  printf("[pid: %d] uptime: %d, init process has no child now, over!\n",
         getpid(), uptime());
  if (failed || reaped != 3) {
    printf("test: FAILED (reaped %d children)\n", reaped);
    exit(1);
  }
  printf("test: ALL CHECKS PASSED\n");
  exit(0);
}
