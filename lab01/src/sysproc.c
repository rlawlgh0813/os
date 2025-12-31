#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"       // for struct proc, myproc()...
#include "spinlock.h"   // for struct spinlock in extern ptable

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

// sys_hello_number
// return n*2 back to user space
int
sys_hello_number(void){
	int n;
	if(argint(0,&n) <0) return -1;  // fetch arg #0 into n
	cprintf("Hello, xv6! Your number is %d\n",n); // print funcion in kernel
	return n*2;
}

// sys_get_procinfo
// copy selected fields of the target process into user buffer
// reference ptable in proc.c
extern struct{
	struct spinlock lock;
	struct proc proc[NPROC];
}ptable;
// ?
struct k_procinfo{
	int pid,ppid,state;
	uint sz;
	char name[16];
};

int 
sys_get_procinfo(void){
	int pid;
	char *uaddr;             // destination buffer
	struct proc *p, *t;
	struct k_procinfo kinfo; // ?

  // fetch args : pid, user buffer pointer
	if(argint(0, &pid) < 0) return -1;
	if(argptr(1, &uaddr, sizeof(struct k_procinfo)) < 0) return -1;
	
  acquire(&ptable.lock);
	// resolve target : pid <= 0 is "self"
  if(pid <= 0) t = myproc();
	else {
		t = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
			if(p->pid == pid){ t = p; break; }
	}
  // validate existence and state
	if(t == 0 || t->state == UNUSED){
		release(&ptable.lock);
		return -1;
	}
	
	// fill kernel-side struct
	kinfo.pid = t->pid;
	kinfo.ppid = t->parent ? t->parent->pid : 0;
	kinfo.state = t->state;
	kinfo.sz = t->sz;
	safestrcpy(kinfo.name,t->name,sizeof(kinfo.name));
	release(&ptable.lock);

	// copy to user-side space
	if(copyout(myproc()->pgdir,(uint)uaddr, (void*)&kinfo, sizeof(kinfo)) < 0) return -1;
	return 0;
}
