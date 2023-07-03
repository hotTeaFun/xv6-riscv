#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "bitset.h"
uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64 sys_sbrk(void) {
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;
  // backtrace();
  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_trace(void) {
  struct proc* p = myproc();
  uint64 tracemask;
  arguint64(0, &tracemask);
  acquire(&p->lock);
  p->tracemask = tracemask;
  release(&p->lock);
  return 0;
}
void do_pageaccess(pagetable_t pt, uint64 addr, int num, uint64* buf) {
  int bl0 = PX(2, addr), bl1 = PX(1, addr), bl2 = PX(0, addr);
  pte_t *pte0 = &pt[bl0], *pte1 = &((pagetable_t)PTE2PA(*pte0))[bl1],
        *pte2 = &((pagetable_t)PTE2PA(*pte1))[bl2];
  for (int i = 0; i < num; i++) {
    if (PTE_FLAGS(*pte2) & PTE_A) {
      bit_set(buf, i);
    }
    if (bl2 == 255) {
      if (bl1 == 255) {
        if (bl0 == 255) {
          panic("do_pageaccess overflow");
        } else {
          bl0++;
          bl1 = 0;
          bl2 = 0;
          *pte0 &= (~PTE_A);
          *pte1 &= (~PTE_A);
          *pte2 &= (~PTE_A);
          pte0 = &pt[bl0], pte1 = &((pagetable_t)PTE2PA(*pte0))[bl1],
          pte2 = &((pagetable_t)PTE2PA(*pte1))[bl2];
        }
      } else {
        bl1++;
        bl2 = 0;
        *pte1 &= (~PTE_A);
        *pte2 &= (~PTE_A);
        pte1 = &((pagetable_t)PTE2PA(*pte0))[bl1],
        pte2 = &((pagetable_t)PTE2PA(*pte1))[bl2];
      }
    } else {
      *pte2 &= (~PTE_A);
      bl2++;
      pte2 = &((pagetable_t)PTE2PA(*pte1))[bl2];
    }
  }
}
uint64 sys_pgaccess(void) {
  uint64 addr;
  int num;
  uint64 dest;
  argaddr(0, &addr);
  argint(1, &num);
  argaddr(2, &dest);
  struct proc* p = myproc();
  pagetable_t pagetable = p->pagetable;
  if (num <= 0 || num > MAXPAGENUM * UINT64WIDTH) {
    return -1;
  }
  uint64 buf[5];
  memset(buf, 0, sizeof(buf));
  do_pageaccess(pagetable, addr, num, buf);
  int len = BYTEROUNDUP(num) >> 3;
  if (copyout(p->pagetable,p->vma, dest, (char*)buf, len) < 0) {
    return -1;
  }
  return 0;
}

uint64 sys_sigalarm(void){
  struct proc* p=myproc();
  argint(0,&p->totticks);
  if(p->totticks < 0){
    printf("sigalarm invalid interval: %d",p->totticks);
    return -1;
  }
  p->ticks = p->totticks;
  argaddr(1,&p->alarmhandler);
  return 0;
}
extern char alarmtrapframe[512];
uint64 sys_sigreturn(void){
  struct proc* p=myproc();
  memmove(p->trapframe,alarmtrapframe,512);
  p->ticks = p->totticks;
  return 0;
}
