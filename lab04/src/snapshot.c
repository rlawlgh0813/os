// snapshot.c — A-1: refcount core (lazy-init, no disk I/O at boot)

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

extern struct superblock sb;

#ifndef BPB
#define BPB (BSIZE*8)
#endif

#define SNAPDIR  "snapshot"
#define RC_FNAME ".refmap"

static struct spinlock rc_lock;

static ushort *rc_tab;     // in-memory refcount table (data blocks only)
static uint    rc_nblks;   // == sb.nblocks
static uint    rc_data_start; // first absolute data block

static struct inode *rc_ip;    // inode for /snapshot/.refmap (lazy)
static int rc_saving;          // reentrancy guard for save_refmap()
static int rc_ready;           // .refmap is present & table synced?

// -------- internal helpers --------

// get index into rc_tab for block b
static inline int rc_idx(uint b, uint *idx_out){
  if(b < rc_data_start) return 0; // not a data block
  uint idx = b - rc_data_start;   // data block index
  if(idx >= rc_nblks) return 0;   // out of range
  *idx_out = idx;
  return 1;
}

// ensure /snapshot and /snapshot/.refmap exist
static void ensure_snapshot_layout(void){
  struct inode *root = namei("/");
  if(root == 0) panic("ensure_snapshot_layout: no root");

  // /snapshot
  ilock(root);
  struct inode *snap = dirlookup(root, SNAPDIR, 0); // look for /snapshot
  if(snap == 0){
    snap = ialloc(root->dev, T_DIR);
    if(!snap) panic("mk SNAPDIR: ialloc");
    ilock(snap);
    snap->nlink = 1; iupdate(snap); // link count for "."
    // create . and .. entries
    if(dirlink(snap, ".",  snap->inum) < 0) panic("mk SNAPDIR: .");
    if(dirlink(snap, "..", root->inum) < 0) panic("mk SNAPDIR: ..");
    if(dirlink(root, SNAPDIR, snap->inum) < 0) panic("mk SNAPDIR: link");
    root->nlink++; iupdate(root);
    iunlock(snap);
  }
  iunlock(root);

  // /snapshot/.refmap
  ilock(snap);
  struct inode *ip = dirlookup(snap, RC_FNAME, 0);
  if(ip == 0){
    ip = ialloc(snap->dev, T_FILE);
    if(!ip) panic("mk .refmap: ialloc");
    ilock(ip);
    ip->nlink = 1; iupdate(ip); // link count
    // link into /snapshot
    if(dirlink(snap, RC_FNAME, ip->inum) < 0) panic("mk .refmap: link");  
    iunlock(ip);
  }
  iunlock(snap);

  rc_ip = ip; // hold rc_ip reference
  iput(snap);
  iput(root);
}

// save rc_tab into rc_ip on disk
static void save_refmap(void){
  if(!rc_ip || !rc_tab) panic("save_refmap: uninit");
  if(rc_saving) return;
  rc_saving = 1;

  begin_op();
  ilock(rc_ip);

  uint need = rc_nblks * sizeof(ushort);
  if(rc_ip->size != need){
    uint off = 0;
    char z[BSIZE];
    memset(z, 0, BSIZE);
    while(off < need){
      uint m = (need - off > BSIZE) ? BSIZE : (need - off);
      if(writei(rc_ip, z, off, m) != m) panic("save_refmap: grow");
      off += m;
    }
  }
  if(writei(rc_ip, (char*)rc_tab, 0, need) != need)
    panic("save_refmap: write");

  iunlock(rc_ip);
  end_op();
  rc_saving = 0;
}

// load .refmap from disk into rc_tab
static void load_refmap(void){
  uint need = rc_nblks * sizeof(ushort);
  if(need > PGSIZE) panic("load_refmap: table too big");

  rc_tab = (ushort*)kalloc();
  if(!rc_tab) panic("load_refmap: kalloc");
  memset(rc_tab, 0, PGSIZE);

  ilock(rc_ip);
  uint have = rc_ip->size;
  if(have >= need){
    if(readi(rc_ip, (char*)rc_tab, 0, need) != need)
      panic("load_refmap: read");
    iunlock(rc_ip);
  }else{
    iunlock(rc_ip);
    save_refmap();
  }
}


// -------- public API --------

// Boot-time init (NO disk I/O). Just set up memory table.
void rc_init(void){
  initlock(&rc_lock, "rc");
  // set parameters from superblock
  rc_nblks      = sb.nblocks;
  rc_data_start = sb.bmapstart + ((sb.size + BPB - 1)/BPB);

  // allocate in-memory table
  uint need = rc_nblks * sizeof(ushort);
  if(need > PGSIZE) panic("rc_init: table too big");

  // allocate zeroed table
  rc_tab = (ushort*)kalloc();
  if(!rc_tab) panic("rc_init: kalloc");
  memset(rc_tab, 0, PGSIZE);

  // mark uninitialized
  rc_ip = 0;
  rc_ready = 0;
  rc_saving = 0;

  cprintf("rc_init(minimal): data_start=%u data_blocks=%u bytes=%u\n",
          rc_data_start, rc_nblks, rc_nblks * (int)sizeof(ushort));
}

// Call once later (e.g., at start of snapshot syscalls).
void rc_ensure_mounted(void){
  if(rc_ready) return;

  ensure_snapshot_layout();  // /snapshot, .refmap

  uint need = rc_nblks * sizeof(ushort);

  if(rc_tab == 0){
    // load from disk
    load_refmap();
  } else {
    ilock(rc_ip);
    uint have = rc_ip->size; // existing size on disk
    iunlock(rc_ip);
    if(have < need) save_refmap(); // grow on disk
  }
  rc_ready = 1;
}

// get current refcount for block b
int rc_get(uint b){
  uint idx; if(!rc_idx(b, &idx)) return 0;
  int v; acquire(&rc_lock); v = rc_tab[idx]; release(&rc_lock); return v;
}

// return new refcount after increment; never above 0xFFFF
int rc_inc(uint b){
  // get index
  uint idx; if(!rc_idx(b, &idx)) return 0;
  int v; acquire(&rc_lock);
  // prevent overflow
  if(rc_tab[idx] < 0xFFFF) rc_tab[idx]++;
  // return new value
  v = rc_tab[idx]; release(&rc_lock); return v;
}

// return new refcount after decrement; never below 0
int rc_dec(uint b){
  // get index
  uint idx; if(!rc_idx(b, &idx)) return 0;
  int v; acquire(&rc_lock);
  if(rc_tab[idx] > 0) rc_tab[idx]--;  // prevent underflow
  // return new value
  v = rc_tab[idx]; release(&rc_lock); return v;
}

// Mark block as allocated (refcount >= 1). No-op if already allocated.
void rc_mark_alloc(uint b){
  // get index
  uint idx; if(!rc_idx(b, &idx)) return;
  acquire(&rc_lock);
  // mark as allocated
  if(rc_tab[idx] == 0) rc_tab[idx] = 1;
  release(&rc_lock);
}

// For now, keep it safe: do nothing if called during save.
void rc_flush(void){
  if(!rc_ready) return;   // not mounted yet → nothing to do
  if(rc_saving) return;   // reentrancy guard
  save_refmap();
}