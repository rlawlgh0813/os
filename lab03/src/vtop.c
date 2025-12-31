#include "types.h"
#include "stat.h"
#include "user.h"

// usage: vtop <hex_va>
static void usage(void){
  printf(1, "usage: vtop <hex_va>>\n");
  exit();
}

// parse a hex string to uint
static uint parse_hex(const char *s){
  uint v = 0;
  int i = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 2 : 0;

  // parse hex digits
  for(; s[i]; i++){
    char c = s[i];
    v <<= 4;
    if(c >= '0' && c <= '9') v |= (c - '0');
    else if(c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
    else if(c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
    else break;
  }
  return v;
}

int
main(int argc, char *argv[]){
  if(argc != 2) usage();

  uint va = parse_hex(argv[1]);
  uint pa = 0, flags = 0;
  int r = vtop((void*)va, &pa, &flags);   // call vtop to translate VA to PA
  if(r < 0) printf(1, "vtop : unmapped or error\n");
  else printf(1, "VA=0x%p -> PA=0x%p, flags=0x%x\n", va, pa, flags);

  exit();
}