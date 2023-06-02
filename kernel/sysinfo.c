#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_sysinfo(void){
  struct proc *p = myproc();
  struct sysinfo info;
  info.nproc = getactiveprocnum();
  info.nmem = kgetfree();
  if(info.nproc < 0||info.nmem < 0){
    return -1;
  }
  uint64 addr;
  argaddr(0, &addr);
  if(copyout(p->pagetable, addr, (char*)&info,sizeof(info)) < 0){
    return -1;
  }
  return 0;
}