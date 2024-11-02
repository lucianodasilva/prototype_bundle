cmake_minimum_required(VERSION 3.9)
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

option (build_batch_tester "Build batch testing tool" ON)

option (build_lockfree_stack_run "Build lockfree_stack long running test" ON)
option (build_pool_allocator_run "Build pool_allocator long running test" ON)

option (build_radix_benchmark "Build radix_benchmark" ON)
option (build_row_or_column_benchmark "Build row_or_column_benchmark" ON)
option (build_spinlock_benchmark "Build spinlock_benchmark" ON)
option (build_gc_prototype_benchmark "build gc_prototype_benchmark" ON)
option (build_bitset_benchmark "build bitset_benchmark" ON)
option (build_streaming_tasks "build streaming_tasks" ON)
option (build_copy_benchmark "build copy benchmark" ON)
option (build_timing_benchmark "build timing benchmark" ON)
option (build_thread_safe_linked_list "build thread safe linked list benchmark" ON)
option (build_thread_local_vs_others "build thread local vs others benchmark" ON)
option (build_streaming_math "build streaming math benchmark" ON)
option (build_tracing_ptr "build tracing ptr benchmark" ON)

find_package(benchmark CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(las CONFIG REQUIRED)

if (build_batch_tester)
    add_executable(batch_tester batch_tester.cpp)
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

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        target_compile_options(streaming_tasks PUBLIC
                 $<$<CONFIG:Release>:-fno-omit-frame-pointer -g>)
    endif()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        message ("Enabling thread sanitizer for Release build")

        target_compile_options(streaming_tasks PUBLIC
                $<$<CONFIG:Release>:-fno-omit-frame-pointer -g>)
    endif()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        target_compile_options(streaming_tasks PUBLIC
                $<$<CONFIG:Release>:/Z7> )

        set_target_properties (streaming_tasks PROPERTIES LINK_FLAGS "/PROFILE")
    endif()
endif()

if (build_copy_benchmark)
	add_executable (copy_benchmark benchmarks/copy_benchmark.cpp)
	target_link_libraries (copy_benchmark PUBLIC benchmark::benchmark benchmark::benchmark_main)
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

if (build_pool_allocator_run)
    add_executable(pool_allocator_run pool_allocator.h pool_allocator_run.cpp)
    target_link_libraries(pool_allocator_run PUBLIC las::test)
endif()