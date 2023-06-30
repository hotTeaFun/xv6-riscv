// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


void freerange(void *pa_start, void *pa_end);
void initfreecnt();
void initref();
extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint64 freememcnt;
  int refv[PHYPAGENUM];
} kmem;

static inline int kpgindex(void* pa){
  if(((uint64)pa % PGSIZE) != 0 || (uint64)pa < PGROUNDUP((uint64)end) || (uint64)pa >= PHYSTOP){
    return -1;
  }
  return ((uint64)pa-PGROUNDUP((uint64)end))>>PGSHIFT;
}
void kinit() {
  initlock(&kmem.lock, "kmem");
  initfreecnt();
  initref();
  freerange(end, (void *)PHYSTOP);
}
void initref(){
  memset(kmem.refv,0,NELEM(kmem.refv));
}
void initfreecnt() { kmem.freememcnt = 0; }
void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) kmeminit(p);
}
// increase the ref counter of the physics page contains pa.
// @return the page base addr.
void* kgetpage(void* pa) {
  void * addr= (void*)PGROUNDDOWN((uint64)pa);
  int idx = kpgindex(addr);
  if (idx < 0)
    panic("kgetpage");
  acquire(&kmem.lock);
  if(kmem.refv[idx] <= 0){
    panic("kgetpage illegal ref");
  }
  kmem.refv[idx]++;
  release(&kmem.lock);
  return addr;
}
// 1. decrease ref counter of the page of physical memory pointed at by pa,
// which normally should have been returned by a call to kalloc().
// 2. free the page if no refs.
void kfree(void* pa) {
  int idx = kpgindex(pa);
  if (idx < 0)
    panic("kputpage");
  acquire(&kmem.lock);
  if(kmem.refv[idx] < 0){
    panic("kfree illegal ref");
  }
  if(--kmem.refv[idx] == 0){
    // Fill with junk to catch dangling refs.
   kpgfree(pa);
  }
  release(&kmem.lock);
}
// clean page at pa and put it into the freelist.
// called by kmeminit during the memory initailization.
void kpgfree(void* pa){
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  struct run *r = (struct run *)pa;

  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.freememcnt += PGSIZE;
}
void kmeminit(void* pa) {
  int idx = kpgindex(pa);
  if (idx < 0)
    panic("kputpage");
  acquire(&kmem.lock);
  if(kmem.refv[idx] != 0){
    panic("kmeminit illegal ref");
  }
  kpgfree(pa);
  release(&kmem.lock);
}
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    int idx = kpgindex(r);
    if(idx < 0 || kmem.refv[idx] != 0){
      panic("kalloc pg index or ref illegal");
    }
    kmem.refv[idx] = 1;
    kmem.freelist = r->next;
    kmem.freememcnt -= PGSIZE;
  }
  release(&kmem.lock);

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}

uint64 kgetfree(void) {
  uint64 cnt = -1;
  acquire(&kmem.lock);
  cnt = kmem.freememcnt;
  release(&kmem.lock);
  return cnt;
}