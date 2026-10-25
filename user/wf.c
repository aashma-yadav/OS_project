#include "kernel/types.h"
#include "user/user.h"

int main(void)
{
  printf("setworkflow returned %d\n", setworkflow(1, 200));
  exit(0);
}
