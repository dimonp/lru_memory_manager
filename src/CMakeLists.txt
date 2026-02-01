find_package(Microsoft.GSL CONFIG REQUIRED)

# optimized version target
add_library(lru_memory_manager
    lrumemorymanager.cpp
    lrumemorymanager.h
)
add_library(lrumm::lru_memory_manager ALIAS lru_memory_manager)

target_include_directories(lru_memory_manager PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/lru_memory_manager>
)
target_link_libraries(lru_memory_manager PRIVATE lrumm_project_options)
target_link_libraries(lru_memory_manager PRIVATE Microsoft.GSL::GSL)

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