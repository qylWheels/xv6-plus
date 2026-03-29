#ifndef _MM_KMALLOC_H_

#include <core/types.h>

// 参数size：为0时不做任何事情；大于0时分配size字节内存
// 返回值：若size=0，返回NULL；若size>0，成功时返回指向已分配内存的指针，
// 失败时panic
void* kmalloc(uint64 size);

// 参数p：为NULL时不做任何事情；为非NULL时，则释放其指向的内存
void kmfree(const void* p);

#endif  // _MM_KMALLOC_H_
