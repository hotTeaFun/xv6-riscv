#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char* argv[]) {
  int repeats;
  char* ret;

  if (argc != 2) {
    fprintf(2, "Usage: sbrktest n\n");
    exit(1);
  }
  repeats=atoi(argv[1]);
  while (repeats--) {
    ret = sbrk(1);
    if (ret == 0) {
      fprintf(2, "sbrktest failed\n");
      exit(1);
    }
  }

  exit(0);
}
