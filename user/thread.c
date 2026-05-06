#include "user/user.h"

int main(int argc, char* argv[]) {
  void* stack = malloc(1024);
  int tid = fork_as_thread(stack, 1024);
  if (tid == 0) {
    printf("this is thread(lightweight child process)\n");
  } else if (tid > 0) {
    printf("this is parent process\n");
  } else {
    printf("failed to fork_as_thread()\n");
  }
}
