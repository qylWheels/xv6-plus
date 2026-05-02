#include "user/user.h"

int main(int argc, char* argv[]) {
  printf("ret=%d\n", fork_as_thread());
}
