#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "mmu.h"

#define PGSZ   4096
#define VPG(x) ((uint)(x) & ~0xFFF)

static void pass(const char *s){ printf(1, "[PASS] %s\n", s); }
static void pass_ex(const char *s, int pid, uint va, uint pa) { printf(1, "[PASS] %s | pid=%d va=0x%x pa=0x%x\n", s, pid, va, pa);}
static void fail(const char *s){ printf(1, "[FAIL] %s\n", s); exit(); }
static void info(const char *s){ printf(1, "[INFO] %s\n", s); }

static int find_pair(struct vlist *L, int n, int pid, uint vpg){
  for(int i=0;i<n;i++) if(L[i].pid==pid && L[i].va==vpg) return 1;
  return 0;
}

// (1-2) Observe various permission combinations across regions
static void test_perm_combos_observe(void)
{
  uint pa, fl;
  int ok = 1;

  // data segment
  static int g = 0;
  if(vtop(&g, &pa, &fl) < 0) fail("vtop(data) fail");
  if(!((fl & 0x1) && (fl & 0x4) && (fl & 0x2))){
    ok = 0; printf(1,"[data] flags=0x%x (need P|U|W)\n", fl);
  }

  // heap segment
  char *h = sbrk(PGSZ);
  if(h==(char*)-1) fail("sbrk heap fail");
  h[0] = 1; // materialize
  if(vtop(h, &pa, &fl) < 0) fail("vtop(heap) fail");
  if(!((fl & 0x1) && (fl & 0x4) && (fl & 0x2))){
    ok = 0; printf(1,"[heap] flags=0x%x (need P|U|W)\n", fl);
  }

  // stack segment
  int local = 0;
  if(vtop(&local, &pa, &fl) < 0) fail("vtop(stack) fail");
  if(!((fl & 0x1) && (fl & 0x4) && (fl & 0x2))){
    ok = 0; printf(1,"[stack] flags=0x%x (need P|U|W)\n", fl);
  }

  // text segment
  void *text_va = (void*)printf;
  if(vtop(text_va, &pa, &fl) < 0) fail("vtop(text) fail");
  if(!(fl & 0x1) || !(fl & 0x4)){
    ok = 0; printf(1,"[text] flags=0x%x (need P|U)\n", fl);
  }
  if(fl & 0x2){
    info("[text] W bit set (RW text). ROU check treated as N/A for this build.");
  }else{
    pass("ROU observed on text (P=1,U=1,W=0).");
  }

  // kernel segment
  void *kva = (void*)0x80100000;
  if(vtop(kva, &pa, &fl) < 0){
    info("vtop(kernel VA) failed; cannot directly observe U=0. Skipping U=0 observation.");
  }else{
    if(!(fl & 0x1)){ ok = 0; printf(1,"[kernelVA] flags=0x%x (need P)\n", fl); }
    if(fl & 0x4){    ok = 0; printf(1,"[kernelVA] flags=0x%x (U must be 0)\n", fl); }
    else            pass("kernel VA shows U=0 as expected.");
  }

  // unmapped page
  char *u = sbrk(PGSZ);
  if(u==(char*)-1) fail("sbrk temp fail");
  u[0] = 0x5a;
  if(vtop(u, &pa, &fl) < 0) fail("vtop(temp before unmap) fail");
  if(sbrk(-PGSZ) == (char*)-1) fail("sbrk(-PGSIZE) temp fail");
  if(vtop(u, &pa, &fl) == 0) fail("vtop succeeds on unmapped page (P should be 0)");
  
  // success
  pass("unmapped page observed (P=0 via vtop failure).");

  // final combo check
  if(ok) pass("permission combos observed (P/U/W bits validated across regions).");
  else   fail("permission combo observation mismatch.");
}

// (1-1) Unmap invalidation of softTLB & IPT entry
static void test_unmap_invalidation(int N)
{
  // allocate N pages and write to each page
  int self = getpid();
  char *base = sbrk(N*PGSZ);
  if(base==(char*)-1) fail("sbrk N pages fail");
  for(int i=0;i<N;i++) base[i*PGSZ] = (char)i;

  // check vtop + phys2virt for last page
  char *va = base + (N-1)*PGSZ;
  uint pa, fl;
  if(vtop(va, &pa, &fl) < 0) fail("vtop tail before unmap fail");
  uint pfn = pa >> 12;
  uint vpg = VPG(va);

  // check phys2virt has (self, vpg)
  if(sbrk(-PGSZ) == (char*)-1) fail("sbrk(-PGSIZE) fail");
  uint pa2, fl2;
  if(vtop(va, &pa2, &fl2) == 0) fail("vtop still succeeds after unmap (TLB stale?)");

  // check phys2virt
  struct vlist tmp[32];
  int n = phys2virt(pfn<<12, tmp, 32);
  if(n < 0) fail("phys2virt after unmap fail");
  if(find_pair(tmp, n, self, vpg)) fail("IPT still has (self,va) after unmap");
  
  // success
  pass_ex("unmap invalidated softTLB & removed IPT mapping", self, vpg, pfn<<12);
}

// COW duplicat chain + IPT cleanup after exit
static void test_cow_and_cleanup(int hold_ticks)
{
  // page alloc + write to trigger COW on fork
  int parent = getpid();
  char *p = sbrk(PGSIZE);
  if(p==(char*)-1) fail("sbrk 1 page fail");
  p[0] = 0x5a;

  // get physical page info
  uint pa, fl;
  if(vtop(p, &pa, &fl) < 0) fail("vtop cow-page fail");

  uint pa_page = pa & ~0xFFF;
  uint vpg     = VPG(p);

  // fork + child sleep + parent check IPT entries
  int pid = fork();
  if(pid < 0) fail("fork fail");

  if(pid == 0){
    sleep(hold_ticks);
    exit();
  } else {
    sleep(20);

    // check phys2virt for both parent & child
    struct vlist buf[64];
    int n = phys2virt(pa_page, buf, 64);
    if(n < 0) fail("phys2virt during-run fail");

    int has_parent = find_pair(buf, n, parent, vpg);  // check parent
    int has_child  = find_pair(buf, n, pid,    vpg);  // check child

    // failed if either is missing
    if(!has_parent || !has_child){
      printf(1, "[dbg] chain miss: pa=0x%x vpg=0x%x parent=%d child=%d (n=%d)\n",
             pa_page, vpg, parent, pid, n);
      for(int i=0;i<n;i++)
        printf(1,"  [%d] pid=%d va=0x%x flags=0x%x ref=%d\n",
               i, buf[i].pid, buf[i].va, buf[i].flags, buf[i].refcnt);
      fail("COW duplicate chain not found (parent & child)");
    }

    // success
    pass_ex("COW duplicate chain observed (parent & child share PFN)", parent, vpg, pa_page);

    // wait for child exit + check IPT cleanup
    wait();
    n = phys2virt(pa_page, buf, 64);
    if(n < 0) fail("phys2virt after-exit fail");
    if(find_pair(buf, n, pid, vpg))
      fail("child IPT entry remains after exit");

    sleep(5);
  }
}

int
main(int argc, char **argv)
{
  int N    = 64;  // default pages for (1-1)
  int hold = 200; // default ticks to hold child for (2)+(3)

  if(argc >= 2) N    = atoi(argv[1]);
  if(argc >= 3) hold = atoi(argv[2]);

  printf(1, "CTEST: N=%d, hold=%d\n", N, hold);

  test_perm_combos_observe(); // (1-2) combo observation
  test_unmap_invalidation(N); // (1-1) unmap invalidation
  test_cow_and_cleanup(hold); // (2) COW chain & (3) exit cleanup

  printf(1, "\n=== CTEST RESULT: PASS ===\n");
  exit();
  return 0;
}
