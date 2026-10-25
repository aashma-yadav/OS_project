#include "kernel/types.h"
#include "user/user.h"

// Three CPU-bound children with different deadlines.
// With EDF on 1 CPU they should finish in deadline order: B, C, A.
static void
spin(void)
{
  volatile int x = 0;
  for (int i = 0; i < 60000000; i++) x += i;
}

static void
child(char name, int deadline)
{
  setworkflow(1, deadline);
  spin();
  printf("%c done (deadline %d) at tick %d\n", name, deadline, uptime());
  exit(0);
}

int main(void)
{
  // Parent is the most urgent process, so it can fork all children
  // before any of them gets going.
  setworkflow(9, 1);
  if (fork() == 0) child('A', 3000);
  if (fork() == 0) child('B', 1000);
  if (fork() == 0) child('C', 2000);
  wait(0); wait(0); wait(0);
  exit(0);
}
