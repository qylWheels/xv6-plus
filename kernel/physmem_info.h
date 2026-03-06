#include "types.h"

// 内核中4k页面和2m页面的相关信息
struct physmem_info
{
    struct
    {
        uint64 total_pgs;
        uint64 used_pgs;
        uint64 free_pgs;
        uint64 alloc_times;
        uint64 free_times;

    } info_4k, info_2m;
};

void kphysmem_info(struct physmem_info *pi);
