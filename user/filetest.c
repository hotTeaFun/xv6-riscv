#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
int main(int argc, char *argv[]) {
  // uint64 addrs[] = {0x80000000LL, 0xffffffffffffffff};
  // char buf[1024];
  // int fd = open("README", 0);
  // if (fd < 0) {
  //   printf("open(README) failed\n");
  //   exit(1);
  // }
  // int n = read(fd, (void *)buf, 100);
  // if (n != 100) {
  //   printf("read(fd, %p, 8192) returned %d, not 100\n", buf, n);
  //   exit(1);
  // }else{
  //   printf("%s\n",buf);
  // }
  // close(fd);
  // for (int ai = 0; ai < 2; ai++) {
  //   uint64 addr = addrs[ai];

  //   int fd = open("README", 0);
  //   if (fd < 0) {
  //     printf("open(README) failed\n");
  //     exit(1);
  //   }
  //   int n = read(fd, (void *)addr, 8192);
  //   if (n > 0) {
  //     printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", addr, n);
  //     exit(1);
  //   }
  //   close(fd);
  // }
  char buf[BSIZE];
  const char *s="bigfile test";
  int i, fd, n;
  const int maxi=MAXFILE;
  unlink("big");
  fd = open("big", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: error: creat big failed!\n", s);
    exit(1);
  }

  for(i = 0; i < maxi; i++){
    ((int*)buf)[0] = i;
    if(write(fd, buf, BSIZE) != BSIZE){
      printf("%s: error: write big file failed\n", s, i);
      exit(1);
    }
  }

  close(fd);

  fd = open("big", O_RDONLY);
  if(fd < 0){
    printf("%s: error: open big failed!\n", s);
    exit(1);
  }

  n = 0;
  for(;;){
    i = read(fd, buf, BSIZE);
    if(i == 0){
      if(n != maxi){
        printf("%s: read only %d blocks from big", s, n);
        exit(1);
      }
      break;
    } else if(i != BSIZE){
      printf("%s: read failed %d\n", s, i);
      exit(1);
    }
    if(((int*)buf)[0] != n){
      printf("%s: read content of block %d is %d\n", s,
             n, ((int*)buf)[0]);
      exit(1);
    }
    n++;
  }
  close(fd);
  if(unlink("big") < 0){
    printf("%s: unlink big failed\n", s);
    exit(1);
  }
  return 0;
}
