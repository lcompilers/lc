cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(lc LANGUAGES C CXX)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release
        CACHE STRING "Build type (Debug, Release)" FORCE)
endif ()
if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR
        CMAKE_BUILD_TYPE STREQUAL "Release"))
    message("${CMAKE_BUILD_TYPE}")
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of: Debug, Release (current value: '${CMAKE_BUILD_TYPE}')")
endif ()

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17
        CACHE STRING "C++ standard" FORCE)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# LLVM
set(WITH_LLVM no CACHE BOOL "Build with LLVM support")
set(WITH_TARGET_AARCH64 no CACHE BOOL "Enable target AARCH64")
set(WITH_TARGET_X86 no CACHE BOOL "Enable target X86")
set(WITH_TARGET_WASM no CACHE BOOL "Enable target WebAssembly")
if (WITH_LLVM)
    set(LFORTRAN_LLVM_COMPONENTS core support mcjit orcjit native asmparser asmprinter)
    find_package(LLVM REQUIRED)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    # Always enable the native target
    if ("${LLVM_NATIVE_ARCH}" STREQUAL "AArch64")
        set(WITH_TARGET_AARCH64 yes)
    endif()

    if ("${LLVM_NATIVE_ARCH}" STREQUAL "X86")
        set(WITH_TARGET_X86 yes)
    endif()

    if (WITH_TARGET_AARCH64)
        if (NOT ("${LLVM_TARGETS_TO_BUILD}" MATCHES "AArch64"))
            message(FATAL_ERROR "The selected LLVM library doesn't have support for AArch64 targets")
        endif()

        list(APPEND LFORTRAN_LLVM_COMPONENTS aarch64info aarch64utils aarch64desc aarch64asmparser aarch64codegen aarch64disassembler)
        add_definitions("-DHAVE_TARGET_AARCH64=1")
    endif()

    if (WITH_TARGET_X86)
        if (NOT ("${LLVM_TARGETS_TO_BUILD}" MATCHES "X86"))
            message(FATAL_ERROR "The selected LLVM library doesn't have support for X86 targets")
        endif()

        list(APPEND LFORTRAN_LLVM_COMPONENTS x86info x86desc x86codegen x86asmparser x86disassembler)
        add_definitions("-DHAVE_TARGET_X86=1")
    endif()

    if (WITH_TARGET_WASM)
        if (NOT ("${LLVM_TARGETS_TO_BUILD}" MATCHES "WebAssembly"))
            message(FATAL_ERROR "The selected LLVM library doesn't have support for WebAssembly targets")
        endif()

        list(APPEND LFORTRAN_LLVM_COMPONENTS webassemblyasmparser webassemblycodegen webassemblydesc webassemblydisassembler webassemblyinfo)
        add_definitions("-DHAVE_TARGET_WASM=1")
    endif()

    # Clang dependencies
    list(APPEND LFORTRAN_LLVM_COMPONENTS windowsdriver)

    llvm_map_components_to_libnames(llvm_libs ${LFORTRAN_LLVM_COMPONENTS})
    unset(LFORTRAN_LLVM_COMPONENTS)

    add_library(p::llvm INTERFACE IMPORTED)
    set_property(TARGET p::llvm PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        ${LLVM_INCLUDE_DIRS})
    #set_property(TARGET p::llvm PROPERTY INTERFACE_COMPILE_DEFINITIONS
    #    ${LLVM_DEFINITIONS})
    #set_property(TARGET p::llvm PROPERTY INTERFACE_COMPILE_OPTIONS
    #    ${LLVM_DEFINITIONS})
    set_property(TARGET p::llvm PROPERTY INTERFACE_COMPILE_OPTIONS
        $<$<COMPILE_LANGUAGE:CXX>:${LFORTRAN_CXX_NO_RTTI_FLAG}>)
    set_property(TARGET p::llvm PROPERTY INTERFACE_LINK_LIBRARIES
        ${llvm_libs})
    if (MSVC)
        # LLVM on Windows appends zlib shared library and we must provide
        # a path to find it:
        get_filename_component(mypath ${ZLIB_LIBRARY} DIRECTORY)
        target_link_directories(p::llvm BEFORE INTERFACE ${mypath})
        message(STATUS "ZLIB LIBRARY PATH: ${mypath}")
    endif()
    set(HAVE_LFORTRAN_LLVM yes)

    find_package(clang 15 REQUIRED)
    add_library(p::clang INTERFACE IMPORTED)
    set_property(TARGET p::clang PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        ${CLANG_INCLUDE_DIRS})
    set_property(TARGET p::clang PROPERTY INTERFACE_LINK_LIBRARIES
        ${CLANG_LIBRARIES})
endif()

enable_testing()

message("\n")
message("Configuration results")
message("---------------------")
message("C compiler      : ${CMAKE_C_COMPILER}")
message("C++ compiler    : ${CMAKE_CXX_COMPILER}")
message("Build type: ${CMAKE_BUILD_TYPE}")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("C compiler flags      : ${CMAKE_C_FLAGS_DEBUG}")
    message("C++ compiler flags    : ${CMAKE_CXX_FLAGS_DEBUG}")
else ()
    message("C compiler flags      : ${CMAKE_C_FLAGS_RELEASE}")
    message("C++ compiler flags    : ${CMAKE_CXX_FLAGS_RELEASE}")
endif ()
message("Installation prefix: ${CMAKE_INSTALL_PREFIX}")
message("WITH_LLVM: ${WITH_LLVM}")
message("WITH_TARGET_AARCH64: ${WITH_TARGET_AARCH64}")
message("WITH_TARGET_X86: ${WITH_TARGET_X86}")
message("WITH_TARGET_WASM: ${WITH_TARGET_WASM}")


add_subdirectory(src)
add_subdirectory(libasr/src/libasr)
