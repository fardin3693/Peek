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
