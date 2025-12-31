#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 1) {
    printf(1, "Usage: snap_create\n");
    exit();
  }

  int id = snapshot_create();
  // error handling if snapshot creation failed
  if(id < 0){
    printf(1, "snapshot_create failed\n");
    exit();
  }
  printf(1, "snapshot_create -> %d\n", id); // print the snapshot id
  
  exit();
}
