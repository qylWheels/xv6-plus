#include <core/proc.h>
#include <mm/kalloc.h>
#include <mm/kmalloc.h>
#include <mm/vm.h>
#include <sysinfo/sysinfo.h>
#include <utils/string.h>

int ksysinfo(const char* path, struct sysinfo* p) {
  if (0 == strcmp(path, "/proc/sched/self")) {
    struct proc* proc = myproc();
    acquire(&proc->lock);
    copyout(proc->pagetable, (uint64)&p->u.proc.sched.self.total_ticks,
            (char*)&proc->ticks, sizeof(proc->ticks));
    copyout(proc->pagetable, (uint64)&p->u.proc.sched.self.voluntary_switches,
            (char*)&proc->volun_switches, sizeof(proc->volun_switches));
    copyout(proc->pagetable, (uint64)&p->u.proc.sched.self.involuntary_switches,
            (char*)&proc->involun_switches, sizeof(proc->involun_switches));
    uint64 total_switches = proc->volun_switches + proc->involun_switches;
    copyout(proc->pagetable, (uint64)&p->u.proc.sched.self.total_switches,
            (char*)&total_switches, sizeof(total_switches));
    release(&proc->lock);
    return 0;
  } else if (0 == strcmp(path, "/memory/phys")) {
    extern struct kmem kmem;
    extern int kalloc_times;
    extern struct spinlock kalloc_cnt_lock;
    extern int kfree_times;
    extern struct spinlock kfree_cnt_lock;

    acquire(&kmem.lock);

    struct proc* proc = myproc();

    copyout(proc->pagetable, (uint64)&p->u.memory.phys.total_pgs,
            (char*)&kmem.total, sizeof(kmem.total));

    uint64 used_pgs = kmem.total - kmem.free;
    copyout(proc->pagetable, (uint64)&p->u.memory.phys.used_pgs,
            (char*)&used_pgs, sizeof(used_pgs));

    copyout(proc->pagetable, (uint64)&p->u.memory.phys.free_pgs,
            (char*)&kmem.free, sizeof(kmem.free));

    acquire(&kalloc_cnt_lock);
    acquire(&kfree_cnt_lock);
    copyout(proc->pagetable, (uint64)&p->u.memory.phys.alloc_times,
            (char*)&kalloc_times, sizeof(kalloc_times));
    copyout(proc->pagetable, (uint64)&p->u.memory.phys.free_times,
            (char*)&kfree_times, sizeof(kfree_times));
    release(&kfree_cnt_lock);
    release(&kalloc_cnt_lock);

    release(&kmem.lock);
    return 0;
  } else if (0 == strcmp(path, "/memory/virt/self")) {
    struct proc* proc = myproc();
    acquire(&proc->lock);
    copyout(proc->pagetable, (uint64)&p->u.memory.virt.self.pgfault_cnt,
            (char*)&proc->pgfaults, sizeof(proc->pgfaults));
    release(&proc->lock);
    return 0;
  } else {
    return -EPATH;
  }

  return -EPATH;
}
