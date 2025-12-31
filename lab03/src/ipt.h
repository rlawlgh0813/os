// Inverse Page Table
#ifndef IPT_H
#define IPT_H

#include "types.h"

#define IPT_HASH_SIZE 4096
#define IPT_HASH(pfn) ((pfn) & (IPT_HASH_SIZE - 1))

// Inverse page table entry
struct ipt_entry {
  uint pfn;               // Physical frame number
  pde_t *pgdir;           // owner's page directory
  uint va;                // Virtual address
  uint flags;             // Flags (e.g., valid, dirty)
  int refcnt;             // Reference count
  struct ipt_entry *next; // Next entry in the hash bucket
};

// Functions to manage the inverse page table
void ipt_init(void);
int ipt_insert(uint pfn, pde_t *pgdir, uint va, uint flags);
int ipt_remove(uint pfn, pde_t *pgdir, uint va);
int ipt_list_for_pfn(uint pfn, struct ipt_entry *kbuf, int max);
void ipt_remove_all_of(pde_t *pgdir);
int ipt_pfn_refs(uint pfn);
#endif