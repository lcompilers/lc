cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(lc C CXX)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug
        CACHE STRING "Build type (Debug, Release)" FORCE)
endif ()
if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR
        CMAKE_BUILD_TYPE STREQUAL "Release"))
    message("${CMAKE_BUILD_TYPE}")
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of: Debug, Release (current value: '${CMAKE_BUILD_TYPE}')")
endif ()

find_program(LC NAMES lc)

set(LC_RTL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/runtime)
set(LC_RTL_HEADER_DIR ${LC_RTL_DIR}/impure)

find_path(LC_RTLIB_DIR lfortran_intrinsics.h
    HINTS ${LC_RTL_HEADER_DIR})
find_library(LC_RTLIB_LIBRARY lc_runtime_static
    HINTS ${LC_RTL_DIR})
add_library(lc_runtime INTERFACE IMPORTED)
set_property(TARGET lc_runtime PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    ${LC_RTLIB_DIR})
set_property(TARGET lc_runtime PROPERTY INTERFACE_LINK_LIBRARIES
    ${LC_RTLIB_LIBRARY})
target_link_libraries(lc_runtime INTERFACE m)

set(LC_BACKEND no CACHE STRING "Only compile the LC subset for the given backend")
set(FAST no CACHE BOOL "Run supported tests with --fast")

enable_testing()

message("\n")
message("Configuration results")
message("---------------------")
message("Fortran compiler: ${CMAKE_Fortran_COMPILER}")
message("C compiler      : ${CMAKE_C_COMPILER}")
message("Build type: ${CMAKE_BUILD_TYPE}")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Fortran compiler flags: ${CMAKE_Fortran_FLAGS_DEBUG}")
    message("C compiler flags      : ${CMAKE_C_FLAGS_DEBUG}")
else ()
    message("Fortran compiler flags: ${CMAKE_Fortran_FLAGS_RELEASE}")
    message("C compiler flags      : ${CMAKE_C_FLAGS_RELEASE}")
endif ()
message("Installation prefix: ${CMAKE_INSTALL_PREFIX}")
message("LC_BACKEND: ${LC_BACKEND}")
message("FAST: ${FAST}")


macro(RUN_UTIL RUN_FAIL RUN_NAME RUN_FILE_NAME RUN_LABELS RUN_EXTRAFILES RUN_EXTRA_ARGS RUN_COPY_TO_BIN RUN_GCC_ARGS)
    set(fail ${${RUN_FAIL}})
    set(name ${${RUN_NAME}})
    set(file_name ${${RUN_FILE_NAME}})
    set(labels ${${RUN_LABELS}})
    set(extra_files ${${RUN_EXTRAFILES}})
    set(extra_args ${${RUN_EXTRA_ARGS}})
    set(copy_to_bin ${${RUN_COPY_TO_BIN}})
    set(gcc_args ${${RUN_GCC_ARGS}})

    if (NOT name)
        message(FATAL_ERROR "Must specify the NAME argument")
    endif()

    if (LC_BACKEND)
        if (${LC_BACKEND} IN_LIST labels)
            # Test is supported by the given LC backend
            set(ADD_TEST ON)
        else()
            # Test is not supported by the given LC backend
            set(ADD_TEST OFF)
        endif()
    else()
        # GCC
        if ("gcc" IN_LIST labels)
            set(ADD_TEST ON)
        else()
            set(ADD_TEST OFF)
        endif()
    endif()

    if (ADD_TEST)
        if ((LC_BACKEND STREQUAL "llvm") OR (LC_BACKEND STREQUAL "cpp") OR (LC_BACKEND STREQUAL "x86")
                OR (LC_BACKEND STREQUAL "c") OR (LC_BACKEND STREQUAL "fortran") OR (LC_BACKEND STREQUAL "llvmPytorch"))
            add_custom_command(
                OUTPUT ${name}.o
                COMMAND ${LC} -c ${CMAKE_CURRENT_SOURCE_DIR}/${file_name} -o ${name}.o ${extra_args}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file_name}
                VERBATIM)
            add_executable(${name} ${name}.o ${extra_files})
            set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
            target_link_libraries(${name} lc_runtime)
            add_test(${name} ${CURRENT_BINARY_DIR}/${name})
        elseif (LC_BACKEND STREQUAL "wasm")
            # wasm test
            execute_process(COMMAND ${LC} ${extra_args} --backend=wasm ${CMAKE_CURRENT_SOURCE_DIR}/${file_name} -o ${name})
            find_program(WASM_EXEC_RUNTIME node)
            execute_process(COMMAND "${WASM_EXEC_RUNTIME}" --version
                            OUTPUT_VARIABLE WASM_EXEC_VERSION
                            OUTPUT_STRIP_TRAILING_WHITESPACE)

            string(REGEX REPLACE "v([0-9]+)\\.([0-9]+)\\.([0-9]+)" "\\1" NODE_MAJOR_VERSION "${WASM_EXEC_VERSION}")

            if (NODE_MAJOR_VERSION LESS 16)
                message(STATUS "${WASM_EXEC_RUNTIME} version: ${WASM_EXEC_VERSION}")
                set(WASM_EXEC_FLAGS "--experimental-wasm-bigint")
            endif()
            set(WASM_EXEC_FLAGS ${WASM_EXEC_FLAGS} "--experimental-wasi-unstable-preview1")
            add_test(${name} ${WASM_EXEC_RUNTIME} ${WASM_EXEC_FLAGS} ${CURRENT_BINARY_DIR}/${name}.js)
        elseif (LC_BACKEND STREQUAL "pytorch")
            # PyTorch C++ API testing
            find_package(Torch REQUIRED)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

            add_executable(${name} ${file_name} ${extra_files})
            target_link_libraries(${name} "${TORCH_LIBRARIES}")
            target_compile_options(${name} PUBLIC ${gcc_args})
            set_property(TARGET ${name} PROPERTY CXX_STANDARD 17)
            add_test(${name} ${CURRENT_BINARY_DIR}/${name})
        else ()
            add_executable(${name} ${file_name} ${extra_files})
            target_compile_options(${name} PUBLIC ${gcc_args})
            add_test(${name} ${CURRENT_BINARY_DIR}/${name})
        endif()

        if (labels)
            set_tests_properties(${name} PROPERTIES LABELS "${labels}")
        endif()

        if (fail)
            set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
        endif()

        if (copy_to_bin)
            file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/${copy_to_bin} DESTINATION ${CURRENT_BINARY_DIR})
        endif()
    endif()
endmacro(RUN_UTIL)

macro(RUN)
    set(options FAIL NOFAST)
    set(oneValueArgs NAME INCLUDE_PATH  COPY_TO_BIN)
    set(multiValueArgs LABELS EXTRAFILES EXTRA_ARGS GCC_ARGS)
    cmake_parse_arguments(RUN "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN} )

    foreach(b ${RUN_LABELS})
        if (NOT (b MATCHES "^(llvm|llvm2|llvm_rtlib|gcc|c|cpp|x86|wasm|gfortran|llvmImplicit|llvmStackArray|fortran|c_nopragma|llvm_nopragma|pytorch|llvmPytorch)$"))
            message(FATAL_ERROR "Unsupported backend: ${b}")
        endif()
    endforeach()

    set(RUN_FILE_NAME ${RUN_NAME})

    if (RUN_INCLUDE_PATH)
        # Only one include path supported for now
        # Later add support for multiple include paths by looping over and appending to extra args
        set(RUN_EXTRA_ARGS ${RUN_EXTRA_ARGS} -I${CMAKE_CURRENT_SOURCE_DIR}/${RUN_INCLUDE_PATH})
        set(RUN_GCC_ARGS ${RUN_GCC_ARGS} -I${CMAKE_CURRENT_SOURCE_DIR}/${RUN_INCLUDE_PATH})
    endif()

    if (NOT FAST)
        RUN_UTIL(RUN_FAIL RUN_NAME RUN_FILE_NAME RUN_LABELS RUN_EXTRAFILES RUN_EXTRA_ARGS RUN_COPY_TO_BIN RUN_GCC_ARGS)
    endif()

    if ((FAST) AND (NOT RUN_NOFAST))
        set(RUN_EXTRA_ARGS ${RUN_EXTRA_ARGS} --fast)
        set(RUN_NAME "${RUN_NAME}_FAST")
        list(REMOVE_ITEM RUN_LABELS gfortran) # remove gfortran from --fast test
        RUN_UTIL(RUN_FAIL RUN_NAME RUN_FILE_NAME RUN_LABELS RUN_EXTRAFILES RUN_EXTRA_ARGS RUN_COPY_TO_BIN RUN_GCC_ARGS)
    endif()
endmacro(RUN)

# The supported test labels are:
#
# gcc      --- compile with Gcc
# fortran       --- convert to Fortran code and compile with GFortran
# llvm          --- compile with LC, only generate binary code at the very end
# llvm2         --- compile with LC, generate object files
# llvm_rtlib    --- compile with LC loading ASR runtime library, generate object files
# cpp           --- compile to C++, compile C++ to binary
# x86           --- compile to x86 binary directly
# wasm          --- compile to WASM binary directly

RUN(NAME expr1.c LABELS gcc c wasm llvm)
RUN(NAME expr2.c LABELS gcc c wasm llvm NOFAST)
RUN(NAME expr3.c FAIL LABELS gcc c wasm llvm)

RUN(NAME switch_case_01.cpp LABELS gcc llvm)
RUN(NAME switch_case_02.cpp LABELS gcc llvm)

# arrays
RUN(NAME array_01.cpp LABELS gcc llvm)
RUN(NAME array_02.cpp LABELS gcc llvm)
RUN(NAME array_03.cpp LABELS gcc llvm)
RUN(NAME array_03_mdspan.cpp LABELS gcc llvm)
RUN(NAME array_04.cpp LABELS gcc llvm)
RUN(NAME array_04_mdspan.cpp LABELS gcc llvm)
RUN(NAME array_05.cpp LABELS gcc llvm)
RUN(NAME array_06.cpp LABELS gcc llvm)
RUN(NAME array_07.cpp LABELS gcc llvm)
RUN(NAME array_08.cpp LABELS gcc llvm)
RUN(NAME array_09.cpp LABELS gcc llvm)
RUN(NAME array_10.cpp LABELS gcc llvm)
RUN(NAME array_11.cpp LABELS gcc llvm)
RUN(NAME array_12.cpp LABELS gcc llvm)
RUN(NAME array_13.cpp LABELS gcc llvm)
RUN(NAME array_14.cpp LABELS gcc llvm NOFAST) # promote_allocatable_to_nonallocatable pass cannot be applied
RUN(NAME array_15.cpp LABELS gcc llvm)
RUN(NAME array_16.cpp LABELS gcc llvm)
RUN(NAME array_17.cpp LABELS gcc llvm)
RUN(NAME array_18.cpp LABELS gcc llvm)
RUN(NAME array_19.cpp LABELS gcc llvm)
RUN(NAME array_20.cpp LABELS gcc llvm)
RUN(NAME array_21.cpp LABELS gcc llvm)
RUN(NAME array_22.cpp LABELS gcc llvm)
RUN(NAME array_23.cpp LABELS gcc llvm)
RUN(NAME array_24.cpp LABELS gcc llvm)
RUN(NAME array_25.cpp LABELS gcc llvm)

RUN(NAME struct_01.cpp LABELS gcc llvm)
RUN(NAME struct_02.cpp LABELS gcc llvm)
RUN(NAME struct_03.cpp LABELS gcc llvm)
RUN(NAME struct_04.cpp LABELS gcc llvm)
RUN(NAME struct_05.cpp LABELS gcc llvm)
RUN(NAME struct_06.cpp LABELS gcc llvm)

RUN(NAME pointer_01.cpp LABELS gcc)
RUN(NAME pointer_02.cpp LABELS gcc)
RUN(NAME pointer_03.cpp LABELS gcc llvm)

RUN(NAME function_01.cpp LABELS gcc llvm NOFAST)

RUN(NAME nbody_01.cpp LABELS gcc llvm NOFAST)
RUN(NAME nbody_02.cpp LABELS gcc llvm NOFAST)

RUN(NAME enum_01.cpp LABELS gcc llvm)
RUN(NAME enum_02.cpp LABELS gcc llvm)
RUN(NAME enum_03.cpp LABELS gcc llvm)
RUN(NAME enum_04.cpp LABELS gcc llvm)

RUN(NAME union_01.cpp LABELS gcc llvm)
RUN(NAME union_02.cpp LABELS gcc llvm)

RUN(NAME vector_01.cpp LABELS gcc llvm)
RUN(NAME vector_02.cpp LABELS gcc llvm)

RUN(NAME loop_01.cpp LABELS gcc llvm NOFAST)

RUN(NAME test_pkg_lnn_01.cpp LABELS gcc llvm NOFAST)

RUN(NAME pytorch_01.cpp LABELS pytorch llvmPytorch)
