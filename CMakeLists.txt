cmake_minimum_required(VERSION 3.22)
project(run_ffmpeg LANGUAGES C VERSION 0.0.1 DESCRIPTION "run ffmpeg in thread")

SET(CMAKE_C_STANDARD 17)


if(APPLE)
    SET (CMAKE_C_COMPILER "clang")
endif()

if(UNIX AND NOT APPLE)
    SET (CMAKE_C_COMPILER "gcc")
    SET (CMAKE_CXX_COMPILER "g++")
endif()



aux_source_directory(. SRCS)
include_directories(/usr/local/include)
link_directories(/usr/local/lib)

add_library(run_ffmpeg SHARED  ${SRCS})
set_target_properties(run_ffmpeg PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(run_ffmpeg PROPERTIES PUBLIC_HEADER run_ffmpeg.h)
target_link_libraries(run_ffmpeg avformat avdevice avcodec avutil avfilter swscale swresample m)

INSTALL(TARGETS run_ffmpeg
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
INSTALL(FILES run_ffmpeg.h DESTINATION include)

