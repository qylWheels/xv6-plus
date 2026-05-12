#include <core/exec.h>
#include <core/param.h>
#include <core/proc.h>
#include <core/riscv.h>
#include <core/swtch.h>
#include <core/trap.h>
#include <core/types.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <fs/log.h>
#include <mm/kalloc.h>
#include <mm/memlayout.h>
#include <mm/vm.h>
#include <sync/spinlock.h>
#include <trace/events/core/trap.h>
#include <utils/misc.h>
#include <utils/printf.h>
#include <utils/string.h>

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc* initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc* p);
void procdump(void);
void procdump_one(struct proc* p);

extern char trampoline[];  // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

void volun_switch_probe(void) {
  struct proc* p = myproc();

  // 无需上锁，因为该函数已被包裹在acquire(&p->lock)里了
  p->volun_switches += 1;
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl) {
  struct proc* p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char* pa = kalloc();
    if (pa == 0) panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void) {
  // 注册tracepoint
  reg_trace_volun_switch_probe(volun_switch_probe);

  struct proc* p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu* mycpu(void) {
  int id = cpuid();
  struct cpu* c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc* myproc(void) {
  push_off();
  struct cpu* c = mycpu();
  struct proc* p = c->proc;
  pop_off();
  return p;
}

int allocpid() {
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc* allocproc(void) {
  struct proc* p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->pgfaults = 0;
  p->ticks = 0;
  p->volun_switches = 0;
  p->involun_switches = 0;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe*)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// 释放线程资源
// 线程p必须持有p->lock
static void freethread(struct proc* p) {
  if (p->trapframe) kfree((void*)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable) {
    uvmunmap(p->pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(p->pagetable, TRAPFRAME, 1, 0);
    // 用户态内存不能被释放，父进程还在使用
    // 只能取消其对应的映射和释放页表项
    if (!p->psz) {
      panic("freethread: psz is NULL");
    }
    if (*(p->psz) == 0) {
      panic("freethread: *psz is 0");
    }
    uvmunmap(p->pagetable, 0, PGROUNDUP(*p->psz) / PGSIZE, 0);
    freewalk(p->pagetable);
  }
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->pgfaults = 0;
  p->ticks = 0;
  p->volun_switches = 0;
  p->involun_switches = 0;
}

void freethreads_in_proc(struct proc* p) {
  for (struct proc* pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p && pp->lwp) {
      freethread(pp);
    }
  }
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void freeproc(struct proc* p) {
  // 释放所有线程资源
  freethreads_in_proc(p);

  if (p->trapframe) kfree((void*)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable) proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->pgfaults = 0;
  p->ticks = 0;
  p->volun_switches = 0;
  p->involun_switches = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t proc_pagetable(struct proc* p) {
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0) return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void userinit(void) {
  struct proc* p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
  uint64 sz;
  struct proc* p = myproc();

  // 若是线程，则先找到其所属的进程，再增长内存空间
  if (p->lwp) {
    while (p->lwp) {
      p = p->parent;
    }
  }

  sz = p->sz;

  if (n > 0) {
    if (sz + n > TRAPFRAME) {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int kfork(void) {
  int i, pid;
  struct proc* np;
  struct proc* p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }
  np->ustack = p->ustack;

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->lwp = 0;
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i]) np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// stack指针是用户态的指针，且指向栈顶（低地址）有有效数据的那个字节
// XXX:
// 不能仿照fork()，即使复制了父进程的用户栈。因为父进程栈帧中的s0(即fp)复制到子线程后，
// 子线程在某个函数中返回时会试图返回到父进程的栈帧，这显然是错误的。因此正确的实现应该是
// 由用户提供start函数，该函数会在子线程中被调用，且arg参数会被传递给start函数
#define ENOPROC 1      // 没有空闲的PCB
#define ESTACKSIZE 2   // 用户提供的stack_size太小，不足以拷贝父进程用户栈的内容
#define EINVALSTACK 3  // 用户提供的stack指针指向的栈地址范围无效
int kcreate_thread(void (*start)(void* arg), void* arg, void* stack,
                   uint64 stack_size) {
  int i, pid;
  struct proc* np;
  struct proc* p = myproc();

  if ((uint64)stack >= p->sz || (uint64)stack + stack_size > p->sz) {
    return -EINVALSTACK;
  }

  // 分配PCB
  if ((np = allocproc()) == 0) {
    return -ENOPROC;
  }

  // 标记np为lwp
  np->lwp = 1;

  // allocproc()已经帮我们把内核所需的空间（尤其是trapframe）配置好了
  // 所以不用uvmcopy()来拷贝全量数据，只需拷贝除了trampoline和trapframe以外的其他页表项
  // 严格来说只需拷贝虚拟地址[0, p->sz)所对应的页表项
  if (uvmcopy_shallow(p->pagetable, np->pagetable, 0, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  // 线程的sz是没用的，只需将psz指向父进程的sz
  np->psz = &p->sz;

  // 设置线程用户栈指针
  np->ustack = (uint64)stack + stack_size;

  // 传递arg参数
  uint64 ustack = (uint64)np->ustack;
  copyout(np->pagetable, ustack - 4, (char*)&arg, sizeof arg);

  // 拷贝trapframe，但是把sp设为用户提供给的栈，epc设为start函数的地址
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->sp = (uint64)stack + stack_size - sizeof arg;
  np->trapframe->epc = (uint64)start;

  // 子线程返回0
  np->trapframe->a0 = 0;

  // 增加fd的引用计数
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i]) np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc* p) {
  struct proc* pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    // 只把进程而非线程交由initproc管理
    if (pp->parent == p && !pp->lwp) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void kexit(int status) {
  struct proc* p = myproc();

  if (p == initproc) panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file* f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int kwait(uint64 addr) {
  struct proc* pp;
  int havekids, pid;
  struct proc* p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char*)&pp->xstate,
                                   sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }

          // 执行真正的释放子进程/线程资源的操作
          // printf("p->name = %s, pp->name = %s, pp->lwp = %d\n", p->name,
          //        pp->name, pp->lwp);
          if (!pp->lwp) {
            freeproc(pp);
          } else {
            freethread(pp);
          }

          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  // DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void) {
  struct proc* p;
  struct cpu* c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if (found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
  int intena;
  struct proc* p = myproc();

  if (!holding(&p->lock)) panic("sched p->lock");
  if (mycpu()->noff != 1) panic("sched locks");
  if (p->state == RUNNING) panic("sched RUNNING");
  if (intr_get()) panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) {
  struct proc* p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void) {
  extern char userret[];
  static int first = 1;
  struct proc* p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char*[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void* chan, struct spinlock* lk) {
  struct proc* p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  // DOC: sleeplock1
  release(lk);

  trace_volun_switch();

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void wakeup(void* chan) {
  struct proc* p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kkill(int pid) {
  struct proc* p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc* p) {
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc* p) {
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void* src, uint64 len) {
  struct proc* p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char*)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void* dst, int user_src, uint64 src, uint64 len) {
  struct proc* p = myproc();
  if (user_src) {
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 打印一个进程的PCB
void procdump_one(struct proc* p) {
  printf("lwp: %d\n", p->lwp);
  printf("state: %d\n", p->state);
  printf("killed: %d\n", p->killed);
  printf("xstate: %d\n", p->xstate);
  printf("pid: %d\n", p->pid);
  printf("parent: %p\n", p->parent);
  printf("kstack: %ld\n", p->kstack);
  printf("ustack: %ld\n", p->ustack);
  printf("sz: %ld\n", p->sz);
  printf("psz: %p\n", p->psz);
  printf("pagetable: %p\n", p->pagetable);
  printf("trapframe: %p\n", p->trapframe);
  printf("name: %s\n", p->name);
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
  static char* states[] = {[UNUSED] "unused",   [USED] "used",
                           [SLEEPING] "sleep ", [RUNNABLE] "runble",
                           [RUNNING] "run   ",  [ZOMBIE] "zombie"};
  struct proc* p;
  char* state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED) continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("[%s] %d %s %s", p->lwp ? "thrd" : "proc", p->pid, state, p->name);
    printf("\n");
  }
}

struct proc* get_proc_of_thread(struct proc* p) {
  /* 若p为进程，则返回其自身 */
  if (!p->lwp) {
    return p;
  }

  struct proc* search = p->parent;
  while (search->lwp) {
    search = search->parent;
  }
  return search;
}
