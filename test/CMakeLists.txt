find_package(GTest CONFIG REQUIRED) # Use CONFIG for modern CMake

add_executable(lru_memory_manager_test lrumemorymanager_test.cpp)
target_include_directories(lru_memory_manager_test PRIVATE lru_memory_manager)
target_link_libraries(lru_memory_manager_test PRIVATE
    GTest::gtest_main
    GTest::gmock_main
    lru_memory_manager
)

include(GoogleTest)
gtest_discover_tests(lru_memory_manager_test)