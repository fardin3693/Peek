cmake_minimum_required(VERSION 3.21)

project(Peek
    VERSION 0.0.1
    DESCRIPTION "Lightweight Quick Look-style file previewer for Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release for single-config generators when nothing is specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PEEK_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PEEK_BUILD_TESTS "Build and register tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

add_executable(peek src/main.cpp)

target_compile_definitions(peek PRIVATE PEEK_VERSION="${PROJECT_VERSION}")
target_link_libraries(peek PRIVATE Qt6::Widgets)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(peek PRIVATE -Wall -Wextra -Wpedantic)
    if(PEEK_ENABLE_SANITIZERS)
        target_compile_options(peek PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(peek PRIVATE -fsanitize=address,undefined)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS peek RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(PEEK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
