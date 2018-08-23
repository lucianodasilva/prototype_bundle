cmake_minimum_required(VERSION 3.9)
project(prototype_bundle)

set(CMAKE_CXX_STANDARD 14)

option (build_make_it "Build make_it" ON)

if (build_make_it)
    add_executable(make_it make_it.cpp)
endif ()

option (build_binary_search "Build binary_search" ON)

if (build_binary_search)
    add_executable(binary_search binary_search.cpp)
endif()


option (build_console_input "Build console_input" ON)

if (build_console_input)
    add_executable(console_input console_input.cpp)
endif()

option (build_radix_benchmark "Build radix_benchmark" ON)

if (build_radix_benchmark)
    set(GOOGLE_BENCHMARK_PATH "" CACHE PATH "Location of google benchmark")

    if (NOT GOOGLE_BENCHMARK_PATH)
        message (FATAL_ERROR "Missing google benchmark path")
    endif()

    # disable google benchmark testing
    set (BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "")
    set (BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "")

    add_subdirectory(${GOOGLE_BENCHMARK_PATH} ${prototype_bundle_BINARY_DIR}/benchmark)
    add_executable(radix_benchmark radix_benchmark.cpp)
    target_link_libraries(radix_benchmark benchmark benchmark_main)

endif()