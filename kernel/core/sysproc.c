#include <core/param.h>
#include <core/proc.h>
#include <core/riscv.h>
#include <core/syscall.h>
#include <core/trap.h>
#include <core/types.h>
#include <mm/memlayout.h>
#include <mm/pgfault_info.h>
#include <mm/physmem_info.h>
#include <mm/vm.h>
#include <sync/spinlock.h>
#include <sysinfo/sysinfo.h>

uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return kfork(); }

uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64 sys_sbrk(void) {
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr) return -1;
    if (addr + n > TRAPFRAME) return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64 sys_pause(void) {
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0) n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 获取系统物理内存相关信息，并填充用户传来的结构体
uint64 sys_physmem_info(void) {
  uint64 p;
  argaddr(0, &p);

  kphysmem_info((struct physmem_info*)p);

  return 0;
}

// 获取当前进程的缺页异常相关信息，并填充用户传来的结构体
uint64 sys_pgfault_info(void) {
  uint64 p;
  argaddr(0, &p);

  struct proc* proc = myproc();
  copyout(proc->pagetable, p, (char*)&proc->pgfaults, sizeof(proc->pgfaults));

  return 0;
}

// 获取系统信息
uint64 sys_sysinfo(void) {
  char path[MAXPATH];
  uint64 p;

  argstr(0, path, MAXPATH);
  argaddr(1, &p);

  return ksysinfo(path, (struct sysinfo*)p);
}

uint64 sys_fork_as_thread(void) {
  uint64 stack;
  uint64 stack_size;
  argaddr(0, &stack);
  argaddr(1, &stack_size);

  return kfork_as_thread((void *)stack, stack_size);
}
