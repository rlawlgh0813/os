#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "stat.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "buf.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// ===== Snapshots: helpers + syscalls (fixed includes/types) =====

static int itoa10(int x, char *buf){
  int i=0, j=0; char tmp[16];
  if(x==0){ buf[0]='0'; buf[1]=0; return 1; }
  while(x>0){ tmp[i++] = '0' + (x%10); x/=10; }
  while(i>0) buf[j++] = tmp[--i];
  buf[j]=0; return j;
}

static int streq(const char *a, const char *b){
  while(*a && *b){ if(*a!=*b) return 0; a++; b++; }
  return *a==0 && *b==0;
}

// ---------- directory/file creators (parent must be ilock()'d) ----------

static struct inode*
mkdir_locked(struct inode *dp, char *name)
{
  struct inode *ip;

  // allocate new directory inode
  ip = ialloc(dp->dev, T_DIR);
  if(ip == 0) panic("mkdir_locked: ialloc");
  ilock(ip);
  ip->nlink = 1;        // for "."
  iupdate(ip);

  // "." and ".."
  if(dirlink(ip, ".",  ip->inum) < 0)  panic("mkdir_locked: '.'");
  if(dirlink(ip, "..", dp->inum) < 0)  panic("mkdir_locked: '..'");

  // link into parent
  if(dirlink(dp, name, ip->inum) < 0)  panic("mkdir_locked: link parent");

  // parent has one more subdir
  dp->nlink++;
  iupdate(dp);

  iunlock(ip);
  return ip; // caller still holds a ref via icache
}

static struct inode*
create_file_locked(struct inode *dp, char *name)
{
  // allocate new file inode
  struct inode *ip = ialloc(dp->dev, T_FILE);
  if(ip == 0) panic("create_file_locked: ialloc");
  
  ilock(ip); // lock new inode
  ip->nlink = 1;
  iupdate(ip);
  // link into parent
  if(dirlink(dp, name, ip->inum) < 0) panic("create_file_locked: dirlink");
  iunlock(ip); // unlock new inode
  return ip;
}

// ---------- refcount helpers for file data blocks ----------

static void rc_inc_file_blocks(struct inode *f)
{
  // direct
  for(int i=0;i<NDIRECT;i++){
    uint b = f->addrs[i];
    if(b) rc_inc(b);
  }
  // indirect entries (if present)
  uint ib = f->addrs[NDIRECT];
  if(ib){
    rc_inc(ib); // indirect block itself
    struct buf *bp = bread(f->dev, ib);
    uint *addr = (uint*)bp->data;
    for(int k=0;k<NINDIRECT;k++){
      uint b = addr[k];
      if(b) rc_inc(b);
    }
    brelse(bp);
  }
}

static int dir_is_root(struct inode *dp){
  return dp->inum == ROOTINO;
}

// clone contents of src_dir into dst_dir
static int
clone_tree(struct inode *dst_dir, struct inode *src_dir)
{
  struct dirent de;
  uint off = 0;
  int n;

  while((n = readi(src_dir, (char*)&de, off, sizeof(de))) == sizeof(de)){
    off += sizeof(de);
    // skip invalid entries
    if(de.inum == 0) continue;
    if(streq(de.name, ".") || streq(de.name, "..")) continue;
    if(dir_is_root(src_dir) && streq(de.name, "snapshot")) continue;

    // lookup child in src_dir
    uint child_off = 0;
    struct inode *ch = dirlookup(src_dir, de.name, &child_off);
    if(ch == 0) continue;
    ilock(ch);

    if(ch->type == T_DIR){
      // create subdir in dst_dir
      struct inode *sub = dirlookup(dst_dir, de.name, 0);
      if(!sub) sub = mkdir_locked(dst_dir, de.name);

      ilock(sub);
      int ok = clone_tree(sub, ch); // recursive clone
      iunlock(sub);
      iput(sub);

      iunlock(ch);
      iput(ch);
      // check recursive result
      if(!ok) return 0;

    } else if(ch->type == T_FILE){
      struct inode *nf = dirlookup(dst_dir, de.name, 0);
      if(!nf) nf = create_file_locked(dst_dir, de.name);

      ilock(nf);

      // copy metadata + block pointers
      nf->size = ch->size;
      for(int i=0;i<NDIRECT;i++) nf->addrs[i] = ch->addrs[i];
      nf->addrs[NDIRECT] = ch->addrs[NDIRECT];
      iupdate(nf);

      // increment refcounts for data blocks
      rc_inc_file_blocks(ch);

      iunlock(nf);
      iput(nf);
      iunlock(ch);
      iput(ch);

    } else if(ch->type == T_DEV){
      // skip device files
      iunlock(ch);
      iput(ch);
      continue;
    } else {
      // unknown type
      iunlock(ch);
      iput(ch);
    }
  }
  if(n < 0) return 0;
  return 1;
}

// clear all children under 'dir'
static int
clear_tree(struct inode *dir, int skip_snapshot)
{
  struct dirent de;
  uint off = 0;
  int n;

  while((n = readi(dir, (char*)&de, off, sizeof(de))) == sizeof(de)){
    off += sizeof(de);
    if(de.inum == 0) continue;
    if(streq(de.name, ".") || streq(de.name, "..")) continue;
    if(skip_snapshot && dir_is_root(dir) && streq(de.name, "snapshot"))
      continue;

    uint off_child = 0;
    struct inode *ip = dirlookup(dir, de.name, &off_child);
    if(ip == 0) continue;
    ilock(ip);

    if(ip->type == T_DIR){
      int ok = clear_tree(ip, 0);
      if(!ok){ iunlockput(ip); return 0; }

      // remove subdir entry from parent
      struct dirent z; memset(&z, 0, sizeof(z));
      if(writei(dir, (char*)&z, off_child, sizeof(z)) != sizeof(z)){
        iunlockput(ip); return 0;
      }
      // fix link counts
      dir->nlink--; iupdate(dir);
      ip->nlink--;  iupdate(ip);
      iunlockput(ip);

    } else if(ip->type == T_FILE){
      // unlink file entry
      struct dirent z; memset(&z, 0, sizeof(z));
      if(writei(dir, (char*)&z, off_child, sizeof(z)) != sizeof(z)){
        iunlockput(ip); return 0;
      }
      ip->nlink--; iupdate(ip);
      iunlockput(ip);

    } else if(ip->type == T_DEV){
      // unlink device entry
      struct dirent z; memset(&z, 0, sizeof(z));
      if(writei(dir, (char*)&z, off_child, sizeof(z)) != sizeof(z)){
        iunlockput(ip); return 0;
      }
      ip->nlink--; iupdate(ip);
      iunlockput(ip);

    } else {
      iunlockput(ip);
    }
  }
  if(n < 0) return 0;
  return 1;
}

// ---------- syscalls ----------

int
sys_snapshot_create(void)
{
  begin_op();

  rc_ensure_mounted();   // lazy-mount /snapshot and load .refmap

  struct inode *root = namei("/");
  struct inode *snap = namei("/snapshot");
  if(!root || !snap){ end_op(); return -1; }

  // find next available numeric ID under /snapshot
  char name[16];
  int id = 0;
  for(;; id++){
    itoa10(id, name);
    ilock(snap);
    struct inode *exist = dirlookup(snap, name, 0);
    iunlock(snap);
    if(!exist) break;
    iput(exist);
  }

  // create /snapshot/[id]
  ilock(snap);
  struct inode *dst = dirlookup(snap, name, 0);
  if(!dst) dst = mkdir_locked(snap, name);
  iunlock(snap);

  // clone root -> /snapshot/[id]
  ilock(root);
  ilock(dst);
  int ok = clone_tree(dst, root);
  iunlock(dst);
  iunlock(root);

  end_op();

  rc_flush();  // persist refmap once

  if(!ok) return -1;
  return id;
}

int
sys_snapshot_delete(void)
{
  int id;
  if(argint(0, &id) < 0) return -1;
  if(id < 0) return -1;

  begin_op();

  rc_ensure_mounted();

  struct inode *snap = namei("/snapshot");
  if(!snap){ end_op(); return -1; }

  char name[16]; itoa10(id, name);

  ilock(snap);
  uint off = 0;
  struct inode *idroot = dirlookup(snap, name, &off);
  iunlock(snap);

  if(!idroot){ end_op(); return -1; }
  ilock(idroot);

  // clear children of /snapshot/[id]
  int ok = clear_tree(idroot, 0);
  iunlock(idroot);

  // unlink [id] entry from /snapshot
  ilock(snap);
  struct dirent z; memset(&z, 0, sizeof(z));
  if(writei(snap, (char*)&z, off, sizeof(z)) != sizeof(z)){
    iunlock(snap); iput(idroot); end_op(); return -1;
  }
  snap->nlink--; iupdate(snap);
  iunlock(snap);

  // decrease link count of idroot
  ilock(idroot);
  idroot->nlink--;
  iupdate(idroot);
  iunlock(idroot);

  iput(idroot);
  end_op();

  rc_flush();
  return ok? 0 : -1;
}

int
sys_snapshot_rollback(void)
{
  int id;
  if(argint(0, &id) < 0) return -1;
  if(id < 0) return -1;

  begin_op();

  rc_ensure_mounted();

  struct inode *snap = namei("/snapshot");
  struct inode *root = namei("/");
  if(!snap || !root){ end_op(); return -1; }

  char name[16]; itoa10(id, name);

  // find snapshot source dir
  ilock(snap);
  struct inode *src = dirlookup(snap, name, 0);
  iunlock(snap);
  if(!src){ end_op(); return -1; }

  // 1) clear current root (keep /snapshot if present)
  ilock(root);
  int ok = clear_tree(root, /*skip_snapshot=*/1);
  iunlock(root);
  if(!ok){ iput(src); end_op(); return -1; }

  // 2) clone snapshot tree into root
  ilock(root);
  ilock(src);
  ok = clone_tree(root, src);
  iunlock(src);
  iunlock(root);

  iput(src);
  end_op();

  rc_flush();
  return ok? 0 : -1;
}

// ========== print_addr ==========
int
sys_print_addr(void)
{
  char *path;
  // get path argument
  if(argstr(0, &path) < 0)
    return -1;

  // get inode
  struct inode *ip = namei(path);
  if(ip == 0)
    return -1;

  // lock inode
  ilock(ip);

  // Only print for regular files/dirs. For others, print nothing.
  if(ip->type != T_FILE && ip->type != T_DIR){
    iunlockput(ip);
    return 0;
  }

  // number of logical blocks in file
  uint nblk = (ip->size + BSIZE - 1) / BSIZE;

  // direct blocks
  uint limit = nblk < NDIRECT ? nblk : NDIRECT;
  for(uint i = 0; i < limit; i++){
    uint a = ip->addrs[i];
    if(a != 0)
      cprintf("addr[%d] : %x\n", i, a);
  }

  // indirect pointer + entries
  if(nblk > NDIRECT){
    uint ib = ip->addrs[NDIRECT];
    if(ib != 0){
      cprintf("addr[%d] : %x (INDIRECT POINTER)\n", NDIRECT, ib);
      struct buf *bp = bread(ip->dev, ib);
      uint *addr = (uint*)bp->data;
      uint cnt = nblk - NDIRECT;
      if(cnt > NINDIRECT) cnt = NINDIRECT;
      for(uint k = 0; k < cnt; k++){
        if(addr[k] != 0)
          cprintf("addr[%d] -> [%d] (bn : %d) : %x\n",
                  NDIRECT, k, NDIRECT + k, addr[k]);
      }
      brelse(bp);
    }
  }

  // finish with newline
  cprintf("\n");

  // release inode
  iunlockput(ip);
  return 0;
}