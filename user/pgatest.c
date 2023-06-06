#include "kernel/types.h"
#include "user/user.h"
#include "kernel/bitset.h"
int main(int argc, char* argv[]) {
  const int num=10;
  if (argc != 1) {
    fprintf(2, "Usage: pgatest\n");
    exit(1);
  }
  int *addr=(int*)malloc(8*(1<<12));
  uint64 buf[100];
  printf("pgaccess test begin\n");
  printf("addr: %p, buf: %p\n",buf,addr);
  addr[0]=1;
  addr[1<<12]=2;
  int ret=pgaccess(addr,num,buf);
  if(ret){
    fprintf(2, "pgaccess test failed\n");
    exit(1);
  }
  for(int i=0;i<num;i++){
    if(bit_get(buf,i)){
      printf("page %d is accessed\n",i);
    }
  }
  addr[1<<11]=3;
  ret=pgaccess(addr,num,buf);
  if(ret){
    fprintf(2, "pgaccess test failed\n");
    exit(1);
  }
  for(int i=0;i<num;i++){
    if(bit_get(buf,i)){
      printf("page %d is accessed\n",i);
    }
  }
  printf("pgaccess test end\n");

  exit(0);
}
