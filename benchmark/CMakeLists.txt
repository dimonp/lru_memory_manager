find_package(benchmark CONFIG REQUIRED)

add_executable(lru_memory_manager_benchmark benchmark.cpp)

target_link_libraries(lru_memory_manager_benchmark PRIVATE lrumm_project_options)
target_link_libraries(lru_memory_manager_benchmark PRIVATE
    benchmark::benchmark
    lru_memory_manager
)


