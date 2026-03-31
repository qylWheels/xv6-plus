#ifndef _MM_KMALLOC_H_

#include <core/types.h>

// 初始化kmalloc模块
void kmallocinit(void);

// 参数size：size=0时返回NULL；size>0且size<=2048时分配size字节内存；size>2048时返回NULL
// 返回值：若size=0，返回NULL；若size>0，成功时返回指向已分配内存的指针，
// 失败时panic
void* kmalloc(uint64 size);

// 参数p：为NULL时不做任何事情；为非NULL时，则释放其指向的内存
void kmfree(const void* p);

#endif  // _MM_KMALLOC_H_
