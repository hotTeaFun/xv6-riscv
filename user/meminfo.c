#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define KB(k) ((k)/1024)
int
main(int argc, char *argv[])
{
  uint64 freememcnt;
  if(argc != 1){
    fprintf(2, "meminfo\n");
    exit(1);
  }
  freememcnt=meminfo();
  if(freememcnt<0)
    fprintf(2, "meminfo failed\n");
  else
    fprintf(0,"free memory cnt: %l kb\n",KB(freememcnt));
  exit(0);
}
