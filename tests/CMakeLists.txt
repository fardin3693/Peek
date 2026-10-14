# Smoke tests: the executable starts, initializes Qt and exits with status 0.
# The offscreen platform plugin lets them run without a display server, and the
# empty platform theme stops desktop theme plugins (e.g. gtk3) from needing one.
set(PEEK_TEST_ENV "QT_QPA_PLATFORM=offscreen" "QT_QPA_PLATFORMTHEME=")

add_test(NAME peek_starts COMMAND peek)
set_tests_properties(peek_starts PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV}"
    PASS_REGULAR_EXPRESSION "Peek [0-9]+\\.[0-9]+\\.[0-9]+"
)

add_test(NAME peek_version COMMAND peek --version)
set_tests_properties(peek_version PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV}"
    PASS_REGULAR_EXPRESSION "peek [0-9]+\\.[0-9]+\\.[0-9]+"
)

find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

add_executable(peek_preview_tests
    test_preview.cpp
    ../src/preview_request.cpp
    ../src/preview_window.cpp
    ../src/text_preview.cpp
    ../src/text_provider.cpp
    ../src/markdown_preview.cpp
    ../src/markdown_provider.cpp
    ../src/image_provider.cpp
    ../src/video_preview.cpp
    ../src/audio_preview.cpp
)
target_include_directories(peek_preview_tests PRIVATE ../src)
target_link_libraries(peek_preview_tests PRIVATE Qt6::Widgets Qt6::Test)
if(PEEK_HAVE_MULTIMEDIA)
    target_sources(peek_preview_tests PRIVATE ../src/audio_provider.cpp ../src/audio_provider.h)
    target_compile_definitions(peek_preview_tests PRIVATE PEEK_WITH_MULTIMEDIA)
    target_link_libraries(peek_preview_tests PRIVATE Qt6::Multimedia Qt6::MultimediaWidgets)
endif()
if(PEEK_HAVE_POPPLER)
    target_sources(peek_preview_tests PRIVATE ../src/pdf_preview.cpp ../src/pdf_preview.h
        ../src/pdf_provider.cpp ../src/pdf_provider.h)
    target_compile_definitions(peek_preview_tests PRIVATE PEEK_WITH_PDF)
    target_link_libraries(peek_preview_tests PRIVATE PkgConfig::POPPLER_CPP)
endif()
peek_configure_target(peek_preview_tests)
add_dependencies(peek_preview_tests peek)

add_test(NAME peek_preview COMMAND peek_preview_tests)
set_tests_properties(peek_preview PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV};PEEK_TEST_EXECUTABLE=$<TARGET_FILE:peek>;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
    TIMEOUT 90
)

add_test(NAME peek_cli COMMAND ${CMAKE_COMMAND}
    "-DPEEK_EXECUTABLE=$<TARGET_FILE:peek>"
    "-DPEEK_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures"
    "-DPEEK_VERSION=${PROJECT_VERSION}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_cli.cmake"
)
set_tests_properties(peek_cli PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV}"
    TIMEOUT 60
)

# Python is used only by the optional desktop adapter and its tests, not Peek.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_test(NAME peek_nautilus COMMAND ${Python3_EXECUTABLE} -B
    "${CMAKE_CURRENT_SOURCE_DIR}/test_nautilus.py"
)
set_tests_properties(peek_nautilus PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV};PEEK_TEST_EXECUTABLE=$<TARGET_FILE:peek>"
    TIMEOUT 60
)

# Regression guard: the Task C experiment lab must stay runnable. Unresolved
# merge markers and syntax errors fail here, before the experiment runs.
add_test(NAME peek_taskc_lab_guard COMMAND ${Python3_EXECUTABLE} -B
    "${CMAKE_CURRENT_SOURCE_DIR}/check_taskc_lab.py"
)
set_tests_properties(peek_taskc_lab_guard PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV}"
    TIMEOUT 60
)

# Regression guard: the P-UX1 Quick Look trigger files must stay consistent
# (Hyprland snippet shape, Nautilus accelerator line, contract doc present).
# No desktop changes, no key synthesis; see docs/quick-look-trigger.md.
add_test(NAME peek_ux1_trigger_guard COMMAND ${Python3_EXECUTABLE} -B
    "${CMAKE_CURRENT_SOURCE_DIR}/check_ux1_trigger.py"
)
set_tests_properties(peek_ux1_trigger_guard PROPERTIES
    ENVIRONMENT "${PEEK_TEST_ENV}"
    TIMEOUT 60
)
