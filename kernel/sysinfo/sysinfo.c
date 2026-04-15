#include <core/proc.h>
#include <mm/kalloc.h>
#include <mm/kmalloc.h>
#include <sysinfo/sysinfo.h>
#include <utils/string.h>

int ksysinfo(const char* path, struct sysinfo* p) {
  if (0 == strcmp(path, "/memory/phys")) {
    extern struct kmem kmem;
    p->u.memory.phys.total_pgs = kmem.total;
    p->u.memory.phys.used_pgs = kmem.total - kmem.free;
    p->u.memory.phys.free_pgs = kmem.free;
    p->u.memory.phys.alloc_times = kmem.alloc_times;
    p->u.memory.phys.free_times = kmem.free_times;
    return 0;
  } else if (0 == strcmp(path, "memory/virt/self")) {
    struct proc* proc = myproc();
    p->u.memory.virt.self.pgfault_cnt = proc->pgfaults;
    return 0;
  } else {
    return -EPATH;
  }

  return -EPATH;
}
