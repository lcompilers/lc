set(SRC
    parser/tokenizer.cpp
    parser/parser.cpp
    parser/parser.tab.cc

    utils.cpp
)

add_library(lc_lib STATIC ${SRC})
target_link_libraries(lc_lib asr lc_runtime_static)

if (WITH_ZLIB)
    target_link_libraries(lc_lib ZLIB::ZLIB)
endif()

target_include_directories(lc_lib BEFORE PUBLIC ${lfortran_SOURCE_DIR}/src)
target_include_directories(lc_lib BEFORE PUBLIC ${lfortran_BINARY_DIR}/src)
if (WITH_XEUS)
    target_link_libraries(lc_lib xeus xeus-zmq)
endif()
if (WITH_JSON)
    target_link_libraries(lc_lib p::rapidjson)
endif()
if (WITH_BFD)
    target_link_libraries(lc_lib p::bfd)
endif()
if (WITH_LINK)
    target_link_libraries(lc_lib p::link)
endif()
if (WITH_EXECINFO)
    target_link_libraries(lc_lib p::execinfo)
endif()
if (WITH_LLVM)
    target_link_libraries(lc_lib p::llvm)
endif()
#install(TARGETS lc_lib
#    RUNTIME DESTINATION bin
#    ARCHIVE DESTINATION lib
#    LIBRARY DESTINATION lib
#)
