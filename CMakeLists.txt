cmake_minimum_required(VERSION 3.50)

option(BUILD_TESTING "Build the project's tests" ON)
option(ENABLE_SANITIZERS "Enable all sanitizers" ON)  

if(BUILD_TESTING)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED True) 
endif()

project(lru_memory_manager VERSION 0.1 LANGUAGES CXX)

add_library(lrumm_project_options INTERFACE IMPORTED)

if (ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(lrumm_project_options INTERFACE /fsanitize=address /Zi /INCREMENTAL:NO)
    target_link_options(lrumm_project_options INTERFACE /INCREMENTAL:NO)
    target_link_options(lrumm_project_options INTERFACE /INCREMENTAL:NO)
  else()
    target_compile_options(lrumm_project_options INTERFACE -fsanitize=address,undefined,leak)
    target_link_options(lrumm_project_options INTERFACE -fsanitize=address,undefined,leak)
  endif()
endif()

find_program(CLANGTIDY clang-tidy)
if(CLANGTIDY)
  set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
endif()

add_subdirectory(src)

if(BUILD_TESTING)
  add_subdirectory(benchmark)
  include(CTest)
  add_subdirectory(test)
endif()
