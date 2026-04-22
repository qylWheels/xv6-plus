#include <core/param.h>
#include <core/proc.h>
#include <core/riscv.h>
#include <core/trap.h>
#include <core/types.h>
#include <drivers/console.h>
#include <drivers/plic.h>
#include <drivers/virtio_disk.h>
#include <drivers/virtio_gpu.h>
#include <fs/bio.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <mm/kalloc.h>
#include <mm/kmalloc.h>
#include <mm/memlayout.h>
#include <mm/vm.h>
#include <test/unity.h>
#include <test/unity_fixture.h>
#include <utils/printf.h>

volatile static int started = 0;

#ifdef UNIT_TEST
static void run_all_tests(void) { RUN_TEST_GROUP(kmalloc); }
#endif  // UNIT_TEST

// start() jumps here in supervisor mode on all CPUs.
void main() {
  if (cpuid() == 0) {
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();                    // physical page allocator
    kvminit();                  // create kernel page table
    kvminithart();              // turn on paging
    kmallocinit();              // 初始化kmalloc模块
    procinit();                 // process table
    trapinit();                 // trap vectors
    trapinithart();             // install kernel trap vector
    plicinit();                 // set up interrupt controller
    plicinithart();             // ask PLIC for device interrupts
    binit();                    // buffer cache
    iinit();                    // inode table
    fileinit();                 // file table
    virtio_disk_init();         // emulated hard disk
    drivers_virtio_gpu_init();  // 初始化显卡

    int err;
    for (int i = 0; i < 1280; i++) {
      // printf("i=%d\n",i);
      if (0 != (err = drivers_virtio_gpu_draw_pixel(i, 0, 233, 68, 17, 255))) {
        printf("err=%d\n", err);
      }
    }

    userinit();  // first user process
    __sync_synchronize();

// 如果定义了UNIT_TEST宏，则运行单元测试后停机
// 单元测试只在cpu0上运行，以防止同步问题
#ifdef UNIT_TEST
    printf("starting unit test...\n\n");
    int dummy_argc = 1;
    const char* dummy_argv[] = {"xv6"};
    UnityMain(dummy_argc, dummy_argv, run_all_tests);
    printf("\n");
    panic("unit test completed, press <ctrl+a> then press <x> to exit qemu\n");
#endif

    started = 1;
  } else {
    while (started == 0);
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();   // turn on paging
    trapinithart();  // install kernel trap vector
    plicinithart();  // ask PLIC for device interrupts
  }

  scheduler();
}
