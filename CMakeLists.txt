cmake_minimum_required(VERSION 3.50)

option(CMAKE_VERBOSE_MAKEFILE "Build the project's tests" ON)
option(LRUMM_BUILD_TESTING "Build the project's tests" ON)
option(LRUMM_ENABLE_SANITIZERS "Enable all sanitizers" ON)
option(LRUMM_ENABLE_EXCEPTIONS "Enable exceptions" OFF)

if(LRUMM_BUILD_TESTING)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED True)
endif()

project(lru_memory_manager VERSION 0.1.1 LANGUAGES CXX)

add_library(lrumm_project_options INTERFACE IMPORTED)

if (LRUMM_ENABLE_SANITIZERS)
  if(MSVC)
    target_compile_options(lrumm_project_options INTERFACE /fsanitize=address /Zi /INCREMENTAL:NO)
    target_link_options(lrumm_project_options INTERFACE /INCREMENTAL:NO)
  else()
    target_compile_options(lrumm_project_options INTERFACE -fsanitize=address,undefined,leak)
    target_link_options(lrumm_project_options INTERFACE -fsanitize=address,undefined,leak)
  endif()
endif()

if (LRUMM_ENABLE_EXCEPTIONS)
  if(MSVC)
    target_compile_options(lrumm_project_options INTERFACE /EHsc-)
    target_compile_options(lrumm_project_options INTERFACE /EHs-)
    target_compile_definitions(lrumm_project_options INTERFACE _HAS_EXCEPTIONS=0)
  else()
    target_compile_options(lrumm_project_options INTERFACE -fno-exceptions)
  endif()
endif()

find_program(CLANGTIDY_FOUND clang-tidy)
if(CLANGTIDY_FOUND)
  set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
endif()

add_subdirectory(src)

if(LRUMM_BUILD_TESTING)
  enable_testing()
  add_subdirectory(benchmark)
  include(CTest)
  add_subdirectory(test)
endif()
