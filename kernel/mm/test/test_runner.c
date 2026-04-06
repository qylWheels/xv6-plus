#include <test/unity_fixture.h>

#ifdef UNIT_TEST
TEST_GROUP_RUNNER(kmalloc) {
  RUN_TEST_CASE(kmalloc, test_leading_zeros);
  RUN_TEST_CASE(kmalloc, test_ROUNDUP);
  RUN_TEST_CASE(kmalloc, test_kmalloc);
  RUN_TEST_CASE(kmalloc, test_kmfree);
}
#endif  // UNIT_TEST
