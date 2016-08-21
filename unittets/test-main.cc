#include <iostream>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if GTEST_OS_WINDOWS_MOBILE
# include <tchar.h>  // NOLINT
GTEST_API_ int _tmain(int argc, TCHAR** argv)
{
#else
GTEST_API_ int main(int argc, char** argv)
{
#endif  // GTEST_OS_WINDOWS_MOBILE
    std::cout << "Running main() from gmock_main.cc\n";
    // Since Google Mock depends on Google Test, InitGoogleMock() is
    // also responsible for initializing Google Test.  Therefore there's
    // no need for calling testing::InitGoogleTest() separately.
    testing::InitGoogleMock(&argc, argv);

    //::testing::GTEST_FLAG(filter) = "GLOBAL_MODULE.*";

    //::testing::GTEST_FLAG(filter) = "TIMER_MODULE.*";
   // ::testing::GTEST_FLAG(filter) = "TIMER_MODULE.test_operations_on_time";

    //::testing::GTEST_FLAG(filter) = "MALLOC_MODULE.test_alloc_dealloc";

    //::testing::GTEST_FLAG(filter) = "AUTH_MODULE.*";

    ::testing::GTEST_FLAG(filter) = "DISPATCHER_MODULE.test_contains_chunk";
    //::testing::GTEST_FLAG(filter) = "DISPATCHER_MODULE.test_find_geco_instance_by_transport_addr";
    return RUN_ALL_TESTS();
}
