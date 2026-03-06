#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "kalloc.h"
#include "physmem_info.h"

extern struct kmem kmem;

void kphysmem_info(struct physmem_info *pi)
{
    acquire(&kmem.lock);
    struct proc *p = myproc();
    copyout(p->pagetable, (uint64)&pi->info_4k.total_pgs, (char *)&kmem.total, sizeof(kmem.total));
    uint64 used_pgs = kmem.total - kmem.free;
    copyout(p->pagetable, (uint64)&pi->info_4k.used_pgs, (char *)&used_pgs, sizeof(used_pgs));
    copyout(p->pagetable, (uint64)&pi->info_4k.free_pgs, (char *)&kmem.free, sizeof(kmem.free));
    copyout(p->pagetable, (uint64)&pi->info_4k.alloc_times, (char *)&kmem.alloc_times, sizeof(kmem.alloc_times));
    copyout(p->pagetable, (uint64)&pi->info_4k.free_times, (char *)&kmem.free_times, sizeof(kmem.free_times));
    release(&kmem.lock);
    return;
}
