#include "types.h"
#include "param.h"
#include "mmu.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "ipt.h"

static struct ipt_entry *ipt_buckets[IPT_HASH_SIZE];
static struct spinlock   ipt_lock;

// PFN-global reference counter: how many (pgdir,vpg) mappings refer to PFN.
#define MAX_PFN   (PHYSTOP >> 12)
static int ipt_pfn_refcnt[MAX_PFN];

// helpers for hashing and validation
static inline uint
vpage(uint va)
{
  return va & ~0xFFF; // page-align
}

static inline int
valid_pfn(uint pfn)
{
  return pfn < MAX_PFN;
}

int ipt_pfn_refs(uint pfn) {
  int n;
  acquire(&ipt_lock);
  n = (pfn < MAX_PFN) ? ipt_pfn_refcnt[pfn] : 0;
  release(&ipt_lock);
  return n;
}

// Initialize the IPT.
void
ipt_init(void)
{
  initlock(&ipt_lock, "ipt");
  for (int i = 0; i < IPT_HASH_SIZE; i++)
    ipt_buckets[i] = 0;
  for (int i = 0; i < MAX_PFN; i++)
    ipt_pfn_refcnt[i] = 0;
}

// Insert mapping (pfn, pgdir, vpg) with flags.
int
ipt_insert(uint pfn, pde_t *pgdir, uint va, uint flags)
{
  uint vpg = vpage(va);
  int  h   = IPT_HASH(pfn);

  acquire(&ipt_lock);

  // de-dup: identical (pfn, pgdir, vpg) exists → refresh flags and return
  for (struct ipt_entry *e = ipt_buckets[h]; e; e = e->next) {
    if (e->pfn == pfn && e->pgdir == pgdir && e->va == vpg) {
      e->flags = flags;
      release(&ipt_lock);
      return 0;
    }
  }

  // allocate new entry
  struct ipt_entry *e = (struct ipt_entry*)kalloc();
  if (!e) {
    release(&ipt_lock);
    return -1; // OOM: drop silently is also acceptable
  }
  e->pfn   = pfn;
  e->pgdir = pgdir;
  e->va    = vpg;
  e->flags = flags;
  e->refcnt = 1;     // per-entry ref = 1 (global refcnt is separate)
  e->next  = ipt_buckets[h];
  ipt_buckets[h] = e;

  if (valid_pfn(pfn))
    ipt_pfn_refcnt[pfn]++;

  release(&ipt_lock);
  return 0;
}

// Remove mapping for exact key (pfn, pgdir, vpg).
int
ipt_remove(uint pfn, pde_t *pgdir, uint va)
{
  uint vpg = vpage(va);
  int  h   = IPT_HASH(pfn);
  int  removed = 0;

  acquire(&ipt_lock);

  struct ipt_entry **pp = &ipt_buckets[h];
  while (*pp) {
    struct ipt_entry *e = *pp;
    if (e->pfn == pfn && e->pgdir == pgdir && e->va == vpg) {
      *pp = e->next;         // unlink
      kfree((char*)e);
      removed++;
      continue;              // keep scanning to remove duplicates if any
    }
    pp = &(*pp)->next;
  }

  if (removed && valid_pfn(pfn)) {
    ipt_pfn_refcnt[pfn] -= removed;
    if (ipt_pfn_refcnt[pfn] < 0) ipt_pfn_refcnt[pfn] = 0; // safety
  }

  release(&ipt_lock);
  return removed; // useful for debugging
}

// Remove all mappings owned by pgdir.
void
ipt_remove_all_of(pde_t *pgdir)
{
  acquire(&ipt_lock);
  for(int h=0; h<IPT_HASH_SIZE; h++){
    struct ipt_entry **pp = &ipt_buckets[h];
    while(*pp){
      struct ipt_entry *e = *pp;
      if(e->pgdir == pgdir){
        uint pfn = e->pfn;
        *pp = e->next;
        kfree((char*)e);
        if(valid_pfn(pfn) && ipt_pfn_refcnt[pfn] > 0)
          ipt_pfn_refcnt[pfn]--;
      }else{
        pp = &(*pp)->next;
      }
    }
  }
  release(&ipt_lock);
}

// List mappings for a PFN into kernel buffer` kbuf (array of ipt_entry).
int
ipt_list_for_pfn(uint pfn, struct ipt_entry *kbuf, int max)
{
  if (max <= 0 || !kbuf) return 0;

  int h = IPT_HASH(pfn);
  int n = 0;

  acquire(&ipt_lock);

  for (struct ipt_entry *e = ipt_buckets[h]; e && n < max; e = e->next) {
    if (e->pfn != pfn) continue;

    // copy out a compact view; .refcnt shows PFN-wide total references
    kbuf[n].pfn    = e->pfn;
    kbuf[n].pgdir  = e->pgdir;
    kbuf[n].va     = e->va;
    kbuf[n].flags  = e->flags;
    kbuf[n].refcnt = valid_pfn(pfn) ? ipt_pfn_refcnt[pfn] : 0;
    kbuf[n].next   = 0; // not used by callers
    n++;
  }

  release(&ipt_lock);
  return n; // number of entries written
}