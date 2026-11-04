find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_phase0 tst_phase0.cpp)
target_link_libraries(tst_phase0 PRIVATE bc_ui Qt6::Test)
add_test(NAME phase0_ui COMMAND tst_phase0)
set_tests_properties(phase0_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME translations_complete
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_translations.py
                     ${PROJECT_SOURCE_DIR}/i18n)
endif()

# Developer helper (not a test): renders screenshots of the main window.
add_executable(bc_screenshot screenshot.cpp)
target_link_libraries(bc_screenshot PRIVATE bc_ui)
