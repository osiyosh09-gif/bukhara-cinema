cmake_minimum_required(VERSION 3.21)

project(BukharaCinema
    VERSION 0.39.1
    DESCRIPTION "BUKHARA CINEMA video editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BC_BUILD_TESTS "Build automated tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network LinguistTools)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(FFmpeg REQUIRED)

# VST 3 audio plug-ins (phase 15c): Steinberg VST 3 SDK, MIT licence, fetched at configure time.
option(BC_ENABLE_VST3 "Host VST 3 audio plug-ins (Steinberg VST 3 SDK, MIT)" ON)
if(BC_ENABLE_VST3)
    include(Vst3Sdk)
endif()

# ---------------------------------------------------------------------------
# Core (no widgets): project model, media probing
# ---------------------------------------------------------------------------
add_library(bc_core STATIC
    src/core/Project.h          src/core/Project.cpp
    src/core/MediaProbe.h       src/core/MediaProbe.cpp
    src/core/Timeline.h         src/core/Timeline.cpp
    src/core/Waveform.h         src/core/Waveform.cpp
    src/core/Loudness.h         src/core/Loudness.cpp
    src/core/AudioSync.h        src/core/AudioSync.cpp
    src/core/Multicam.h         src/core/Multicam.cpp
    src/core/Subtitles.h        src/core/Subtitles.cpp
    src/core/Backups.h          src/core/Backups.cpp
    src/core/MediaCache.h       src/core/MediaCache.cpp
    src/core/Proxy.h            src/core/Proxy.cpp
    src/core/HwAccel.h          src/core/HwAccel.cpp
    src/core/SceneDetect.h      src/core/SceneDetect.cpp
    src/core/Interchange.h      src/core/Interchange.cpp
    src/core/Vst3Host.h         src/core/Vst3Host.cpp
    src/core/Vst3Ipc.h          src/core/Vst3Ipc.cpp
    src/core/Vst3Remote.cpp
    src/core/AutoMulticam.h     src/core/AutoMulticam.cpp
    src/core/Speech.h           src/core/Speech.cpp
    src/core/InterchangeRead.cpp
    src/core/RenderPreview.h    src/core/RenderPreview.cpp
    src/core/Collector.h        src/core/Collector.cpp
    src/core/Motion.h           src/core/Motion.cpp
    src/core/Effects.h          src/core/Effects.cpp
    src/core/AudioFx.h          src/core/AudioFx.cpp
    src/core/Color.h            src/core/Color.cpp
    src/core/Lut.h              src/core/Lut.cpp
    src/core/Scopes.h           src/core/Scopes.cpp
)
target_include_directories(bc_core PUBLIC src)
target_link_libraries(bc_core PUBLIC Qt6::Core Qt6::Gui FFmpeg::FFmpeg)
if(BC_ENABLE_VST3)
    target_link_libraries(bc_core PRIVATE bc_vst3host)
    set_source_files_properties(src/core/Vst3Host.cpp PROPERTIES COMPILE_DEFINITIONS BC_HAVE_VST3)
endif()

# Video engine: MLT (LGPL). On Windows CI it is built from source against the
# LGPL FFmpeg; never use a distribution package that links GPL FFmpeg there.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLT REQUIRED IMPORTED_TARGET mlt++-7)
add_library(bc_engine STATIC
    src/engine/TimelinePlayer.h src/engine/TimelinePlayer.cpp
    src/engine/TitleRenderer.h src/engine/TitleRenderer.cpp
    src/engine/EngineServices.h src/engine/EngineServices.cpp
    src/engine/Exporter.h src/engine/Exporter.cpp
)
target_include_directories(bc_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bc_engine PUBLIC bc_core Qt6::Core Qt6::Gui PRIVATE PkgConfig::MLT)

# ---------------------------------------------------------------------------
# Core UI library (shared by the app and the tests)
# ---------------------------------------------------------------------------
add_library(bc_ui STATIC
    src/app/AppInfo.h
    src/app/Language.h          src/app/Language.cpp
    src/app/Theme.h             src/app/Theme.cpp
    src/app/FeatureStatus.h     src/app/FeatureStatus.cpp
    src/app/PlaceholderPanel.h  src/app/PlaceholderPanel.cpp
    src/app/MonitorView.h       src/app/MonitorView.cpp
    src/app/ClipInspector.h     src/app/ClipInspector.cpp
    src/app/EffectsPanel.h      src/app/EffectsPanel.cpp
    src/app/Vst3Plugins.h       src/app/Vst3Plugins.cpp
    src/app/AudioMixer.h        src/app/AudioMixer.cpp
    src/app/ColorPanel.h        src/app/ColorPanel.cpp
    src/app/ColorWheel.h        src/app/ColorWheel.cpp
    src/app/ScopesPanel.h       src/app/ScopesPanel.cpp
    src/app/ExportDialog.h      src/app/ExportDialog.cpp
    src/app/Autosave.h          src/app/Autosave.cpp
    src/app/RecoveryDialog.h    src/app/RecoveryDialog.cpp
    src/app/CacheDialog.h       src/app/CacheDialog.cpp
    src/app/CollectDialog.h     src/app/CollectDialog.cpp
    src/app/SettingsDialog.h    src/app/SettingsDialog.cpp
    src/app/Shortcuts.h    src/app/Shortcuts.cpp
    src/app/SpeedDialog.h  src/app/SpeedDialog.cpp
    src/app/MixLoudnessDialog.h src/app/MixLoudnessDialog.cpp
    src/app/SceneDialog.h       src/app/SceneDialog.cpp
    src/app/LoudnessDialog.h src/app/LoudnessDialog.cpp
    src/app/MulticamDialog.h src/app/MulticamDialog.cpp
    src/app/MulticamMonitor.h src/app/MulticamMonitor.cpp
    src/app/ExportQueue.h src/app/ExportQueue.cpp
    src/app/ExportQueueDialog.h src/app/ExportQueueDialog.cpp
    src/app/SubtitlesPanel.h src/app/SubtitlesPanel.cpp
    src/app/AboutDialog.h       src/app/AboutDialog.cpp
    src/app/StatusDialog.h      src/app/StatusDialog.cpp
    src/app/MainWindow.h        src/app/MainWindow.cpp
    src/app/Logging.h           src/app/Logging.cpp
    src/app/ProjectController.h src/app/ProjectController.cpp
    src/app/ProjectProxies.cpp
    src/app/ProjectSource.cpp
    src/app/ProjectEditing.cpp
    src/app/ProjectLoudness.cpp
    src/app/ProjectMulticam.cpp
    src/app/ProjectMixLoudness.cpp
    src/app/ProjectScenes.cpp
    src/app/ProjectPreview.cpp
    src/app/ProjectSubtitles.cpp
    src/app/ProjectInterchange.cpp
    src/app/ProjectAutoMulticam.cpp
    src/app/ProjectSpeech.cpp
    src/app/SpeechDialog.h      src/app/SpeechDialog.cpp
    src/app/AutoMulticamDialog.h src/app/AutoMulticamDialog.cpp
    src/app/InterchangeDialog.h src/app/InterchangeDialog.cpp
    src/app/InterchangeImportDialog.h src/app/InterchangeImportDialog.cpp
    src/app/MediaBinPanel.h     src/app/MediaBinPanel.cpp
    src/app/ProjectInfoPanel.h  src/app/ProjectInfoPanel.cpp
    src/app/SequenceSettingsDialog.h src/app/SequenceSettingsDialog.cpp
    src/app/Formatting.h        src/app/Formatting.cpp
    src/app/TimelinePanel.h     src/app/TimelinePanel.cpp
    resources/resources.qrc
)
target_include_directories(bc_ui PUBLIC src)
target_link_libraries(bc_ui PUBLIC bc_core bc_engine Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Network)
target_compile_definitions(bc_ui PUBLIC
    BC_VERSION="${PROJECT_VERSION}")

set(BC_TS_FILES
    i18n/bukhara_uz_Cyrl.ts
    i18n/bukhara_uz_Latn.ts
    i18n/bukhara_ru.ts
    i18n/bukhara_en.ts
)
# lupdate reads the UI and the engine (export messages are shown to the user).
get_target_property(BC_UI_SOURCES bc_ui SOURCES)
get_target_property(BC_ENGINE_SOURCES bc_engine SOURCES)
qt_add_translations(bc_ui
    TS_FILES ${BC_TS_FILES}
    SOURCES ${BC_UI_SOURCES} ${BC_ENGINE_SOURCES}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(BC_APP_SOURCES src/main.cpp)
if(WIN32)
    list(APPEND BC_APP_SOURCES resources/windows/app.rc)
endif()
add_executable(BukharaCinema WIN32 ${BC_APP_SOURCES})
target_link_libraries(BukharaCinema PRIVATE bc_ui)

install(TARGETS BukharaCinema RUNTIME DESTINATION .)

# Speech recognition helper (phase 14b): runs Vosk (speech/libvosk.dll with its
# own C++ runtime) in a process of its own; plain C++, no Qt.
add_executable(BukharaSpeech src/speechhelper/main.cpp)
target_compile_features(BukharaSpeech PRIVATE cxx_std_17)
if(MINGW)
    target_link_options(BukharaSpeech PRIVATE -static -static-libgcc -static-libstdc++)
elseif(UNIX)
    target_link_libraries(BukharaSpeech PRIVATE ${CMAKE_DL_LIBS})
endif()
install(TARGETS BukharaSpeech RUNTIME DESTINATION .)

# The plug-in host (phase 15c-2): each VST 3 plug-in runs in its own copy of
# this small program, so a crashing plug-in cannot take the editor down.
if(BC_ENABLE_VST3)
    add_executable(BukharaPluginHost
        src/pluginhost/main.cpp
        src/core/Vst3Host.h src/core/Vst3Host.cpp
        src/core/Vst3Ipc.h src/core/Vst3Ipc.cpp
        src/core/Vst3Remote.cpp)
    target_include_directories(BukharaPluginHost PRIVATE src)
    target_compile_definitions(BukharaPluginHost PRIVATE BC_HAVE_VST3)
    target_link_libraries(BukharaPluginHost PRIVATE bc_vst3host)
    if(MINGW)
        # One file without runtime DLLs next to it.
        target_link_options(BukharaPluginHost PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    install(TARGETS BukharaPluginHost RUNTIME DESTINATION .)
endif()
install(FILES
    licenses/THIRD_PARTY.md
    licenses/LGPL-3.0.txt
    licenses/GPL-3.0.txt
    licenses/VST3-SDK-MIT.txt
    licenses/Vosk-Apache-2.0.txt
    DESTINATION licenses)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
