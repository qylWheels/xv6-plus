#include <uapi/sysinfo/sysinfo.h>

#include "user.h"

int main(int argc, char* argv[]) {
  struct sysinfo s;

  if (0 != sysinfo("/memory/phys", &s)) {
    printf("sysinfo failed\n");
    exit(1);
  }
  printf("used pages: %ld\n", s.u.memory.phys.used_pgs);
  printf("alloc times: %ld\n", s.u.memory.phys.alloc_times);

  char* mem = sbrklazy(16 * 4096);
  mem[1] = 5;
  mem[4096 * 5 + 4] = 114;
  if (0 != sysinfo("/memory/virt/self", &s)) {
    printf("sysinfo failed\n");
    exit(1);
  }
  printf("page fault count: %ld\n", s.u.memory.virt.self.pgfault_cnt);
}
