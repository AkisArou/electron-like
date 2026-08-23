cmake_minimum_required(VERSION 3.20)
project(electron_like C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(nts_web_contract INTERFACE)
target_include_directories(nts_web_contract INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(nts_counter_example OBJECT
  examples/counter/app.c
)
target_link_libraries(nts_counter_example PRIVATE nts_web_contract)

# This standalone CMake build validates only the public C contract and the
# ScriptC-shaped C fixture. The real Blink adapter is compiled inside a pinned
# Chromium GN/Ninja build, where Blink's C++ implementation types are available.
