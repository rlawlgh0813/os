// print_addr.c
#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(1, "Usage: print_addr <path>\n");
    exit();
  }
  
  print_addr(argv[1]);
  exit();
}