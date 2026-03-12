#include <kernel/core/types.h>
#include <kernel/mm/physmem_info.h>
#include "user/user.h"

int main(int argc, char *argv[])
{
    struct physmem_info pi;
    
    physmem_info(&pi);
    printf("total=%ld, used=%ld, free=%ld, alloctimes=%ld, freetimes=%ld\n",
           pi.info_4k.total_pgs, pi.info_4k.used_pgs, pi.info_4k.free_pgs, pi.info_4k.alloc_times, pi.info_4k.free_times);

    sbrk(4096 * 10);

    physmem_info(&pi);
    printf("total=%ld, used=%ld, free=%ld, alloctimes=%ld, freetimes=%ld\n",
           pi.info_4k.total_pgs, pi.info_4k.used_pgs, pi.info_4k.free_pgs, pi.info_4k.alloc_times, pi.info_4k.free_times);

    sbrk(-4096 * 10);

    physmem_info(&pi);
    printf("total=%ld, used=%ld, free=%ld, alloctimes=%ld, freetimes=%ld\n",
           pi.info_4k.total_pgs, pi.info_4k.used_pgs, pi.info_4k.free_pgs, pi.info_4k.alloc_times, pi.info_4k.free_times);

    return 0;
}
