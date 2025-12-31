// softtlb.c — simple software TLB (hash + chaining)
#include "types.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"
#include "softtlb.h"

#define STLB_NBUCKET 1024u // number of hash buckets
#define STLB_HASH(pg, vpg) ((((uint)(pg) >> 6) ^ (vpg >> 12)) & (STLB_NBUCKET-1)) // hash function

static struct stlb_entry *stlb_bkt[STLB_NBUCKET]; // hash table buckets
static struct spinlock stlb_lock;                 // lock for STLB
static uint stlb_hit = 0, stlb_miss = 0;          // stats 

// helper functions to get page-aligned addresses
static inline uint vpage(uint va){ return va & ~0xFFF; } // page-aligned VA
static inline uint ppage(uint pa){ return pa & ~0xFFF; } // page-aligned PA

// Initialize the software TLB
void
stlb_init(void)
{
  initlock(&stlb_lock, "softtlb");  // init spinlock
  for(uint i=0;i<STLB_NBUCKET;i++) stlb_bkt[i]=0; // clear buckets
  stlb_hit = stlb_miss = 0;         // clear stats
}

// Lookup a mapping in the software TLB
int
stlb_lookup(pde_t *pgdir, uint vpg, uint *ppg_out, uint *flags_out)
{
  // compute hash bucket
  uint h = STLB_HASH(pgdir, vpg);

  // acquire lock
  acquire(&stlb_lock);

  // search for entry
  for(struct stlb_entry *e = stlb_bkt[h]; e; e = e->next){
    if(e->pgdir == pgdir && e->vpg == vpg){
      if(ppg_out)   *ppg_out   = e->ppg;
      if(flags_out) *flags_out = e->flags;
      stlb_hit++; // plus one hit
      release(&stlb_lock);
      return 0;   // found
    }
  }

  // not found
  stlb_miss++;
  release(&stlb_lock);
  return -1;
}

// Insert or update a mapping in the software TLB
void
stlb_insert(pde_t *pgdir, uint vpg, uint ppg, uint flags)
{
  uint h = STLB_HASH(pgdir, vpg);
  acquire(&stlb_lock);

  // de-dup: update existing
  for(struct stlb_entry *e = stlb_bkt[h]; e; e = e->next){
    if(e->pgdir == pgdir && e->vpg == vpg){
      e->ppg   = ppg;
      e->flags = flags;
      release(&stlb_lock);
      return;
    }
  }

  // new entry
  struct stlb_entry *e = (struct stlb_entry*)kalloc();
  if(!e){ release(&stlb_lock); return; } // drop on OOM (safe)
  e->pgdir = pgdir;
  e->vpg   = vpg;
  e->ppg   = ppg;
  e->flags = flags;
  e->next  = stlb_bkt[h];
  stlb_bkt[h] = e;

  release(&stlb_lock);
}

// Invalidate a single mapping in the software TLB
void
stlb_invalidate_one(pde_t *pgdir, uint vpg)
{
  uint h = STLB_HASH(pgdir, vpg);
  
  // acquire lock
  acquire(&stlb_lock);
  
  // search and remove entry
  struct stlb_entry **pp = &stlb_bkt[h];
  while(*pp){
    struct stlb_entry *e = *pp;
    if(e->pgdir == pgdir && e->vpg == vpg){
      *pp = e->next;
      kfree((char*)e);
      break;               // unique key; stop
    }else{
      pp = &(*pp)->next;
    }
  }
  
  // release lock
  release(&stlb_lock);
}

// Invalidate all mappings of a given page directory in the software TLB
void
stlb_invalidate_all_of(pde_t *pgdir)
{
  // acquire lock
  acquire(&stlb_lock);
  
  // scan all buckets
  for(uint h=0; h<STLB_NBUCKET; h++){
    struct stlb_entry **pp = &stlb_bkt[h];
    // search and remove entries
    while(*pp){
      struct stlb_entry *e = *pp;
      if(e->pgdir == pgdir){
        *pp = e->next;
        kfree((char*)e);
      }else{
        pp = &(*pp)->next;
      }
    }
  }

  // release lock
  release(&stlb_lock);
}

// Get software TLB statistics
void
stlb_stats(uint *hits, uint *misses)
{
  // acquire lock
  acquire(&stlb_lock);
  
  // return stats
  if(hits)   *hits = stlb_hit;
  if(misses) *misses = stlb_miss;
  
  // release lock
  release(&stlb_lock);
}

// Print software TLB statistics
void
stlb_printstats(void)
{
  acquire(&stlb_lock);
  uint h = stlb_hit, m = stlb_miss;
  release(&stlb_lock);

  // compute rate
  uint total = h + m;
  uint rate = total ? (h * 100) / total : 0;

  // print stats
  cprintf("[STLB] hits=%d misses=%d rate=%d%%\n", (int)h, (int)m, (int)rate);
}