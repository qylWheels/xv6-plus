#include <kernel/uapi/core/types.h>
#include <kernel/uapi/mm/pgfault_info.h>
#include "user/user.h"

int main(int argc, char *argv[])
{
    struct pgfault_info pi;

    pgfault_info(&pi);
    printf("pgfault count: %d\n", pi.pgfault_cnt);

    // 要用sbrklazy()才能测试成功，否则会马上就分配内存
    char *arr = sbrklazy(4096 * 16);
    for (int i = 0; i < 4096 * 16; ++i)
    {
        arr[i] = 1;
    }

    pgfault_info(&pi);
    printf("pgfault count: %d\n", pi.pgfault_cnt);

    return 0;
}
