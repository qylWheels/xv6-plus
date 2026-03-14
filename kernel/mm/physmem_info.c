#include <sync/spinlock.h>
#include <core/types.h>
#include <mm/physmem_info.h>
#include <mm/vm.h>
#include <mm/kalloc.h>
#include <core/proc.h>

extern struct kmem kmem;
extern int kalloc_times;
extern struct spinlock kalloc_cnt_lock;
extern int kfree_times;
extern struct spinlock kfree_cnt_lock;

void kphysmem_info(struct physmem_info *pi)
{
    acquire(&kmem.lock);
    acquire(&kalloc_cnt_lock);
    acquire(&kfree_cnt_lock);
    struct proc *p = myproc();
    copyout(p->pagetable, (uint64)&pi->info_4k.total_pgs, (char *)&kmem.total, sizeof(kmem.total));
    uint64 used_pgs = kmem.total - kmem.free;
    copyout(p->pagetable, (uint64)&pi->info_4k.used_pgs, (char *)&used_pgs, sizeof(used_pgs));
    copyout(p->pagetable, (uint64)&pi->info_4k.free_pgs, (char *)&kmem.free, sizeof(kmem.free));
    // copyout(p->pagetable, (uint64)&pi->info_4k.alloc_times, (char *)&kmem.alloc_times, sizeof(kmem.alloc_times));
    copyout(p->pagetable, (uint64)&pi->info_4k.alloc_times, (char *)&kalloc_times, sizeof(kalloc_times));
    // copyout(p->pagetable, (uint64)&pi->info_4k.free_times, (char *)&kmem.free_times, sizeof(kmem.free_times));
    copyout(p->pagetable, (uint64)&pi->info_4k.free_times, (char *)&kfree_times, sizeof(kfree_times));
    release(&kfree_cnt_lock);
    release(&kalloc_cnt_lock);
    release(&kmem.lock);
    return;
}
