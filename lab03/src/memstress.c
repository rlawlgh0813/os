#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

static void
usage(void) {
  printf(1, "usage: memstress [-n pages] [-t ticks] [-w]\n");
  exit();
}

int
main(int argc, char *argv[])
{
  // 기본값 설정
  int pages = 31;      // 기본값: 31 페이지
  int hold_ticks = 500; // 기본값: 500 틱
  int do_write = 0;    // 기본값: 쓰기 안함
  int i;

  // 옵션 처리
  if(argc > 1 && argv[1][0] == '-'){
    for(i=1; i<argc; i++){
      char *a = argv[i];
      if(a[0] != '-') usage();
      if(a[1] == 'n'){
        if(i+1 >= argc) usage();
        pages = atoi(argv[++i]);
      } else if(a[1] == 't'){
        if(i+1 >= argc) usage();
        hold_ticks = atoi(argv[++i]);
      } else if(a[1] == 'w'){
        do_write = 1;
      } else {
        usage();
      }
    }
  }

  // 헤더 출력
  int pid = getpid();
  printf(1, "[memstress] pid=%d pages=%d hold=%d ticks write=%d\n", pid, pages, hold_ticks, do_write);

  int inc = pages * 4096; 
  char *base = sbrk(inc);
  if (base == (char*)-1) {
    printf(1, "[memstress] sbrk failed\n");
    exit();
  }

  if (do_write) {
    for (int p = 0; p < pages; p++) {
      base[p*4096] = (char)(p & 0xff);
    }
  }

  sleep(hold_ticks);

  printf(1, "[memstress] pid=%d done\n", pid);
  exit();
}
