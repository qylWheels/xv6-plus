#include <uapi/fs/fcntl.h>
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

  pause(30);
  for (int i = 0; i < 333; i++) {
    int fd = open("./grep", O_RDONLY);
    // printf("fd: %d\n", fd);
    char buf[128];
    read(fd, (void*)buf, sizeof(buf));
    close(fd);
  }
  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 1000; j++) {
      for (int k = 0; k < 1000; k++) {
        mem[k] = i;
      }
    }
  }
  if (0 != sysinfo("/proc/sched/self", &s)) {
    printf("sysinfo failed\n");
    exit(1);
  }
  struct proc_sched_info pss = s.u.proc.sched.self;
  printf("tick: %ld, vol_swtch: %ld, invol_swtch: %ld, total_swtch: %ld\n",
         pss.total_ticks, pss.voluntary_switches, pss.involuntary_switches,
         pss.total_switches);

  return 0;
}
