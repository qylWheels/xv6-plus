#ifndef _UAPI_SYSINFO_SYSINFO_H_
#define _UAPI_SYSINFO_SYSINFO_H_

#include <core/types.h>

// 目前支持或计划支持的信息：
// /
// └── memory/
//     ├── phys
//     └── virt/
//         └── self

// 错误码
#define EPATH 1  // 路径错误

struct memory_phys_info {
  uint64 total_pgs;
  uint64 used_pgs;
  uint64 free_pgs;
  uint64 alloc_times;
  uint64 free_times;
};

struct memory_virt_info {
  uint64 pgfault_cnt;
};

struct sysinfo {
  union {
    union {
      struct memory_phys_info phys;
      union {
        struct memory_virt_info self;
      } virt;
    } memory;
  } u;
};

#endif  // _UAPI_SYSINFO_SYSINFO_H_
