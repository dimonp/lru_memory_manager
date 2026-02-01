cmake_minimum_required(VERSION 3.50)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED True) 
endif()

project(lru_memory_manager VERSION 0.1 LANGUAGES CXX)

option(BUILD_TESTING "Build the project's tests" ON)
option(DISABLE_SANITIZERS "Enable all sanitizers" OFF)  

add_library(lrumm_project_options INTERFACE IMPORTED)


if (DISABLE_SANITIZERS)
  if(MSVC)
    set(LIST_OF_SANITIZERS "address")
    target_compile_options(lrumm_project_options INTERFACE /fsanitize=${LIST_OF_SANITIZERS} /Zi /INCREMENTAL:NO)
    target_link_options(lrumm_project_options INTERFACE /INCREMENTAL:NO)
  else()
    set(LIST_OF_SANITIZERS "address leak undefined")
    target_compile_options(lrumm_project_options INTERFACE -fsanitize=${LIST_OF_SANITIZERS})
    target_link_options(lrumm_project_options INTERFACE -fsanitize=${LIST_OF_SANITIZERS})
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
