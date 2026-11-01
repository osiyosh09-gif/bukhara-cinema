find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_phase0 tst_phase0.cpp)
target_link_libraries(tst_phase0 PRIVATE bc_ui Qt6::Test)
add_test(NAME phase0_ui COMMAND tst_phase0)
set_tests_properties(phase0_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")

find_program(BC_FFMPEG_EXE NAMES ffmpeg HINTS ${FFMPEG_ROOT}/bin REQUIRED)
add_executable(tst_phase1 tst_phase1.cpp)
target_link_libraries(tst_phase1 PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase1 PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
if(BC_REQUIRE_LGPL_FFMPEG)
    target_compile_definitions(tst_phase1 PRIVATE BC_REQUIRE_LGPL_FFMPEG)
endif()
add_test(NAME phase1_project_media COMMAND tst_phase1)
set_tests_properties(phase1_project_media PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase2 tst_phase2.cpp)
target_link_libraries(tst_phase2 PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase2 PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase2_timeline COMMAND tst_phase2)
set_tests_properties(phase2_timeline PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase3 tst_phase3.cpp)
target_link_libraries(tst_phase3 PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase3 PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase3_titles_audio COMMAND tst_phase3)
set_tests_properties(phase3_titles_audio PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase4 tst_phase4.cpp)
target_link_libraries(tst_phase4 PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase4 PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase4_export COMMAND tst_phase4)
set_tests_properties(phase4_export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase5 tst_phase5.cpp)
target_link_libraries(tst_phase5 PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase5 PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase5_reliability COMMAND tst_phase5)
set_tests_properties(phase5_reliability PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase5b tst_phase5b.cpp)
target_link_libraries(tst_phase5b PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase5b PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase5b_proxies_collect COMMAND tst_phase5b)
set_tests_properties(phase5b_proxies_collect PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 600)

add_executable(tst_phase6a tst_phase6a.cpp)
target_link_libraries(tst_phase6a PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase6a PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase6a_source_edits COMMAND tst_phase6a)
set_tests_properties(phase6a_source_edits PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase6b tst_phase6b.cpp)
target_link_libraries(tst_phase6b PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase6b PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase6b_trim_tools COMMAND tst_phase6b)
set_tests_properties(phase6b_trim_tools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase6c tst_phase6c.cpp)
target_link_libraries(tst_phase6c PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase6c PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase6c_nest_shortcuts COMMAND tst_phase6c)
set_tests_properties(phase6c_nest_shortcuts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase7a tst_phase7a.cpp)
target_link_libraries(tst_phase7a PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase7a PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase7a_motion COMMAND tst_phase7a)
set_tests_properties(phase7a_motion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase7b tst_phase7b.cpp)
target_link_libraries(tst_phase7b PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase7b PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase7b_speed COMMAND tst_phase7b)
set_tests_properties(phase7b_speed PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

add_executable(tst_phase8a tst_phase8a.cpp)
target_link_libraries(tst_phase8a PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase8a PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase8a_transitions COMMAND tst_phase8a)
set_tests_properties(phase8a_transitions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME translations_complete
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_translations.py
                     ${PROJECT_SOURCE_DIR}/i18n)
endif()

# Developer helper (not a test): renders screenshots of the main window.
add_executable(bc_screenshot screenshot.cpp)
target_link_libraries(bc_screenshot PRIVATE bc_ui)

add_executable(tst_phase8b tst_phase8b.cpp)
target_link_libraries(tst_phase8b PRIVATE bc_ui Qt6::Test)
target_compile_definitions(tst_phase8b PRIVATE BC_FFMPEG_EXE="${BC_FFMPEG_EXE}")
add_test(NAME phase8b_effects COMMAND tst_phase8b)
set_tests_properties(phase8b_effects PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy" TIMEOUT 300)
