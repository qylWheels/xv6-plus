#include "user/user.h"

void test_thread(int tid) {
  if (tid == 0) {
    printf("^");
  } else if (tid > 0) {
    printf("#");
  }
}

int main(int argc, char* argv[]) {
  void* stack = malloc(2048);
  int tid = fork_as_thread(stack, 2048);
  if (tid < 0) {
    printf("failed to fork_as_thread()\n");
  }
  for (int i = 0; i < 5; i++) {
    test_thread(tid);
  }

  // FIXME: 这样才能正常运行，检查原因
  if (tid > 0) {
    wait(0);
  } else {
    exit(0);
  }
  return 0;
}
