#include "types.h"
#include "stat.h"
#include "user.h"

static int parse_uint(const char *s, int *ok){
  int n=0; *ok=0;
  if(!s || !*s) return 0;
  for(int i=0; s[i]; i++){
    if(s[i]<'0' || s[i]>'9') return 0;
    n = n*10 + (s[i]-'0');
  }
  *ok=1; return n;
}

int main(int argc, char **argv){
  if(argc != 2){
    printf(1, "Usage: snap_delete <id>\n");
    exit();
  }
  int ok=0; int id = parse_uint(argv[1], &ok);
  if(!ok){
    printf(1, "Usage: snap_delete <id>\n");
    exit();
  }
  int r = snapshot_delete(id);
  printf(1, "snapshot_delete(%d) -> %d\n", id, r);
  exit();
}
