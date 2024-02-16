set(SRC
    ../../../src/libasr/runtime/lfortran_intrinsics.c
)
add_library(lc_runtime SHARED ${SRC})
target_include_directories(lc_runtime BEFORE PUBLIC ${libasr_SOURCE_DIR}/..)
target_include_directories(lc_runtime BEFORE PUBLIC ${libasr_BINARY_DIR}/..)
set_target_properties(lc_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../$<0:>
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../$<0:>
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../$<0:>)
add_library(lc_runtime_static STATIC ${SRC})
target_include_directories(lc_runtime_static BEFORE PUBLIC ${libasr_SOURCE_DIR}/..)
target_include_directories(lc_runtime_static BEFORE PUBLIC ${libasr_BINARY_DIR}/..)
set_target_properties(lc_runtime_static PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../$<0:>
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../$<0:>
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../$<0:>)
install(TARGETS lc_runtime lc_runtime_static
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION share/lc/lib
    LIBRARY DESTINATION share/lc/lib
)

# Install the header
install(
    FILES ../../../src/libasr/runtime/lfortran_intrinsics.h
    DESTINATION share/lc/lib/impure
)
