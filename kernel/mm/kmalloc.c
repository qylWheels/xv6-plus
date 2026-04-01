#include <core/riscv.h>
#include <mm/kalloc.h>
#include <mm/kmalloc.h>
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

struct kmalloc_page {
  struct kmalloc_block_head* firstfree;
  struct kmalloc_page* next;
};

struct kmalloc_cache {
  uint16 block_size;  // cache中每块的大小
  struct kmalloc_page* current;
  struct kmalloc_page* partial;
  struct kmalloc_page* full;
};

struct kmalloc_caches {
  struct kmalloc_cache kmalloc_cache_list[NCACHE];
} caches;

// 不考虑n=0时的情况，因为该情况已被kmalloc排除
// FIXME: 修复n已经是2的x次方时，仍会向上取整的问题
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
    caches.kmalloc_cache_list[i].block_size = 1 << (i + 3);
    caches.kmalloc_cache_list[i].current = NULL;
    caches.kmalloc_cache_list[i].partial = NULL;
    caches.kmalloc_cache_list[i].full = NULL;
  }
}

// 初始化一个新分配的页
static void init_page(void* page, uint16 block_size) {
  for (void* p = page; p < page + PGSIZE; p += block_size) {
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

  // current为空，分配一个新的页，将其初始化，并加入current中
  if (cache->current == NULL) {
    void* page = kalloc();
    init_page(page, cache->block_size);
    cache->current = page;
  }

  // 先从current中取出一个块
  struct kmalloc_block_head* mem = cache->current->firstfree;
  cache->current->firstfree->nextfree = mem->nextfree;

  // 若获取块后current已满
  if (cache->current->firstfree == NULL) {
    // 将其加入full链表
    cache->current->next = cache->full->next;
    cache->full->next = cache->current;

    // 并尝试从partial中选一个page进入current
    // 若partial为空
    if (cache->partial == NULL) {
      // 申请新页
      void* page = kalloc();

      // 初始化该页
      init_page(page, cache->block_size);

      // 将该页加到partial中
      cache->partial = page;
    }
    // 现在partial一定有page，将第一个page放置到current中
    cache->current = cache->partial;
    cache->partial = cache->partial->next;
  }

  // 返回分配的内存
  return (void*)mem;
}

void kmfree(const void* p) {}

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
  TEST_ASSERT_NOT_NULL(kmalloc(1));
  TEST_ASSERT_NOT_NULL(kmalloc(15));
  TEST_ASSERT_NOT_NULL(kmalloc(514));
  TEST_ASSERT_NOT_NULL(kmalloc(1919));
  TEST_ASSERT_NOT_NULL(kmalloc(1919));
  TEST_ASSERT_NOT_NULL(kmalloc(1919));
  TEST_ASSERT_NOT_NULL(kmalloc(2047));
  TEST_ASSERT_NOT_NULL(kmalloc(2047));
  TEST_ASSERT_NOT_NULL(kmalloc(2047));
  TEST_ASSERT_NOT_NULL(kmalloc(2048));
  TEST_ASSERT_NOT_NULL(kmalloc(2048));
  TEST_ASSERT_NOT_NULL(kmalloc(2048));

  // sz>2048
  TEST_ASSERT_NULL(kmalloc(2049));
  TEST_ASSERT_NULL(kmalloc(16384));
}

#endif  // UNIT_TEST
