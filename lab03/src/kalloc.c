// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#include "pframe.h"
#include "proc.h"

// For physical frame tracking
extern uint ticks;

// global frame table & lock for physical frame tracking
struct physframe_info pf_info[PFNNUM];
struct spinlock pf_lock;

// Utility functions for address/frame number conversion
static inline uint pa2pfn(uint pa){ return pa >> 12; }    // Convert physical address to frame number
static inline uint pfn2pa(uint pfn){ return pfn << 12; }  // Convert frame number to physical address

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  // Initialize the physical frame tracking lock
  initlock(&kmem.lock, "kmem");
  initlock(&pf_lock, "pf_lock");
  kmem.use_lock = 0;  // No locking during initialization

  // Initialize the physical frame info table
  for(int i = 0; i < PFNNUM; i++) {
    pf_info[i].frame_index = i;
    pf_info[i].allocated = 0; // Mark all frames as free initially
    pf_info[i].pid = -1;      // No owner process
    pf_info[i].start_tick = 0; // No allocation time
  }
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;  // Enable locking after initialization
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  // Add the page to the free list.
  if(kmem.use_lock) acquire(&kmem.lock);
  // Update physical frame info if locking is enabled
  uint pa = V2P((uint)v);
  uint pfn = pa2pfn(pa);
  if(pfn < PFNNUM){
    if(kmem.use_lock) acquire(&pf_lock);
    pf_info[pfn].allocated = 0; // Mark frame as free
    pf_info[pfn].pid = -1;      // Clear owner process ID
    pf_info[pfn].start_tick = 0; // Clear allocation time
    if(kmem.use_lock) release(&pf_lock);
  }

  // Push the page onto the free list
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;

  // Release the lock if it was acquired
  if(kmem.use_lock) release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  // get a page from the free list with locking
  if(kmem.use_lock) acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;  // Allocate the page

    // Update physical frame info if locking is enabled
    if(kmem.use_lock) {
      struct proc *p = myproc();
      
      // Update physical frame info if a process is allocating
      if(p){
        uint pa = V2P((char*)r);  // Get physical address
        uint pfn = pa2pfn(pa);    // Convert to frame number
        if(pfn < PFNNUM){
          acquire(&pf_lock);
          pf_info[pfn].allocated = 1; // Mark frame as allocated
          pf_info[pfn].pid = p->pid;  // Set owner process ID
          pf_info[pfn].start_tick = ticks; // Set allocation time
          release(&pf_lock);
        }
      }
    }
  }
  if(kmem.use_lock) release(&kmem.lock);

  if(r) memset((char*)r, 5, PGSIZE); // fill with junk
  return (char*)r;
}

