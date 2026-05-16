#include <kernel/uapi/fs/fcntl.h>

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

  free(stack);
  return 0;
}
