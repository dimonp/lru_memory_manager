cmake_minimum_required(VERSION 3.50)
project(lru_memory_manager VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Address Sanitizer flags for the all targets
# add_compile_options(-fsanitize=address -O1 -fno-omit-frame-pointer)
# add_link_options(-fsanitize=address)

add_subdirectory(src)
add_subdirectory(benchmark)

option(BUILD_TESTING "Build the project's tests" ON)
if(BUILD_TESTING)
  include(CTest)
  add_subdirectory(test)
endif()
