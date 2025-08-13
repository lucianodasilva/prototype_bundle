cmake_minimum_required(VERSION 3.30)
project(prototype_bundle)

macro (append_source path out)
    file (
            GLOB_RECURSE
            sources
            ${path}/*.h
            ${path}/*.hpp
            ${path}/*.c
            ${path}/*.cpp
            ${path}/*.s
    )

    list (APPEND ${out} ${sources})
endmacro()

include_directories (${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_STANDARD 17)

# Test LTO
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

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

option (build_lockfree_stack_run "Build lockfree_stack long running test" ON)

option (build_disk_io_benchmark "Build disk_io_benchmark" ON)
option (build_radix_benchmark "Build radix_benchmark" ON)
option (build_row_or_column_benchmark "Build row_or_column_benchmark" ON)
option (build_spinlock_benchmark "Build spinlock_benchmark" ON)
option (build_gc_prototype_benchmark "build gc_prototype_benchmark" ON)
option (build_bitset_benchmark "build bitset_benchmark" ON)
option (build_streaming_tasks "build streaming_tasks" ON)
option (build_copy_benchmark "build copy benchmark" ON)
option (build_copy_16_block_benchmark "build copy 16 byte block benchmark" ON)
option (build_timing_benchmark "build timing benchmark" ON)
option (build_thread_safe_linked_list "build thread safe linked list benchmark" ON)
option (build_thread_local_vs_others "build thread local vs others benchmark" ON)
option (build_streaming_math "build streaming math benchmark" ON)
option (build_tracing_ptr "build tracing ptr benchmark" ON)
option (build_proto_gc "build prototype garbage collection" ON)
option (build_binalloc "build binalloc" ON)
option (build_bitmap_vs_stack "build binalloc" ON)

option (bench_profile "build for profiling" OFF)

find_package(benchmark CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(las CONFIG REQUIRED)

get_property(las_batch_file TARGET las::batch PROPERTY LOCATION)
message (STATUS "LAS-BATCH PATH: ${las_batch_file}")

add_custom_command (
    OUTPUT ${CMAKE_BINARY_DIR}/las-batch
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:las::batch> ${CMAKE_BINARY_DIR}/las-batch
    DEPENDS las::batch
    COMMENT "Copying las::batch to ${CMAKE_BINARY_DIR}"
)

if (build_disk_io_benchmark)
    add_executable(disk_io_benchmark benchmarks/disk_io_benchmark.cpp)
    target_link_libraries (disk_io_benchmark PUBLIC las::las)
endif()

if (build_radix_benchmark)
    add_executable(radix_benchmark benchmarks/radix_benchmark.cpp)
    target_link_libraries(radix_benchmark benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_row_or_column_benchmark )
    add_executable(row_or_column_benchmark benchmarks/row_or_column_benchmark.cpp)
    target_link_libraries(row_or_column_benchmark benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_spinlock_benchmark)
    add_executable(spinlock_benchmark benchmarks/spinlock_benchmark.cpp)
    target_link_libraries(spinlock_benchmark PUBLIC
            benchmark::benchmark
            benchmark::benchmark_main
            las::test)
endif()

if (build_gc_prototype_benchmark)
    add_executable(gc_prototype_benchmark benchmarks/gc_prototype.cpp)
    target_link_libraries(gc_prototype_benchmark benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_bitset_benchmark)
    add_executable(bitset benchmarks/bitset.cpp)
    target_link_libraries(bitset benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_streaming_tasks)
	add_executable (streaming_tasks benchmarks/static_ring_buffer.hpp benchmarks/streaming_tasks.cpp)

	target_include_directories (streaming_tasks PUBLIC ${CMAKE_CURRENT_LIST_DIR}/extras/glm.git)

	target_link_libraries (streaming_tasks PUBLIC benchmark::benchmark benchmark::benchmark_main glm::glm)
endif()

if (build_copy_benchmark)
	add_executable (copy_benchmark benchmarks/copy_benchmark.cpp)
	target_link_libraries (copy_benchmark PUBLIC benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_copy_16_block_benchmark)
    add_executable (copy_16_block_benchmark benchmarks/copy_16_block_benchmark.cpp)
    target_link_libraries (copy_16_block_benchmark PUBLIC benchmark::benchmark benchmark::benchmark_main las::test)
endif()


if (build_timing_benchmark) 
	add_executable (timing_benchmark benchmarks/timing_benchmark.cpp)
	target_link_libraries (timing_benchmark PUBLIC benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_thread_safe_linked_list)
    add_executable (thread_safe_linked_list benchmarks/thread_safe_linked_list.cpp)
    target_link_libraries (thread_safe_linked_list PUBLIC benchmark::benchmark benchmark::benchmark_main las::test)
endif()

if (build_thread_local_vs_others)
    add_executable (thread_local_vs_others benchmarks/thread_local_vs_others.cpp)
    target_link_libraries (thread_local_vs_others PUBLIC benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_streaming_math)
    add_executable (streaming_math benchmarks/streaming-math.cpp)
    target_link_libraries (streaming_math PUBLIC benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_tracing_ptr)
    add_executable (tracing_ptr benchmarks/tracing_ptr.cpp)
    target_link_libraries (tracing_ptr PUBLIC benchmark::benchmark benchmark::benchmark_main)
endif()

if (build_lockfree_stack_run)
    add_executable(lockfree_stack_run lockfree_stack_run.cpp)
    target_link_libraries(lockfree_stack_run PUBLIC las::test)
endif()

if (build_binalloc)
    add_executable(binalloc
            benchmarks/binalloc/basic_alloc.h
            benchmarks/binalloc/bin.cpp
            benchmarks/binalloc/bin.h
            benchmarks/binalloc/config.cpp
            benchmarks/binalloc/config.h
            benchmarks/binalloc/main.cpp
            benchmarks/binalloc/page.cpp
            benchmarks/binalloc/page.h
            benchmarks/binalloc/slab.cpp
            benchmarks/binalloc/slab.h
            benchmarks/binalloc/stack.h
            benchmarks/binalloc/utils.cpp
            benchmarks/binalloc/utils.h)

    target_link_libraries(binalloc PUBLIC benchmark::benchmark benchmark::benchmark_main las::test)

    set_target_properties(binalloc PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

endif()

if (build_bitmap_vs_stack)
    add_executable(bitmap_vs_stack
            benchmarks/bitmap_vs_atomic_stack.cpp)

    target_link_libraries(bitmap_vs_stack PUBLIC benchmark::benchmark benchmark::benchmark_main las::test)
endif()

if (bench_profile)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options($<$<CONFIG:Release>:-fno-omit-frame-pointer -g>)
    endif()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options($<$<CONFIG:Release>:-fno-omit-frame-pointer -g>)
    endif()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        add_compile_options($<$<CONFIG:Release>:/Z7> )
        list(APPEND CMAKE_CXX_LINK_FLAGS "/PROFILE")
    endif()

endif()