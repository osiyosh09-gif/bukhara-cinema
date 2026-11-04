cmake_minimum_required(VERSION 3.21)

project(BukharaCinema
    VERSION 0.10.0
    DESCRIPTION "BUKHARA CINEMA video editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BC_BUILD_TESTS "Build automated tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent LinguistTools)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(FFmpeg REQUIRED)

# ---------------------------------------------------------------------------
# Core (no widgets): project model, media probing
# ---------------------------------------------------------------------------
add_library(bc_core STATIC
    src/core/Project.h          src/core/Project.cpp
    src/core/MediaProbe.h       src/core/MediaProbe.cpp
    src/core/Timeline.h         src/core/Timeline.cpp
    src/core/Waveform.h         src/core/Waveform.cpp
    src/core/Backups.h          src/core/Backups.cpp
    src/core/MediaCache.h       src/core/MediaCache.cpp
    src/core/Proxy.h            src/core/Proxy.cpp
    src/core/Collector.h        src/core/Collector.cpp
)
target_include_directories(bc_core PUBLIC src)
target_link_libraries(bc_core PUBLIC Qt6::Core Qt6::Gui FFmpeg::FFmpeg)

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
    src/app/ExportDialog.h      src/app/ExportDialog.cpp
    src/app/Autosave.h          src/app/Autosave.cpp
    src/app/RecoveryDialog.h    src/app/RecoveryDialog.cpp
    src/app/CacheDialog.h       src/app/CacheDialog.cpp
    src/app/CollectDialog.h     src/app/CollectDialog.cpp
    src/app/SettingsDialog.h    src/app/SettingsDialog.cpp
    src/app/AboutDialog.h       src/app/AboutDialog.cpp
    src/app/StatusDialog.h      src/app/StatusDialog.cpp
    src/app/MainWindow.h        src/app/MainWindow.cpp
    src/app/Logging.h           src/app/Logging.cpp
    src/app/ProjectController.h src/app/ProjectController.cpp
    src/app/ProjectProxies.cpp
    src/app/ProjectSource.cpp
    src/app/ProjectEditing.cpp
    src/app/MediaBinPanel.h     src/app/MediaBinPanel.cpp
    src/app/ProjectInfoPanel.h  src/app/ProjectInfoPanel.cpp
    src/app/SequenceSettingsDialog.h src/app/SequenceSettingsDialog.cpp
    src/app/Formatting.h        src/app/Formatting.cpp
    src/app/TimelinePanel.h     src/app/TimelinePanel.cpp
    resources/resources.qrc
)
target_include_directories(bc_ui PUBLIC src)
target_link_libraries(bc_ui PUBLIC bc_core bc_engine Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(bc_ui PUBLIC
    BC_VERSION="${PROJECT_VERSION}")

set(BC_TS_FILES
    i18n/bukhara_uz_Cyrl.ts
    i18n/bukhara_uz_Latn.ts
    i18n/bukhara_ru.ts
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
install(FILES
    licenses/THIRD_PARTY.md
    licenses/LGPL-3.0.txt
    licenses/GPL-3.0.txt
    DESTINATION licenses)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
