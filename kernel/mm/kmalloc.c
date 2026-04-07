#include <core/riscv.h>
#include <mm/kalloc.h>
#include <mm/kmalloc.h>
#include <sync/spinlock.h>
#include <test/unity_fixture.h>
#include <utils/misc.h>
#include <utils/printf.h>

// cache的个数
#define NCACHE (9)

// 对象的大小范围为2^3~2^11字节
#define MAXBLOCKSZ (1 << 11)

// 若内存块空闲，kmalloc_block_head嵌入在该内存块的头部；
// 若内存块被占用，则该内存块存储用户数据，kmalloc_block_head不在该内存块的任何位置中
struct kmalloc_block_head {
  struct kmalloc_block_head* nextfree;
};

// 这个结构会嵌在由kalloc()分配的页的头部
struct kmalloc_page {
  void* base;  // 用户可用的空间的起始地址
  struct kmalloc_block_head* firstfree;
  struct kmalloc_page* next;
};

struct kmalloc_cache {
  struct spinlock lock;
  uint16 block_size;  // cache中每块的大小
  struct kmalloc_page* current;
  struct kmalloc_page* partial;
  struct kmalloc_page* full;
};

struct kmalloc_caches {
  struct kmalloc_cache kmalloc_cache_list[NCACHE];
} caches;

// 不考虑n=0时的情况，因为该情况已被kmalloc排除
#define ROUNDUP(n)              \
  ({                            \
    uint64 _r = 1;              \
    if (n <= 8) {               \
      _r = 8;                   \
    } else {                    \
      uint64 _v = (n);          \
      while (_r < _v) _r <<= 1; \
    }                           \
    _r;                         \
  })

// 统计n的前导0个数
static int leading_zeros(uint64 x) {
  if (x == 0) {
    return 64;
  }

  int n = 0;

  // 检查高 32 位是否全为 0
  if ((x & 0xFFFFFFFF00000000ULL) == 0) {
    n += 32;
    x <<= 32;
  }
  // 检查剩余部分的高 16 位是否全为 0
  if ((x & 0xFFFF000000000000ULL) == 0) {
    n += 16;
    x <<= 16;
  }
  // 检查剩余部分的高 8 位是否全为 0
  if ((x & 0xFF00000000000000ULL) == 0) {
    n += 8;
    x <<= 8;
  }
  // 检查剩余部分的高 4 位是否全为 0
  if ((x & 0xF000000000000000ULL) == 0) {
    n += 4;
    x <<= 4;
  }
  // 检查剩余部分的高 2 位是否全为 0
  if ((x & 0xC000000000000000ULL) == 0) {
    n += 2;
    x <<= 2;
  }
  // 最后检查最高位是否为 0
  if ((x & 0x8000000000000000ULL) == 0) {
    n += 1;
  }

  return n;
}

// 初始化kmalloc内部的数据结构，需要在main里调用
void kmallocinit(void) {
  for (int i = 0; i < NCACHE; ++i) {
    initlock(&caches.kmalloc_cache_list[i].lock, "cache");
    acquire(&caches.kmalloc_cache_list[i].lock);
    caches.kmalloc_cache_list[i].block_size = 1 << (i + 3);
    caches.kmalloc_cache_list[i].current = NULL;
    caches.kmalloc_cache_list[i].partial = NULL;
    caches.kmalloc_cache_list[i].full = NULL;
    release(&caches.kmalloc_cache_list[i].lock);
  }
}

// 初始化一个新分配的页
static void init_page(void* page, uint16 block_size) {
  for (void* p = page + sizeof(struct kmalloc_page); p < page + PGSIZE;
       p += block_size) {
    struct kmalloc_block_head* head = (struct kmalloc_block_head*)p;
    if (p + block_size < page + PGSIZE) {
      head->nextfree = (struct kmalloc_block_head*)(p + block_size);
    } else {
      head->nextfree = NULL;
    }
  }
}

void* kmalloc(uint64 sz) {
  if (sz == 0) {
    return NULL;
  }

  if (sz > MAXBLOCKSZ) {
    return NULL;
  }

  uint64 roundup_sz = ROUNDUP(sz);
  uint64 cache_list_index = 64 - leading_zeros(roundup_sz) - 4;
  struct kmalloc_cache* cache = &caches.kmalloc_cache_list[cache_list_index];

  acquire(&cache->lock);

  // current为空，分配一个新的页，将其初始化，并加入current中
  if (cache->current == NULL) {
    void* page = kalloc();
    cache->current = page;
    init_page(page, cache->block_size);
    cache->current->base = page + sizeof(struct kmalloc_page);
    cache->current->firstfree = page + sizeof(struct kmalloc_page);
    cache->current->next = NULL;
  }

  // 先从current中取出一个块
  struct kmalloc_block_head* mem = cache->current->firstfree;
  cache->current->firstfree = mem->nextfree;

  // 若获取块后current已满
  if (cache->current->firstfree == NULL) {
    // 将其加入full链表
    cache->current->next = cache->full;
    cache->full = cache->current;

    // 并尝试从partial中选一个page进入current
    // 若partial为空
    if (cache->partial == NULL) {
      // 申请新页
      void* page = kalloc();
      cache->partial = page;
      init_page(page, cache->block_size);
      cache->partial->base = page + sizeof(struct kmalloc_page);
      cache->partial->firstfree = page + sizeof(struct kmalloc_page);
      cache->partial->next = NULL;
    }
    // 现在partial一定有page，将第一个page放置到current中
    cache->current = cache->partial;
    cache->partial = cache->partial->next;
  }

  release(&cache->lock);

  // 返回分配的内存
  return (void*)mem;
}

void kmfree(void* p) {
  // 判断指针指向的内存属于哪个cache，哪个链表（current/partial/full）
  for (int i = 0; i < NCACHE; ++i) {
    acquire(&caches.kmalloc_cache_list[i].lock);

    struct kmalloc_page* pg;

    // 依次检查current页，以及遍历partial、full链表
    struct kmalloc_page* current = caches.kmalloc_cache_list[i].current;
    struct kmalloc_page* partial = caches.kmalloc_cache_list[i].partial;
    struct kmalloc_page* full = caches.kmalloc_cache_list[i].full;

    // 检查current页
    pg = current;
    if (pg == NULL) {  // 该cache尚未被动用，直接找下一个cache
      release(&caches.kmalloc_cache_list[i].lock);
      continue;
    }
    // 若指针属于current页内
    if (p >= pg->base && (uint64)p < (uint64)pg + PGSIZE) {
      struct kmalloc_block_head* blk_head = p;
      blk_head->nextfree = pg->firstfree;
      pg->firstfree = blk_head;
      release(&caches.kmalloc_cache_list[i].lock);
      return;
    }

    // 扫描partial链表
    for (pg = partial; NULL != pg; pg = pg->next) {
      if (p >= pg->base && (uint64)p < (uint64)pg + PGSIZE) {
        struct kmalloc_block_head* blk_head = p;
        blk_head->nextfree = pg->firstfree;
        pg->firstfree = blk_head;
        release(&caches.kmalloc_cache_list[i].lock);
        return;
      }
    }

    // 扫描full链表
    struct kmalloc_page* prev_pg = NULL;
    for (pg = full; NULL != pg; pg = pg->next) {
      if (p >= pg->base && (uint64)p < (uint64)pg + PGSIZE) {
        struct kmalloc_block_head* blk_head = p;
        blk_head->nextfree = pg->firstfree;
        pg->firstfree = blk_head;

        // 现在该页（pg）有空闲空间了，将该页从full链表中删除
        // 若该页为full链表中的第一个页
        if (NULL == prev_pg) {
          caches.kmalloc_cache_list[i].full = pg->next;
        } else {  // 若该页不是full链表的第一个页
          prev_pg->next = pg->next;
        }

        // 把该页放到partial链表中
        pg->next = partial;
        caches.kmalloc_cache_list[i].partial = pg;
        release(&caches.kmalloc_cache_list[i].lock);
        return;
      }
      prev_pg = pg;
    }

    release(&caches.kmalloc_cache_list[i].lock);
  }
}

// 单元测试
#ifdef UNIT_TEST

TEST_GROUP(kmalloc);
TEST_SETUP(kmalloc) {}
TEST_TEAR_DOWN(kmalloc) {}

TEST(kmalloc, test_leading_zeros) {
  TEST_ASSERT_EQUAL(leading_zeros(0), 64);
  TEST_ASSERT_EQUAL(leading_zeros(1ull << 63), 0);
  TEST_ASSERT_EQUAL(leading_zeros(114514), 47);
}

TEST(kmalloc, test_ROUNDUP) {
  // 0<n<=8
  TEST_ASSERT_EQUAL(ROUNDUP(1), 8);
  TEST_ASSERT_EQUAL(ROUNDUP(3), 8);
  TEST_ASSERT_EQUAL(ROUNDUP(8), 8);

  // n>8
  TEST_ASSERT_EQUAL(ROUNDUP(9), 16);
  TEST_ASSERT_EQUAL(ROUNDUP(15), 16);
  TEST_ASSERT_EQUAL(ROUNDUP(16), 16);
  TEST_ASSERT_EQUAL(ROUNDUP(2047), 2048);
  TEST_ASSERT_EQUAL(ROUNDUP(2048), 2048);
}

TEST(kmalloc, test_kmalloc) {
  // sz=0
  TEST_ASSERT_NULL(kmalloc(0));

  // 0<sz<=2048
  TEST_ASSERT_NULL(caches.kmalloc_cache_list[0].current);
  TEST_ASSERT_NOT_NULL(kmalloc(1));
  TEST_ASSERT_NOT_NULL(caches.kmalloc_cache_list[0].current);

  TEST_ASSERT_NULL(caches.kmalloc_cache_list[1].current);
  TEST_ASSERT_NOT_NULL(kmalloc(15));
  TEST_ASSERT_NOT_NULL(caches.kmalloc_cache_list[1].current);

  struct kmalloc_page* old_current = caches.kmalloc_cache_list[1].current;
  for (int i = 0; i < 1000; ++i) {
    kmalloc(15);
  }
  TEST_ASSERT_NOT_NULL(caches.kmalloc_cache_list[1].full);
  TEST_ASSERT_NOT_EQUAL(caches.kmalloc_cache_list[1].current, old_current);

  TEST_ASSERT_NOT_NULL(kmalloc(514));
  TEST_ASSERT_NOT_NULL(kmalloc(1919));
  TEST_ASSERT_NOT_NULL(kmalloc(2047));
  TEST_ASSERT_NOT_NULL(kmalloc(2048));

  // sz>2048
  TEST_ASSERT_NULL(kmalloc(2049));
  TEST_ASSERT_NULL(kmalloc(16384));
}

#define foreach_cache(index, list)                               \
  do {                                                           \
    struct kmalloc_page* pg;                                     \
    for (pg = caches.kmalloc_cache_list[index].list; NULL != pg; \
         pg = pg->next) {                                        \
      printf("%p\n", pg);                                        \
    }                                                            \
  } while (0)

TEST(kmalloc, test_kmfree) {
  void* p = kmalloc(32);
  for (int i = 0; i < 130; i++) {
    kmalloc(32);
  }

  TEST_ASSERT_NOT_NULL(caches.kmalloc_cache_list[2].full);
  TEST_ASSERT_NULL(caches.kmalloc_cache_list[2].partial);

  kmfree(p);

  TEST_ASSERT_NULL(caches.kmalloc_cache_list[2].full);
  TEST_ASSERT_NOT_NULL(caches.kmalloc_cache_list[2].partial);
  TEST_ASSERT_EQUAL(caches.kmalloc_cache_list[2].partial->firstfree, p);
}

#endif  // UNIT_TEST
