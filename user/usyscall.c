#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
int ugetpid(void) {
  struct usyscall* roregion = (struct usyscall*)USYSCALL;
  return roregion->pid;
}