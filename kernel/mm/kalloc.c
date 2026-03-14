// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include <core/types.h>
#include <core/param.h>
#include <mm/memlayout.h>
#include <mm/kalloc.h>
#include <sync/spinlock.h>
#include <core/riscv.h>
#include <utils/printf.h>
#include <utils/string.h>
#include <mm/kalloc.h>
#include <trace/events/kalloc.h>

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct kmem kmem;

int kalloc_times = 0;         // kalloc()调用的次数
struct spinlock counter_lock; // 计数器锁
void kalloc_probe()
{
  acquire(&counter_lock);
  kalloc_times += 1;
  release(&counter_lock);
}

void kinit()
{
  // 注册针对kalloc的probe
  initlock(&counter_lock, "kalloc_times_lock");
  reg_trace_kalloc_probe(kalloc_probe);

  initlock(&kmem.lock, "kmem");
  kmem.free = 0;
  kmem.alloc_times = 0;
  kmem.free_times = 0;
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  kmem.total = ((uint64)pa_end - (uint64)p) / PGSIZE + 1;
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
  kmem.free_times -= kmem.total; // 将释放次数重置为0（即不计入freerange时的释放次数）
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.free += 1;
  kmem.free_times += 1;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  trace_kalloc();

  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
  {
    kmem.freelist = r->next;
    kmem.free -= 1;
    kmem.alloc_times += 1;
  }
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
