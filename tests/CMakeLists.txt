find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_phase0 tst_phase0.cpp)
target_link_libraries(tst_phase0 PRIVATE bc_ui Qt6::Test)
add_test(NAME phase0_ui COMMAND tst_phase0)
set_tests_properties(phase0_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_program(BC_FFMPEG_EXE NAMES ffmpeg HINTS ${FFMPEG_ROOT}/bin REQUIRED)
add_executable(tst_phase1 tst_phase1.cpp)
target_link_libraries(tst_phase1 PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase1 PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
if(BC_REQUIRE_LGPL_FFMPEG)
    target_compile_definitions(tst_phase1 PRIVATE BC_REQUIRE_LGPL_FFMPEG)
endif()
add_test(NAME phase1_project_media COMMAND tst_phase1)
set_tests_properties(phase1_project_media PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME translations_complete
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_translations.py
                     ${PROJECT_SOURCE_DIR}/i18n)
endif()

# Developer helper (not a test): renders screenshots of the main window.
add_executable(bc_screenshot screenshot.cpp)
target_link_libraries(bc_screenshot PRIVATE bc_ui)
