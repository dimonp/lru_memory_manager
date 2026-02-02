find_package(Microsoft.GSL CONFIG REQUIRED)

# optimized version target
add_library(lru_memory_manager STATIC)
add_library(lrumm::lru_memory_manager ALIAS lru_memory_manager)

target_sources(lru_memory_manager
    PRIVATE
        lrumemorymanager.cpp
    PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/lru_memory_manager/lrumemorymanager.h)

target_include_directories(lru_memory_manager PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/lru_memory_manager>)
    
target_link_libraries(lru_memory_manager PRIVATE lrumm_project_options)
target_link_libraries(lru_memory_manager PUBLIC Microsoft.GSL::GSL)

install(TARGETS lru_memory_manager 
    EXPORT lru_memory_manager_targets
        FILE_SET HEADERS DESTINATION include
        LIBRARY DESTINATION lib)

install(EXPORT lru_memory_manager_targets
        FILE lru_memory_manager_targets.cmake
        NAMESPACE mmlru::
        DESTINATION cmake)