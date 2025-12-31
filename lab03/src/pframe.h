// Physical frame tracking
#ifndef PFRAME_H
#define PFRAME_H
#include "types.h"

// Global frame table entry
#define PFNNUM 60000

// Physical frame info structure
struct physframe_info {
  uint frame_index; // Physical frame index
  int allocated;    // 1 if allocated, 0 if free
  int pid;          // PID of the owner process
  uint start_tick;  // Tick when allocated
};

// Defined in kalloc.c
extern struct physframe_info pf_info[PFNNUM];
extern struct spinlock pf_lock;

#endif