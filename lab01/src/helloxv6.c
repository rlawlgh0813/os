#include "types.h"	// type definitions used by xv6
#include "stat.h"	// file status structures
#include "user.h"	// user-space system call interfaces

int main(int argc, char *argv[]){
	// call hello_number with test arguments
	int res = hello_number(5);
	int res2 = hello_number(-7);

	// print the return values of hello_number
	printf(1, "hello_number(5) returned %d\n", res);
	printf(1, "hello_number(-7) returned %d\n", res2);

	// terminate
	exit();
}