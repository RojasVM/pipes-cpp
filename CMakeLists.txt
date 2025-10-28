
cmake_minimum_required(VERSION 3.16)
project(pipes_cpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(pipes src/main.cpp)
if (WIN32)
  target_compile_definitions(pipes PRIVATE UNICODE)
endif()
