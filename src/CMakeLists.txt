# optimized version target
add_library(lru_memory_manager
    lrumemorymanager.cpp
    lrumemorymanager.h
)

target_include_directories(lru_memory_manager PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/lru_memory_manager>
)
set_target_properties(lru_memory_manager PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(lru_memory_manager PRIVATE -O3)

# test version target
add_library(lru_memory_manager_t
    lrumemorymanager.cpp
    lrumemorymanager.h
)

target_include_directories(lru_memory_manager_t PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/lru_memory_manager>
)
set_target_properties(lru_memory_manager PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(lru_memory_manager_t PRIVATE -fsanitize=address -O1 -fno-omit-frame-pointer)
target_link_options(lru_memory_manager_t PRIVATE -fsanitize=address)

install(TARGETS lru_memory_manager EXPORT lru_memory_manager_targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/lrumemorymanager.h"
        DESTINATION include/lru_memory_manager)

install(EXPORT lru_memory_manager_targets
        FILE lru_memory_manager_targets.cmake
        NAMESPACE mmlru::
        DESTINATION cmake)