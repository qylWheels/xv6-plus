#include <kernel/uapi/fs/fcntl.h>
#include <kernel/uapi/mm/vm.h>

#include "user/user.h"

void thread_fork(void* arg) {
  (void)arg;
  int ret = fork();
  if (ret < 0) {
    printf("thread_fork() ok\n");
  } else {
    printf("thread_fork() failed\n");
  }
  exit(0);
}

void thread_pipe_read_write_close_wait(void* arg) {
  int* pipefd = (int*)arg;
  char* msg = "hello world";
  int ret = write(pipefd[1], msg, strlen(msg) + 1);
  if (ret != strlen(msg) + 1) {
    printf(
        "thread_pipe_read_write_close_wait() failed, write() failed, ret = "
        "%d\n",
        ret);
  }
  exit(0);
}

void thread_exec(void* arg) {
  int ret = exec("ls", (char**)0);
  if (ret < 0) {
    printf("thread_exec() ok\n");
  } else {
    printf("thread_exec() failed\n");
  }
  exit(0);
}

void thread_open(void* arg) {
  int fd = *(int*)arg;
  char* msg = "hello world";
  int ret = write(fd, msg, strlen(msg) + 1);
  if (ret != strlen(msg) + 1) {
    printf("thread_open() failed, write() failed, ret = %d\n", ret);
  }
  exit(0);
}

void thread_dup(void* arg) {
  int* fdarr = (int*)arg;
  int fd_dupped = dup(fdarr[0]);
  if (fd_dupped < 0) {
    printf("thread_dup() failed, dup() failed, ret = %d\n", fd_dupped);
    return;
  }
  fdarr[1] = fd_dupped;
  exit(0);
}

void thread_getpid(void* arg) {
  int* tid = (int*)arg;
  *tid = getpid();
  exit(0);
}

void thread_sys_sbrk(void* arg) {
  char* brk = sys_sbrk(4096, SBRK_EAGER);
  *(char**)arg = brk;
  exit(0);
}

void thread_pause_uptime(void* arg) {
  int* paused_time = (int*)arg;
  int start_time = uptime();
  pause(10);
  int end_time = uptime();
  *paused_time = end_time - start_time;
  exit(0);
}

void thread_create_thread(void* arg) {
  int* ret = (int*)arg;
  uint stack_size = 512;
  void* stack = malloc(stack_size);
  *ret = create_thread(thread_create_thread, (void*)0, stack, stack_size);
  exit(0);
}

int main(int argc, char* argv[]) {
  // 在下面的测试中将会复用这个栈
  uint stack_size = 512;
  void* stack = malloc(stack_size);
  int tid, ret, fd;
  char buf[64] = {0};
  char* msg = "hello world";

  // 测试fork()-------------------------------------------------
  tid = create_thread(thread_fork, (void*)0, stack, stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  wait(0);

  // 测试pipe()、read()、write()、close()、wait()----------------
  int pipefd[2];
  ret = pipe(pipefd);
  if (ret < 0) {
    printf("pipe() failed\n");
    return -1;
  }

  tid = create_thread(thread_pipe_read_write_close_wait, (void*)pipefd, stack,
                      stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }

  if ((ret = read(pipefd[0], buf, sizeof(buf))) != strlen(msg) + 1) {
    printf(
        "thread_pipe_read_write_close_wait() failed, read() failed, ret = %d\n",
        ret);
    return -1;
  }
  if (strcmp(buf, msg) != 0) {
    printf("thread_pipe_read_write_close_wait() failed, buf = %s\n", buf);
    return -1;
  } else {
    printf("thread_pipe_read_write_close_wait() ok\n");
  }
  close(pipefd[0]);
  close(pipefd[1]);
  wait(0);

  // 测试exec()---------------------------------------------
  tid = create_thread(thread_exec, (void*)0, stack, stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  wait(0);

  // 测试open()---------------------------------------------
  fd = open("temp", O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("thread_open() failed, open() failed\n");
    return -1;
  }

  tid = create_thread(thread_open, (void*)&fd, stack, stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  wait(0);

  // xv6没有lseek系统调用，只能关了再开
  close(fd);
  fd = open("temp", O_RDWR);
  if (fd < 0) {
    printf("thread_open() failed, open() 2 failed\n");
    return -1;
  }
  ret = read(fd, buf, sizeof(buf));
  if (ret != strlen(msg) + 1) {
    printf("thread_open() failed, read() failed, ret = %d\n", ret);
    return -1;
  }
  if (strcmp(buf, msg) != 0) {
    printf("thread_open() failed, buf = %s\n", buf);
    return -1;
  }
  close(fd);
  unlink("temp");
  printf("thread_open() ok\n");

  // 测试dup()---------------------------------------------
  fd = open("temp", O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("thread_dup() failed, open() failed\n");
    return -1;
  }
  int fdarr[2] = {fd, 0};
  tid = create_thread(thread_dup, (void*)fdarr, stack, stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  wait(0);
  ret = write(fdarr[0], msg, strlen(msg) + 1);
  if (ret < 0) {
    printf("thread_dup() failed, write() 1 failed, ret = %d\n", ret);
    return -1;
  }
  ret = write(fdarr[1], msg, strlen(msg) + 1);
  if (ret != strlen(msg) + 1) {
    printf("thread_dup() failed, write() 2 failed, ret = %d\n", ret);
    return -1;
  }
  close(fdarr[0]);
  close(fdarr[1]);

  fd = open("temp", O_RDONLY);
  if (fd < 0) {
    printf("thread_dup() failed, open() 2 failed\n");
    return -1;
  }
  ret = read(fd, buf, sizeof(buf));
  if (ret != (strlen(msg) + 1) * 2) {
    printf("thread_dup() failed, read() failed, ret = %d\n", ret);
    return -1;
  }
  if (memcmp(buf, "hello world\0hello world", (strlen(msg) + 1) * 2) != 0) {
    printf("thread_dup() failed, buf = %s\n", buf);
    return -1;
  }
  close(fd);

  unlink("temp");
  printf("thread_dup() ok\n");

  // 测试getpid()---------------------------------------------
  int pid = getpid();
  int thread_pid;
  ret = create_thread(thread_getpid, (void*)&thread_pid, stack, stack_size);
  if (ret < 0) {
    printf("failed to create_thread(): %d\n", ret);
    return -1;
  }
  wait(0);
  if (tid != pid) {
    printf("thread_getpid() ok\n");
  } else {
    printf("thread_getpid() failed, tid = %d, pid = %d\n", tid, pid);
    return -1;
  }

  // 测试sys_sbrk()---------------------------------------------
  char* prev_brk = 0;
  tid = create_thread(thread_sys_sbrk, (void*)&prev_brk, stack, stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  wait(0);
  if (prev_brk < 0) {
    printf("thread_sys_sbrk() failed, sys_sbrk() failed, prev_brk = %p\n",
           prev_brk);
    return -1;
  }
  char* current_brk = sys_sbrk(0, SBRK_EAGER);

  // 只需测试在prev_brk和current_brk之间访存是否成功
  for (volatile char* p = prev_brk; p < current_brk; p++) {
    *p = 'f';  // 防止被编译器优化
  }
  printf("thread_sys_sbrk() ok\n");

  // 测试pause()和uptime()------------------------------------------
  int thread_paused_time = 0;
  tid = create_thread(thread_pause_uptime, (void*)&thread_paused_time, stack,
                      stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  // 在等待线程执行结束前，进程就要开始计时
  int proc_start_time = uptime();
  pause(10);
  int proc_end_time = uptime();
  int proc_paused_time = proc_end_time - proc_start_time;
  wait(0);
  if (proc_paused_time == 10 && thread_paused_time == 10) {
    printf("thread_pause_uptime() ok\n");
  } else {
    printf(
        "thread_pause_uptime() failed, proc_paused_time = %d, expected 10; "
        "thread_paused_time = %d, expected 10\n",
        proc_paused_time, thread_paused_time);
    return -1;
  }

  // 测试sysinfo()---------------------------------------------
  // TODO: 懒得写了

  // 测试create_thread()和----------------------------------------
  tid = create_thread(thread_create_thread, (void*)&ret, stack, stack_size);
  if (tid < 0) {
    printf("failed to create_thread(): %d\n", tid);
    return -1;
  }
  wait(0);
  if (ret < 0) {
    printf("thread_create_thread() ok\n");
  }

  free(stack);
  return 0;
}
