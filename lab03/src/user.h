struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(void);
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);

// Physical frame tracking
struct physframe_info{
    uint frame_index; // Physical frame index
    int allocated;    // 1 if allocated, 0 if free
    int pid;          // PID of the owner process
    uint start_tick;  // Tick when allocated
};

// Dump physical memory info to user space
int dump_physmem_info(void *addr, int max_entries);

// Virtual to physical address translation
int vtop(void *va, uint *pa_out, uint *flags_out);

// Get virtual addresses mapping to a physical page
struct vlist{
    int pid;               // Process ID
    uint va;              // Virtual address
    uint flags;           // Page table entry flags
    int refcnt;       // Reference count
};
int phys2virt(uint pa_page, struct vlist *out, int max); 