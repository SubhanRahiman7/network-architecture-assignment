cmake_minimum_required(VERSION 3.16)
project(persistent_link LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(host src/host.cpp)
add_executable(cli src/cli.cpp)
target_include_directories(host PRIVATE src)
target_include_directories(cli PRIVATE src)

if (WIN32)
  target_link_libraries(host PRIVATE ws2_32)
  target_link_libraries(cli PRIVATE ws2_32)
endif()
