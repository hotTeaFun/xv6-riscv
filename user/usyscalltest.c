#include "kernel/types.h"
#include "user/user.h"
int main(int argc, char* argv[]) {
  if (argc != 1) {
    fprintf(2, "Usage: usyscalltest\n");
    exit(1);
  }
  int pid=ugetpid();
  printf("usyscall test begin\n");
  printf("ugetpid: %d\n",pid);
  printf("usyscall test end\n");

  exit(0);
}
