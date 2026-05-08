#include "user/user.h"

void test_thread(int tid) {
  if (tid == 0) {
    printf("this is thread(lightweight child process)\n");
  } else if (tid > 0) {
    printf("this is parent process\n");
  }
}

int main(int argc, char* argv[]) {
  void* stack = malloc(1024);
  int tid = fork_as_thread(stack, 1024);
  if (tid < 0) {
    printf("failed to fork_as_thread()\n");
  }
  for (int i = 0; i < 10; i++) {
    test_thread(tid);
  }
  while (1);
}
