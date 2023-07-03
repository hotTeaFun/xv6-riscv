#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "proc.h"
#include "fcntl.h"
#include "file.h"
/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

int docow(pagetable_t pt, uint64 va);

int get_vma_idx(struct VMA* vma, uint64 va);

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
  
  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  // if(va >= MAXVA)
  //   panic("walk");
  if(va >= MAXVA)
    return 0;
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte=walk(pagetable, a, 0)) == 0){
      panic("uvmunmap: walk error");
    }
    if(PTE_FLAGS(*pte) == 0){
      continue;
    }
    if(PTE_FLAGS(*pte) == PTE_V){
      panic("uvmunmap: not a leaf");
    }
    if(((*pte)&PTE_V)==0){
      panic("uvmunmap: not present");
    }
    uint64 pa = PTE2PA(*pte);
    if(do_free){
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and use copy-on-write mechanism.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, struct VMA*vma, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i ,npa;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0){
      if(get_vma_idx(vma,i)>=0){
        pte_t* npte=walk(new,i,1);
        if(npte==0){
          panic("uvmcopy: walk error");
        }
        *npte=*pte;
        continue;
      }
      panic("uvmcopy: page not present");
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // if the physics page is writable, then maintain the pte flags.
    if(flags & PTE_W){
      flags |= PTE_COW | PTE_PW;
      flags &= (~PTE_W);
      *pte = PAFLAGS2PTE(pa,flags);
    }
    // if not writable, keep the flags unchanged.

    // ref the physis page.
    npa = (uint64)kgetpage((void*)pa);
    if(npa != pa)
      goto err;
    if(mappages(new, i, PGSIZE, npa, flags) != 0){
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pt, struct VMA* vma, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t* pte0;
  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pte0 = walk(pt,va0,0);
    if(pte0 == 0){
      return -1;
    }
    uint64 flags = PTE_FLAGS(*pte0);
    if(((flags & PTE_V) ==0) || ((flags & PTE_U) == 0))
      return -1;
    if((get_vma_idx(vma,va0))>=0){
      if((flags & PTE_R)==0){
        return -1;
      }
    }
    else if(((flags & PTE_W) == 0) && docow(pt,va0) != 0){
      return -1;
    }
    pa0 = PTE2PA(*pte0);
    if (pa0 == 0){
      return -1;
    }
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

void vmprint(pagetable_t pt){
  printf("page table %p\n",pt);
  do_vmprint(pt,0);
}
void pteprint(pagetable_t pt,int index, int depth){
  pte_t pte=pt[index];
  for(int i=0;i<depth;i++){
    printf("..");
    if(i!=depth-1){
      printf(" ");
    }
  }
  printf("%d: %p(pte addr) %p(pte) %p(flags) %p(pa)\n",index,&pt[index],pte,PTE_FLAGS(pte),PTE2PA(pte));
}
void do_vmprint(pagetable_t pt,int depth){
  for(int i =0;i<512;i++){
    pte_t pte =pt[i];
    if(pte & PTE_V){
        pteprint(pt,i,depth+1);
        if(depth<2){
          do_vmprint((pagetable_t)PTE2PA(pte),depth+1);
        }
    }
  }
}

int get_vma_idx(struct VMA* vma, uint64 va){
  for(uint i=0;i<MAXVMA;i++){
	  struct VMA *v=&vma[i];     
    if(v->used && va>=v->addr && va<v->addr+v->len){ //find corresponding vma
      // lazy allocation		 
	    // char * mem;
	    // if((mem = (char*)kalloc())==0) goto a;
      // memset(mem,0,PGSIZE);
      // va=PGROUNDDOWN(va);
	    // uint64 off=v->start_point+va-v->addr;// starting point + extra offset

	    // // PROT_READ=1 PROT_WRITE=2 PROT_EXEC=4
	    // // PTE_R=2     PTE_W=4      PTE_X=8
	    // // 所以需要将vma[i]->prot 左移一位
	    // if(mappages(p->pagetable,va,PGSIZE,(uint64)mem,(v->prot<<1) |PTE_U  )!=0)
	    // {
	    //    kfree(mem);
	    //    goto a;
	    // }
      //       // read 4096 bytes of relevant file into allocated page
	    // ilock(v->f->ip);
	    // readi(v->f->ip,1,va,off,PGSIZE);
	    // iunlock(v->f->ip);
	    // lazy=1;
	    // break;
      return i;
	 }
  }
  return -1;
}
// fault_flag == 1 when page store fault ,0 when page load fault
// return:
// -1 when va not belong to any vma
// 0 when successed
// 1 when permission denied or other error
int do_lazymmap(struct proc* p,uint64 va,int fault_flag){
  int idx;
  if((idx=get_vma_idx(p->vma,va))<0){
    return -1;
  }
  struct VMA* vma=&p->vma[idx];
  if((fault_flag&&(vma->prot&PROT_WRITE))||(!fault_flag&&(vma->prot&PROT_READ))){
    pte_t* pte=walk(p->pagetable,va,1);
    if(pte==0){
      return 1;
    }
    void* mem=kalloc();
    if(mem==0){
      return 1;
    }
    memset(mem,0,PGSIZE);
    va=PGROUNDDOWN(va);
    uint64 off=vma->start_point+va-vma->addr;
    *pte=PAFLAGS2PTE(mem, vma->prot<<1|PTE_V|PTE_U);
    ilock(vma->f->ip);
    readi(vma->f->ip,1,va,off,PGSIZE);
    iunlock(vma->f->ip);
    return 0;
  }
  return 1;
}
// locate the pte of va, do page allocation and remapping if needed.
// return 0 if successed, -1 otherwise.
int docow(pagetable_t pt, uint64 va){
  
  pte_t* pte = walk(pt,va,0);
  if(pte == 0){
    return -1;
  }
  uint64 flags = PTE_FLAGS(*pte);
  uint64 pa = PTE2PA(*pte);
  if((flags & PTE_V) == 0 ||(flags & PTE_U) == 0){
    return -1;
  }
  if((flags & PTE_COW) && ((flags & PTE_W) ==0) && (flags & PTE_PW)){
      void* mem = kalloc();
      memmove(mem,(void*)pa,PGSIZE);
      kfree((void*)pa);
      flags = (flags & (~PTE_COW) & (~PTE_PW)) | PTE_W;
      *pte = PAFLAGS2PTE(mem,flags);
      return 0;
  }
  return -1;
}

