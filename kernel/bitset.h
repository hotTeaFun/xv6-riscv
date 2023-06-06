#pragma once
#include "kernel/types.h"
#define UINT64SHIFT 6
#define UINT64MASK 0x3F
#define UINT64WIDTH (sizeof(uint64) * 8)
#define BYTEROUNDUP(sz) (((sz) + 8 - 1) & ~(8 - 1))
#define MAXPAGENUM 1024
static inline void bit_set(uint64* bitset, uint64 pos) {
  uint64 idx = pos >> UINT64SHIFT;
  uint64 shift = pos & UINT64MASK;
  bitset[idx] |= 1 << shift;
}
static inline int bit_get(uint64* bitset, uint64 pos) {
  uint64 idx = pos >> UINT64SHIFT;
  uint64 shift = pos & UINT64MASK;
  return bitset[idx] & (1 << shift);
}
static inline void bit_clear(uint64* bitset, uint64 pos) {
  uint64 idx = pos >> UINT64SHIFT;
  uint64 shift = pos & UINT64MASK;
  bitset[idx] &= ~(1 << shift);
}