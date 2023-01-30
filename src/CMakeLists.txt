add_executable(lc lc.cpp)
target_link_libraries(lc p::clang p::llvm)

add_executable(fa fa.cpp)
target_link_libraries(fa p::clang p::llvm)
set_target_properties(fa PROPERTIES CXX_VISIBILITY_PRESET hidden)

add_executable(fa2 fa2.cpp)
target_link_libraries(fa2 p::clang p::llvm asr)
set_target_properties(fa2 PROPERTIES CXX_VISIBILITY_PRESET hidden)
