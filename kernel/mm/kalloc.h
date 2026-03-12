#ifndef _MM_KALLOC_H_
#define _MM_KALLOC_H_

#include <sync/spinlock.h>

struct kmem
{
    struct spinlock lock;
    struct run *freelist;
    uint64 total; // 页总数
    uint64 free; // 空闲页数量
    uint64 alloc_times; // 分配次数
    uint64 free_times; // 释放次数
};

void *kalloc(void);
void kfree(void *);
void kinit(void);

#endif // _MM_KALLOC_H_
