#include "types.h"

// Software TLB (STLB) for caching page table lookups
struct stlb_entry {
  pde_t *pgdir;          // owner address space
  uint   vpg;            // VA page-aligned
  uint   ppg;            // PA page-aligned
  uint   flags;          // PTE flags snapshot (incl. PTE_P)
  struct stlb_entry *next;
};

void stlb_init(void);   // Initialize the STLB
int  stlb_lookup(pde_t *pgdir, uint vpg, uint *ppg_out, uint *flags_out); // Lookup entry for (pgdir,vpg)
void stlb_insert(pde_t *pgdir, uint vpg, uint ppg, uint flags); // Insert (pgdir,vpg) -> (ppg,flags)
void stlb_invalidate_one(pde_t *pgdir, uint vpg); // Invalidate one entry for (pgdir,vpg)
void stlb_invalidate_all_of(pde_t *pgdir); // Invalidate all entries of pgdir

void stlb_stats(uint *hits, uint *misses);  // Get STLB hit/miss statistics
void stlb_printstats(void); // Print STLB hit/miss statistics
