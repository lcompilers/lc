add_executable(lc lc.cpp)
target_link_libraries(lc p::clang p::llvm)

add_executable(fa fa.cpp)
target_link_libraries(fa p::clang p::llvm)
