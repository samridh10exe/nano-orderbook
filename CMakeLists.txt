cmake_minimum_required(VERSION 3.20)
project(order_book CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")

# Header-only library
add_library(orderbook INTERFACE)
target_include_directories(orderbook INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

# Tests
add_executable(tests tests/correctness.cpp)
target_link_libraries(tests orderbook)

# Benchmarks
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark orderbook)

add_executable(baseline benchmarks/baseline.cpp)
target_link_libraries(baseline orderbook)

add_executable(stress benchmarks/stress.cpp)
target_link_libraries(stress orderbook)

# Optional: clang-tidy
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    set_target_properties(tests benchmark baseline stress PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY}"
    )
endif()
