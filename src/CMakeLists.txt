add_library(lru_memory_manager
    lrumemorymanager.cpp
    lrumemorymanager.h
)

target_include_directories(lru_memory_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lru_memory_manager PROPERTIES LINKER_LANGUAGE CXX)