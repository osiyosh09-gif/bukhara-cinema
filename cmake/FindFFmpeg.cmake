# Finds FFmpeg libraries: avformat avcodec avutil swscale.
# Creates imported target FFmpeg::FFmpeg.
# Hint: -DFFMPEG_ROOT=<folder with include/ and lib/> (Windows LGPL build).

set(_ff_components avformat avcodec avutil swscale)

if(NOT FFMPEG_ROOT)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
        if(PC_FFMPEG_FOUND)
            add_library(FFmpeg::FFmpeg ALIAS PkgConfig::PC_FFMPEG)
            set(FFmpeg_FOUND TRUE)
            set(FFmpeg_VERSION "${PC_FFMPEG_libavformat_VERSION}")
            message(STATUS "FFmpeg found via pkg-config (avformat ${FFmpeg_VERSION})")
            return()
        endif()
    endif()
endif()

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
    HINTS ${FFMPEG_ROOT} PATH_SUFFIXES include)

set(_ff_libs)
foreach(c ${_ff_components})
    find_library(FFMPEG_${c}_LIBRARY NAMES ${c} lib${c}
        HINTS ${FFMPEG_ROOT} PATH_SUFFIXES lib)
    list(APPEND _ff_libs ${FFMPEG_${c}_LIBRARY})
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS FFMPEG_INCLUDE_DIR
    FFMPEG_avformat_LIBRARY FFMPEG_avcodec_LIBRARY FFMPEG_avutil_LIBRARY FFMPEG_swscale_LIBRARY)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${_ff_libs}")
endif()
