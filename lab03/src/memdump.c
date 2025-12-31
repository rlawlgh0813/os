#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define MAX_FRINFO 60000 

static void
usage(void)
{
    printf(1, "usage: memdump [-a] [-p PID]\n");
    exit();
}

int main(int argc, char *argv[])
{
    if (argc == 1) usage();

    // 옵션 변수 초기화
    int show_all = 0;
    int pid_filter = -1;
    int i;
    
    // 옵션 처리
    if(argc > 1 && argv[1][0] == '-'){
        for(i = 1; i < argc; i++){
            char *a = argv[i];
            if(a[0] != '-') usage();
            if(a[1] == 'a'){
                show_all = 1;
            }else if(a[1] == 'p'){
                if(i + 1 >= argc) usage();
                pid_filter = atoi(argv[++i]);
            }else {
                usage();
            }
        }
    }

    static struct physframe_info buf[MAX_FRINFO];
    int n = dump_physmem_info((void *)buf, MAX_FRINFO);
    if (n < 0)
    {
        printf(1, "memdump: dump_physmem_info failed\n");
        exit();
    }

    printf(1, "[memdump] pid=%d\n", getpid());
    printf(1, "[frame#]\t[alloc]\t[pid]\t[start_tick]\n");

    // 출력 루프
    for(i = 0; i < n; i++){
        // 프레임 정보 가져오기
        struct physframe_info *e = &buf[i];

        // 필터링
        if(!show_all && e->allocated == 0) continue;
        if(pid_filter >= 0 && e->pid != pid_filter) continue;

        // 출력
        printf(1, "%d\t%d\t%d\t%d\n", e->frame_index, e->allocated, e->pid, e->start_tick);
    }
    exit();
}