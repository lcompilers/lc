add_executable(lc lc.cpp)
target_include_directories(lc PRIVATE "tpl")
target_link_libraries(lc p::clang p::llvm asr lc_lib)
install(TARGETS lc DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
