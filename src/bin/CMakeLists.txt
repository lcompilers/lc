add_executable(lc lc.cpp)
target_link_libraries(lc p::clang p::llvm asr)
