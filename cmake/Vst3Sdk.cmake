# Steinberg VST 3 SDK (MIT licence since 3.8): only the parts needed to host
# VST 3 audio effects (and to build the small test plug-in of tests/vst3).
# The SDK's own CMake is not used: it always adds its hosting examples, which
# need more system libraries. The source lists below follow the SDK's targets
# base, pluginterfaces, sdk_common and sdk_hosting (cmake/modules/SMTG_VST3_SDK.cmake).

include(FetchContent)
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.8.1_build_84
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES base pluginterfaces public.sdk
    SOURCE_SUBDIR  bc-sources-only) # no CMakeLists there: download only
FetchContent_MakeAvailable(vst3sdk)
set(VST3SDK_DIR ${vst3sdk_SOURCE_DIR})

set(_s ${VST3SDK_DIR})
set(BC_VST3_BASE_SOURCES
    ${_s}/base/source/baseiids.cpp
    ${_s}/base/source/fbuffer.cpp
    ${_s}/base/source/fdebug.cpp
    ${_s}/base/source/fdynlib.cpp
    ${_s}/base/source/fobject.cpp
    ${_s}/base/source/fstreamer.cpp
    ${_s}/base/source/fstring.cpp
    ${_s}/base/source/timer.cpp
    ${_s}/base/source/updatehandler.cpp
    ${_s}/base/thread/source/fcondition.cpp
    ${_s}/base/thread/source/flock.cpp
    ${_s}/pluginterfaces/base/conststringtable.cpp
    ${_s}/pluginterfaces/base/coreiids.cpp
    ${_s}/pluginterfaces/base/funknown.cpp
    ${_s}/pluginterfaces/base/ustring.cpp
    ${_s}/public.sdk/source/common/commoniids.cpp
    ${_s}/public.sdk/source/common/commonstringconvert.cpp
    ${_s}/public.sdk/source/common/memorystream.cpp
    ${_s}/public.sdk/source/vst/utility/stringconvert.cpp
)

function(bc_vst3_setup target)
    target_include_directories(${target} PUBLIC ${VST3SDK_DIR})
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_compile_definitions(${target} PUBLIC
        $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
    if(MINGW)
        target_compile_options(${target} PRIVATE -Wno-multichar)
    endif()
    if(NOT MSVC)
        # SDK code: its own warnings are not ours to fix.
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# --- hosting: load .vst3 modules, run their components and controllers
add_library(bc_vst3host STATIC
    ${BC_VST3_BASE_SOURCES}
    ${_s}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${_s}/public.sdk/source/vst/hosting/eventlist.cpp
    ${_s}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${_s}/public.sdk/source/vst/hosting/module.cpp
    ${_s}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${_s}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${_s}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${_s}/public.sdk/source/vst/hosting/processdata.cpp
    ${_s}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp
    ${_s}/public.sdk/source/vst/moduleinfo/moduleinfocreator.cpp
    ${_s}/public.sdk/source/vst/vstinitiids.cpp
)
if(WIN32)
    target_sources(bc_vst3host PRIVATE ${_s}/public.sdk/source/vst/hosting/module_win32.cpp
                                       ${_s}/public.sdk/source/common/threadchecker_win32.cpp)
    target_link_libraries(bc_vst3host PUBLIC shell32 ole32 user32)
elseif(UNIX AND NOT APPLE)
    target_sources(bc_vst3host PRIVATE ${_s}/public.sdk/source/vst/hosting/module_linux.cpp
                                       ${_s}/public.sdk/source/common/threadchecker_linux.cpp)
    target_link_libraries(bc_vst3host PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()
bc_vst3_setup(bc_vst3host)
set_target_properties(bc_vst3host PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- plug-in side, for the test plug-in only (tests/vst3)
set(BC_VST3_PLUGIN_SOURCES
    ${BC_VST3_BASE_SOURCES}
    ${_s}/public.sdk/source/common/pluginview.cpp
    ${_s}/public.sdk/source/main/pluginfactory.cpp
    ${_s}/public.sdk/source/main/moduleinit.cpp
    ${_s}/public.sdk/source/vst/vstbus.cpp
    ${_s}/public.sdk/source/vst/vstcomponentbase.cpp
    ${_s}/public.sdk/source/vst/vstinitiids.cpp
    ${_s}/public.sdk/source/vst/vstparameters.cpp
    ${_s}/public.sdk/source/vst/vstsinglecomponenteffect.cpp # includes vsteditcontroller.cpp
)
if(WIN32)
    list(APPEND BC_VST3_PLUGIN_SOURCES ${_s}/public.sdk/source/main/dllmain.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND BC_VST3_PLUGIN_SOURCES ${_s}/public.sdk/source/main/linuxmain.cpp)
endif()

# Builds a .vst3 bundle: test-vst3/<name>.vst3/Contents/<arch>/<name>.vst3|.so
# (not "vst3" next to the tests: the SDK looks there, so every test would find it)
function(bc_add_test_vst3 name)
    add_library(${name} MODULE ${ARGN} ${BC_VST3_PLUGIN_SOURCES})
    bc_vst3_setup(${name})
    if(WIN32)
        set(arch x86_64-win)
        set(suffix .vst3)
    else()
        set(arch x86_64-linux)
        set(suffix .so)
        target_link_libraries(${name} PRIVATE ${CMAKE_DL_LIBS} pthread)
    endif()
    set(bundle ${CMAKE_CURRENT_BINARY_DIR}/test-vst3/${name}.vst3)
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        SUFFIX ${suffix}
        LIBRARY_OUTPUT_DIRECTORY ${bundle}/Contents/${arch}
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${bundle}/Contents/${arch}
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${bundle}/Contents/${arch})
    if(MINGW)
        # One file to copy next to nothing: no libstdc++/libgcc DLLs needed by the module.
        target_link_options(${name} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
endfunction()
