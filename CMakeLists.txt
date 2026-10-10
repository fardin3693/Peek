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
option(PEEK_WITH_PDF "Enable PDF first-page preview via the Poppler C++ API" ON)

# Optional PDF backend: Poppler ships no CMake config, so probe it through
# pkg-config (a CMake module, not a new dependency). Absence only disables
# the PDF preview; the image-only application always configures.
set(PEEK_HAVE_POPPLER OFF)
if(PEEK_WITH_PDF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(POPPLER_CPP QUIET IMPORTED_TARGET poppler-cpp)
        if(POPPLER_CPP_FOUND)
            set(PEEK_HAVE_POPPLER ON)
        endif()
    endif()
    if(PEEK_HAVE_POPPLER)
        message(STATUS "Peek: PDF preview enabled (Poppler C++ ${POPPLER_CPP_VERSION})")
    else()
        message(STATUS "Peek: Poppler C++ development files not found,"
            " building image-only preview without PDF support")
    endif()
else()
    message(STATUS "Peek: PDF preview disabled by PEEK_WITH_PDF=OFF")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

add_executable(peek
    src/main.cpp
    src/preview_request.cpp
    src/preview_request.h
    src/preview_window.cpp
    src/preview_window.h
    src/text_preview.cpp
    src/text_preview.h
)

target_compile_definitions(peek PRIVATE PEEK_VERSION="${PROJECT_VERSION}")
target_link_libraries(peek PRIVATE Qt6::Widgets)

if(PEEK_HAVE_POPPLER)
    target_sources(peek PRIVATE src/pdf_preview.cpp src/pdf_preview.h)
    target_compile_definitions(peek PRIVATE PEEK_WITH_PDF)
    target_link_libraries(peek PRIVATE PkgConfig::POPPLER_CPP)
endif()

function(peek_configure_target target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(PEEK_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

peek_configure_target(peek)

include(GNUInstallDirs)
install(TARGETS peek RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(PEEK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
