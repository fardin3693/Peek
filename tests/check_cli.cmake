cmake_minimum_required(VERSION 3.21)

foreach(required IN ITEMS PEEK_EXECUTABLE PEEK_TEST_DIR PEEK_VERSION)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be supplied by the test registration")
    endif()
endforeach()
if(NOT IS_ABSOLUTE "${PEEK_EXECUTABLE}" OR NOT EXISTS "${PEEK_EXECUTABLE}" OR
   IS_DIRECTORY "${PEEK_EXECUTABLE}")
    message(FATAL_ERROR "PEEK_EXECUTABLE must name an existing absolute executable path")
endif()
if(NOT IS_ABSOLUTE "${PEEK_TEST_DIR}" OR "${PEEK_TEST_DIR}" STREQUAL "/")
    message(FATAL_ERROR "PEEK_TEST_DIR must name an absolute fixture directory, not the root")
endif()
if(NOT PEEK_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "PEEK_VERSION must be the configured semantic version")
endif()

# Own a fresh child directory; never overwrite fixtures or recursively remove
# the caller's directory. Failed cases leave this child available for inspection.
file(MAKE_DIRECTORY "${PEEK_TEST_DIR}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef fixture_id)
get_filename_component(fixture_dir "${PEEK_TEST_DIR}/peek-cli-${fixture_id}" ABSOLUTE)
if(EXISTS "${fixture_dir}")
    message(FATAL_ERROR "Refusing to reuse an existing fixture directory: ${fixture_dir}")
endif()
file(MAKE_DIRECTORY "${fixture_dir}")
set(first_path "${fixture_dir}/first ' \" 日本語 <b>; line\n.txt")
set(second_path "${fixture_dir}/second path.txt")
file(WRITE "${first_path}" "test-owned first fixture\n")
file(WRITE "${second_path}" "test-owned second fixture\n")

function(run_cli case_name expected_status)
    # Expanding ARGN as a CMake list loses empty arguments and splits semicolons.
    # Explicit quoted argv slots preserve both, without involving a shell.
    if(ARGC EQUAL 2)
        execute_process(COMMAND "${PEEK_EXECUTABLE}"
            WORKING_DIRECTORY "${fixture_dir}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
            TIMEOUT 5 ENCODING UTF-8
        )
    elseif(ARGC EQUAL 3)
        execute_process(COMMAND "${PEEK_EXECUTABLE}" "${ARGV2}"
            WORKING_DIRECTORY "${fixture_dir}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
            TIMEOUT 5 ENCODING UTF-8
        )
    elseif(ARGC EQUAL 4)
        execute_process(COMMAND "${PEEK_EXECUTABLE}" "${ARGV2}" "${ARGV3}"
            WORKING_DIRECTORY "${fixture_dir}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
            TIMEOUT 5 ENCODING UTF-8
        )
    else()
        message(FATAL_ERROR "${case_name}: unsupported number of test arguments")
    endif()
    if(NOT "${status}" STREQUAL "${expected_status}")
        message(FATAL_ERROR
            "${case_name}: expected exit ${expected_status}, got '${status}'\n"
            "stdout=[${output}]\nstderr=[${error}]\nfixtures=${fixture_dir}"
        )
    endif()
    set(CLI_STDOUT "${output}" PARENT_SCOPE)
    set(CLI_STDERR "${error}" PARENT_SCOPE)
endfunction()

function(expect_stderr case_name expected)
    if(NOT "${CLI_STDERR}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${case_name}: stderr mismatch\nexpected=[${expected}]\nactual=[${CLI_STDERR}]"
        )
    endif()
endfunction()

function(expect_output case_name expected_stdout expected_stderr)
    if(NOT "${CLI_STDOUT}" STREQUAL "${expected_stdout}")
        message(FATAL_ERROR
            "${case_name}: stdout mismatch\nexpected=[${expected_stdout}]\nactual=[${CLI_STDOUT}]"
        )
    endif()
    expect_stderr("${case_name}" "${expected_stderr}")
endfunction()

function(expect_contains case_name actual required)
    string(FIND "${actual}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${case_name}: expected '${required}' in [${actual}]")
    endif()
endfunction()

run_cli("help" 0 "--help")
expect_stderr("help" "")
expect_contains("help" "${CLI_STDOUT}" "Usage:")
expect_contains("help" "${CLI_STDOUT}" "path")
expect_contains("help" "${CLI_STDOUT}" "image preview")

run_cli("version" 0 "--version")
if(NOT CLI_STDOUT MATCHES "^peek [0-9]+\\.[0-9]+\\.[0-9]+\n$")
    message(FATAL_ERROR "version: unexpected stdout [${CLI_STDOUT}]")
endif()
expect_output("version" "peek ${PEEK_VERSION}\n" "")

run_cli("no arguments" 0)
expect_stderr("no arguments" "")
if(NOT CLI_STDOUT MATCHES "Peek [0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "no arguments: expected Peek semantic version in [${CLI_STDOUT}]")
endif()
expect_contains("no arguments" "${CLI_STDOUT}" "Peek ${PEEK_VERSION}")
expect_contains("no arguments" "${CLI_STDOUT}" "Usage: peek [options] PATH")
string(FIND "${CLI_STDOUT}" "Options:" full_help)
if(NOT full_help EQUAL -1)
    message(FATAL_ERROR "no arguments: full help must not be appended [${CLI_STDOUT}]")
endif()

run_cli("empty path" 2 "")
expect_output("empty path" "" "peek: empty path\n")

run_cli("multiple paths" 2 "${first_path}" "${second_path}")
expect_output("multiple paths" "" "peek: expected exactly one path\n")

run_cli("unknown option" 2 "--peek-unknown-option")
if(NOT "${CLI_STDOUT}" STREQUAL "")
    message(FATAL_ERROR "unknown option: expected empty stdout, got [${CLI_STDOUT}]")
endif()
if(NOT CLI_STDERR MATCHES "^peek: [^\n]+\n$")
    message(FATAL_ERROR "unknown option: expected one prefixed error line, got [${CLI_STDERR}]")
endif()
expect_contains("unknown option" "${CLI_STDERR}" "peek-unknown-option")

set(missing_absolute "${fixture_dir}/missing absolute path.txt")
run_cli("missing absolute path" 1 "${missing_absolute}")
expect_output("missing absolute path" ""
    "peek: path does not exist or cannot be accessed: ${missing_absolute}\n")

# Relative diagnostics prefix the working directory without cleaning the operand.
set(missing_dash "-missing path; 日本語\n.txt")
run_cli("missing leading-dash path after --" 1 "--" "${missing_dash}")
expect_output("missing leading-dash path after --" ""
    "peek: path does not exist or cannot be accessed: ${fixture_dir}/${missing_dash}\n")

set(missing_relative "relative missing ' \" 日本語 <b>; line\n.txt")
run_cli("missing relative path" 1 "${missing_relative}")
expect_output("missing relative path" ""
    "peek: path does not exist or cannot be accessed: ${fixture_dir}/${missing_relative}\n")

set(missing_dot_relative "./${missing_relative}")
run_cli("missing relative path with dot" 1 "${missing_dot_relative}")
expect_output("missing relative path with dot" ""
    "peek: path does not exist or cannot be accessed: ${fixture_dir}/${missing_dot_relative}\n")

set(missing_parent_relative "../peek-cli-${fixture_id}/${missing_relative}")
run_cli("missing relative path with parent traversal" 1 "${missing_parent_relative}")
expect_output("missing relative path with parent traversal" ""
    "peek: path does not exist or cannot be accessed: ${fixture_dir}/${missing_parent_relative}\n")

file(REMOVE_RECURSE "${fixture_dir}")
