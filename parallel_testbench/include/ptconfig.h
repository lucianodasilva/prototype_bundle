#pragma once
#ifndef PTCONFIG_H
#define PTCONFIG_H

// Detect operating system
#if defined (__linux__) && !defined(__ANDROID__)
#	define PT_OS_GNU_LINUX 1
#elif defined (__ANDROID__)
#	define PT_OS_ANDROID 1
#elif defined (__APPLE__)
#	define PT_OS_DARWIN 1
#elif defined (_WIN32)
#	define PT_OS_WINDOWS 1
#endif

// Detect compiler
#if defined (__GNUC__)
#	define PT_COMPILER_GCC 1
#elif defined (_MSC_VER)
#	define PT_COMPILER_MSVC 1
#elif defined (__clang__)
#	define PT_COMPILER_CLANG 1
#endif

// Detect architecture
#if defined (__x86_64__) || defined (_M_X64)
#	define PT_ARCH_X64 1
#elif defined (__i386) || defined (_M_IX86)
#	define PT_ARCH_X86 1
#elif defined (__arm__) || defined (_M_ARM)
#	define PT_ARCH_ARM 1
#elif defined (__aarch64__) || defined (_M_ARM64)
#	define PT_ARCH_ARM64 1
#endif

// Detect endianness
#if defined (__BYTE_ORDER__) && defined (__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#       define PT_LITTLE_ENDIAN 1
#elif  defined (__BYTE_ORDER__) && defined (__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#       define PT_BIG_ENDIAN 1
#elif defined (PT_OS_WINDOWS)
#       define PT_LITTLE_ENDIAN 1
#endif

namespace ptbench {
    enum struct os {
        unknown = 0,
        windows,
        gnu_linux,
        android,
        darwin,
#if defined (PT_OS_GNU_LINUX)
        native = gnu_linux,
#elif defined (PT_OS_ANDROID)
        native = android,
#elif defined (PT_OS_DARWIN)
        native = darwin,
#elif defined (PT_OS_WINDOWS)
        native = windows,
#else
        native = unknown,
#endif
    };

    enum struct compiler {
        unknown = 0,
        gcc,
        msvc,
        clang,
#if defined (PT_COMPILER_GCC)
        native = gcc,
#elif defined (PT_COMPILER_MSVC)
        native = msvc,
#elif defined (PT_COMPILER_CLANG)
        native = clang,
#else
        native = unknown
#endif
    };

    enum struct arch {
        unknown = 0,
        x86,
        x64,
        arm,
        arm64,
#if defined (PT_ARCH_X86)
        native = x86,
#elif defined (PT_ARCH_X64)
        native = x64,
#elif defined (PT_ARCH_ARM)
        native = arm,
#elif defined (PT_ARCH_ARM64)
        native = arm64
#else
        native = unknown
#endif
    };

    enum struct endian {
        unknown = 0,
        big,
        little,
#if defined (PT_BIG_ENDIAN)
        native = big,
#elif defined (PT_LITTLE_ENDIAN)
        native = little,
#else
        native = unknown,
#endif
    };
}

#endif //PTCONFIG_H
