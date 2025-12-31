#include "types.h"	// type definitions used by xv6
#include "stat.h"	// file status structures
#include "user.h"	// user-space system call interfaces

// convert numeric process state to string
static char* s2str(int s){
	switch(s){
		case 0 : return "UNUSED";
		case 1 : return "EMBRYO";
		case 2 : return "SLEEPING";
		case 3 : return "RUNNABLE";
		case 4 : return "RUNNING";
		case 5 : return "ZOMBIE";
	}
	return "UNKNOWN";
}

int main(int argc, char *argv[]){
	struct procinfo info;
	int pid = (argc >= 2) ? atoi(argv[1]) : 0;	// if no argument, pid = 0 is "self"

	// call get_procinfo() to fetch process info
	if(get_procinfo(pid, &info) < 0){
		printf(2, "psinfo : get_procinfo() failed\n");
		exit();
	}
	// print process info
	printf(1, "PID=%d PPID=%d STATE=%s SZ=%d NAME=%s\n",info.pid,info.ppid,s2str(info.state),info.sz,info.name);
	
	// terminate the program
	exit();
}
