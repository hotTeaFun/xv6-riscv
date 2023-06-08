#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

/* Possible states of a thread: */
#define FREE 0x0
#define RUNNING 0x1
#define RUNNABLE 0x2

#define STACK_SIZE 8192
#define MAX_THREAD 4
// Saved registers for thread context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  char stack[STACK_SIZE]; /* the thread's stack */
  int state;              /* FREE, RUNNING, RUNNABLE */
  struct context context;
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(struct context *, struct context *);
volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void thread_a(void) {
  int i;
  printf("thread_a started\n");
  a_started = 1;
  while (b_started == 0 || c_started == 0) thread_yield();

  for (i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);

  current_thread->state = FREE;
  thread_schedule();
}

void thread_b(void) {
  int i;
  printf("thread_b started\n");
  b_started = 1;
  while (a_started == 0 || c_started == 0) thread_yield();

  for (i = 0; i < 100; i++) {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  current_thread->state = FREE;
  thread_schedule();
}

void thread_c(void) {
  int i;
  printf("thread_c started\n");
  c_started = 1;
  while (a_started == 0 || b_started == 0) thread_yield();

  for (i = 0; i < 100; i++) {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  current_thread->state = FREE;
  thread_schedule();
}

int main(int argc, char *argv[]) {
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  thread_schedule();
  exit(0);
}