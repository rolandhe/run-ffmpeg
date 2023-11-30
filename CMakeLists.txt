cmake_minimum_required(VERSION 3.22)
project(run_ffmpeg LANGUAGES C VERSION 0.0.1 DESCRIPTION "run ffmpeg in thread")

SET(CMAKE_C_STANDARD 17)


if(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        add_definitions(-DCONF_MAC_X86=1)
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(arm64)|(ARM64)")
        add_definitions(-DCONF_MAC_ARM=1)
    else()
        message(STATUS "host processor architecture is unknown")
    endif()
    SET (CMAKE_C_COMPILER "clang")
endif()

if(UNIX AND NOT APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        add_definitions(-DCONF_LINUX_X86=1)
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(arm64)|(ARM64)")
        add_definitions(-DCONF_LINUX_ARM=1)
    else()
        message(STATUS "host processor architecture is unknown")
    endif()
    SET (CMAKE_C_COMPILER "gcc")
    SET (CMAKE_CXX_COMPILER "g++")
endif()



aux_source_directory(. SRCS)
include_directories(/usr/local/include)
link_directories(/usr/local/lib)

add_library(run_ffmpeg SHARED  ${SRCS})
set_target_properties(run_ffmpeg PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(run_ffmpeg PROPERTIES PUBLIC_HEADER run_ffmpeg.h)

if(APPLE)
    target_link_libraries(run_ffmpeg avformat avdevice avcodec avutil avfilter swscale swresample)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(run_ffmpeg avformat avdevice avcodec avutil avfilter swscale swresample m)
endif()


INSTALL(TARGETS run_ffmpeg
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
INSTALL(FILES run_ffmpeg.h DESTINATION include)

