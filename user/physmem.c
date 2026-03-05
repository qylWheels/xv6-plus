#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int result = physmem_info(0);
    printf("result=%d\n", result);
    return result;
}
