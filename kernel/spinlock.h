#ifndef _SPIN_LOCK_H_
#define _SPIN_LOCK_H_
// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  int nts;
  int n;

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};
#endif

