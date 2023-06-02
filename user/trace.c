#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define MAXARGSNUM 20
int
main(int argc, char *argv[])
{
  char* nargv[20];
  uint64 maskcode;
  int i;
  if(argc <= 2){
    fprintf(2, "Usage: trace mask program\n");
    exit(1);
  }
  maskcode = atoul(argv[1]);
  if(trace(maskcode)){
    fprintf(2, "trace failed\n");
    exit(1);
  }
  for(i = 2; i < argc; i++){
    nargv[i-2] = argv[i];
  }
  return exec(nargv[0],nargv);
}
