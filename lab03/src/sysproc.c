#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "pframe.h" // for pframe_lookup
#include "ipt.h"   // for ipt_lookup
#include "softtlb.h" // for software TLB functions

// physmem_info system call
int
sys_dump_physmem_info(void)
{
  int uaddr;        // user virtual address
  int max_entries;  // max number of entries that can be stored in the user array

  // Fetch the system call arguments
  if(argint(0, &uaddr) < 0) return -1;
  if(argint(1, &max_entries) < 0) return -1;
  if(max_entries <= 0) return 0;

  // max_entries should not exceed PFNNUM
  int n = max_entries;
  if(n > PFNNUM) n = PFNNUM;

  // copy the physframe_info array to user space
  struct proc *proc = myproc();
  acquire(&pf_lock);
  for(int i=0; i<n; i++){
    if(copyout(proc->pgdir,
               (uint)(uaddr + i * sizeof(struct physframe_info)),
               (void*)&pf_info[i],
               sizeof(struct physframe_info)) < 0){
      release(&pf_lock);
      return -1;  // error in copyout
    } 
  }
  release(&pf_lock);

  // return the number of entries copied
  return n;
}

// vtop system call
extern int sw_vtop(pde_t *pgdir, const void *va, uint *pa, uint *flags);
int
sys_vtop(void)
{
  int va_u, pa_u, flags_u;  // user pointers
  // check user arguments are valid
  if(argint(0, &va_u) < 0) return -1;
  if(argint(1, &pa_u) < 0) return -1;
  if(argint(2, &flags_u) < 0) return -1;

  // sw_vtop to get the physical address and flags
  struct proc *p = myproc();
  uint pa = 0, flags = 0;
  int r = sw_vtop(p->pgdir, (void*)va_u, &pa, &flags);
  if(r < 0) return -1;

  // copy the results back to user space
  if(copyout(p->pgdir, (uint)pa_u, (void*)&pa, sizeof(uint)) < 0) return -1;
  if(copyout(p->pgdir, (uint)flags_u, (void*)&flags, sizeof(uint)) < 0) return -1;
  return 0;
}

// struct proc defined in proc.h
extern struct{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
// pgdir_to_pid system call
static int 
pgdir_to_pid(pde_t *pgdir){
  int pid = -1;

  // search the process table for a process with the given pgdir
  acquire(&ptable.lock);
  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != UNUSED && p->pgdir == pgdir){
      pid = p->pid;
      break;
    }
  }
  release(&ptable.lock);

  return pid;
}

struct vlist {
  int pid;    // PID of the owner process
  uint va;    // Virtual address
  uint flags; // Flags (e.g., valid, dirty)
  int refcnt; // Reference count
};
int
sys_phys2virt(void)
{
  int pa_u, out_u, max;
  // check user arguments are valid
  if(argint(0, &pa_u) < 0) return -1;
  if(argint(1, &out_u) < 0) return -1;
  if(argint(2, &max) < 0) return -1;
  if(max <= 0) return 0;

  uint pa_page = (uint)pa_u & ~0xFFF; // page-aligned physical address
  uint pfn = pa_page >> 12; // physical frame number

  if(max > 64) max = 64; // limit max entries to 64
  struct ipt_entry tmp[64];

  int n = ipt_list_for_pfn(pfn, tmp, max);
  if(n < 0) return -1; // error

  // prepare the vlist array to copy to user space
  struct vlist klist[64];
  for(int i=0; i<n; i++){
    klist[i].pid = pgdir_to_pid(tmp[i].pgdir);
    klist[i].va = tmp[i].va;
    klist[i].flags = tmp[i].flags;
    klist[i].refcnt = tmp[i].refcnt;
  }

  // copy the vlist array to user space
  if(copyout(myproc()->pgdir, (uint)out_u, (char*)klist, n * sizeof(struct vlist)) < 0)
    return -1;
  
  // return number of entries copied
  return n;
}

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
