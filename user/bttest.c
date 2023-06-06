#include "kernel/types.h"
#include "user/user.h"
int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(2, "Usage: bttest n\n");
    exit(1);
  }
  int n = atoi(argv[1]);
  printf("backtrace test begin\n");
  int ret=sleep(n);
  if(ret){
    fprintf(2, "backtrace test failed\n");
    exit(1);
  }
  printf("backtrace test end\n");

  exit(0);
}
