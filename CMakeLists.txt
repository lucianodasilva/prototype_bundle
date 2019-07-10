cmake_minimum_required(VERSION 3.9)
project(prototype_bundle)

set(CMAKE_CXX_STANDARD 14)

option (build_make_it "Build make_it" OFF)

if (build_make_it)
    add_executable(make_it make_it.cpp)
endif ()

option (build_binary_search "Build binary_search" OFF)

if (build_binary_search)
    add_executable(binary_search binary_search.cpp)
endif()

option (build_console_input "Build console_input" OFF)

if (build_console_input)
    add_executable(console_input console_input.cpp)
endif()

option (build_radix_benchmark "Build radix_benchmark" OFF)
option (build_dispatch_benchmark "Build dispatch_benchmark" OFF)
option (build_row_or_column_benchmark "Build row_or_column_benchmark" OFF)
option (build_gc_prototype_benchmark "build gc_prototype_benchmark" OFF)
option (build_bbtree_benchmark "build bbtree_benchmark" OFF)
option (build_streamming_tasks "build streamming_tasks" ON)
option (build_copy_benchmark "build copy benchmark" ON)
option (build_timing_benchmark "build timing benchmark" ON)

# disable google benchmark testing
set (BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "")
set (BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "")

set (GOOGLE_BENCHMARK_PATH ${CMAKE_CURRENT_LIST_DIR}/extras/benchmark.git/)
add_subdirectory(${GOOGLE_BENCHMARK_PATH} ${prototype_bundle_BINARY_DIR}/benchmark)

if (build_radix_benchmark)

    add_executable(radix_benchmark radix_benchmark.cpp)
    target_link_libraries(radix_benchmark benchmark benchmark_main)

endif()

if (build_dispatch_benchmark )

    add_executable(dispatch_benchmark dispatch_benchmark.cpp)
    target_link_libraries(dispatch_benchmark benchmark benchmark_main)

endif()

if (build_row_or_column_benchmark )

    add_executable(row_or_column_benchmark row_or_column_benchmark.cpp)
    target_link_libraries(row_or_column_benchmark benchmark benchmark_main)

endif()

if (build_gc_prototype_benchmark)

    add_executable(gc_prototype_benchmark gc_prototype.cpp)
    target_link_libraries(gc_prototype_benchmark benchmark benchmark_main)

endif()

if (build_bbtree_benchmark)

    add_executable(bbtree bbtree.cpp)
    target_link_libraries(bbtree benchmark_main)

endif()

if (build_streamming_tasks)
	add_executable (streamming_tasks static_ring_buffer.hpp streamming_tasks.cpp)

	set (GLM_QUIET ON CACHE BOOL "")
	set (BUILD_SHARED_LIBS OFF CACHE BOOL "")
	set (BUILD_STATIC_LIBS ON CACHE BOOL "")
	add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/extras/glm.git/glm)

	target_include_directories (streamming_tasks PUBLIC ${CMAKE_CURRENT_LIST_DIR}/extras/glm.git)

	target_link_libraries (streamming_tasks PUBLIC benchmark_main glm_static)


endif()

if (build_copy_benchmark)
	add_executable (copy_benchmark copy_benchmark.cpp)
	target_link_libraries (copy_benchmark PUBLIC benchmark_main)
endif()

if (build_timing_benchmark) 
	add_executable (timing_benchmark timing_benchmark.cpp)
	target_link_libraries (timing_benchmark PUBLIC benchmark_main)
endif()