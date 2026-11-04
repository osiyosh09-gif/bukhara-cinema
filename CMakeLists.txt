cmake_minimum_required(VERSION 3.21)

project(BukharaCinema
    VERSION 0.1.0
    DESCRIPTION "BUKHARA CINEMA video editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BC_BUILD_TESTS "Build automated tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)

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
    src/app/SettingsDialog.h    src/app/SettingsDialog.cpp
    src/app/AboutDialog.h       src/app/AboutDialog.cpp
    src/app/StatusDialog.h      src/app/StatusDialog.cpp
    src/app/MainWindow.h        src/app/MainWindow.cpp
    src/app/Logging.h           src/app/Logging.cpp
    resources/resources.qrc
)
target_include_directories(bc_ui PUBLIC src)
target_link_libraries(bc_ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(bc_ui PUBLIC
    BC_VERSION="${PROJECT_VERSION}")

set(BC_TS_FILES
    i18n/bukhara_uz_Cyrl.ts
    i18n/bukhara_uz_Latn.ts
    i18n/bukhara_ru.ts
)
qt_add_translations(bc_ui
    TS_FILES ${BC_TS_FILES}
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
