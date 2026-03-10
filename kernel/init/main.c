#include <core/types.h>
#include <core/param.h>
#include <mm/memlayout.h>
#include <mm/kalloc.h>
#include <mm/vm.h>
#include <core/riscv.h>
#include <core/proc.h>
#include <core/trap.h>
#include <utils/printf.h>
#include <drivers/console.h>
#include <drivers/plic.h>
#include <drivers/virtio_disk.h>
#include <fs/bio.h>
#include <fs/fs.h>
#include <fs/file.h>

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
