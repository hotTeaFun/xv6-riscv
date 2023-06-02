#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define KB(k) ((k)/1024)
int
main(int argc, char *argv[])
{
  struct sysinfo info;
  if(argc != 1){
    fprintf(2, "Usage: sysinfo\n");
    exit(1);
  }
  if(sysinfo(&info))
    fprintf(2, "sysinfo failed\n");
  else
    fprintf(0, "active process cnt: %l\nfree memory cnt: %l kb\n",info.nproc, KB(info.nmem));
  exit(0);
}
