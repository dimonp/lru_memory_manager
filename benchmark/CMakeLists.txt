find_package(benchmark CONFIG REQUIRED)

add_executable(lru_memory_manager_benchmark benchmark.cpp)

target_include_directories(lru_memory_manager_benchmark PRIVATE lru_memory_manager)
target_link_libraries(lru_memory_manager_benchmark PRIVATE
    benchmark::benchmark
    lru_memory_manager
)

