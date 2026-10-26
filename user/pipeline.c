#include "kernel/types.h"
#include "user/user.h"

#define MAXST 8
#define MAXHOG 8

// work is in millions of loop iterations
static void
spin(int m)
{
  volatile uint x = 0;
  for (int i = 0; i < m; i++)
    for (int j = 0; j < 1000000; j++)
      x += j;
}

int
main(int argc, char *argv[])
{
  if (argc < 5) {
    printf("usage: pipeline stages workM deadline_ticks hogs\n");
    exit(1);
  }
  int n = atoi(argv[1]), work = atoi(argv[2]);
  int dl = atoi(argv[3]), hogs = atoi(argv[4]);
  setsched(argc > 5 ? atoi(argv[5]) : 2);   // 0 RR, 1 EDF, 2 EDF+donation
  if (n < 2 || n > MAXST || hogs > MAXHOG) {
    printf("stages must be 2..%d, hogs 0..%d\n", MAXST, MAXHOG);
    exit(1);
  }

  // Hogs are forked BEFORE the pipes exist, so they never hold a write end.
  int hp[MAXHOG];
  for (int h = 0; h < hogs; h++) {
    hp[h] = fork();
    if (hp[h] == 0)
      for (;;) spin(1);
  }
  int p[MAXST][2];
  for (int i = 0; i < n - 1; i++)
    pipe(p[i]);
  int start = uptime();

  // Stage s reads p[s-1][0] and writes p[s][1].
  for (int s = 0; s < n; s++) {
    if (fork() == 0) {
      for (int i = 0; i < n - 1; i++) {
        if (!(s > 0 && i == s - 1)) close(p[i][0]);
        if (!(s < n - 1 && i == s)) close(p[i][1]);
      }
      char c = 'x';
      if (s == n - 1)
        setworkflow(1, dl);        // only the final stage has a deadline
      if (s > 0)
         if (read(p[s - 1][0], &c, 1) != 1)
          printf("stage %d: read returned early\n", s);
      spin(work);
      printf("stage %d finished work at tick %d\n", s, uptime());
      if (s < n - 1) {
        write(p[s][1], &c, 1);
      } else {
        int t = uptime() - start;
        printf("pipeline: done at +%d ticks, deadline %d: %s\n",
               t, dl, t <= dl ? "MET" : "MISSED");
      }
      exit(0);
    }
  }

  for (int i = 0; i < n - 1; i++) { close(p[i][0]); close(p[i][1]); }

  for (int s = 0; s < n; s++) wait(0);      // only the stages exit by themselves
  for (int h = 0; h < hogs; h++) kill(hp[h]);
  for (int h = 0; h < hogs; h++) wait(0);
  exit(0);
}
