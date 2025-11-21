# optimized version target
add_library(lru_memory_manager
    lrumemorymanager.cpp
    lrumemorymanager.h
)

target_include_directories(lru_memory_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lru_memory_manager PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(lru_memory_manager PRIVATE -O3)

# test version target
add_library(lru_memory_manager_t
    lrumemorymanager.cpp
    lrumemorymanager.h
)

target_include_directories(lru_memory_manager_t PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lru_memory_manager PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(lru_memory_manager_t PRIVATE -fsanitize=address -O1 -fno-omit-frame-pointer)
target_link_options(lru_memory_manager_t PRIVATE -fsanitize=address)
