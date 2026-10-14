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

# Optional developer rebuild cache: uses ccache when present, never fails
# the configure when absent.
find_program(PEEK_CCACHE_PROGRAM ccache)
if(PEEK_CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${PEEK_CCACHE_PROGRAM})
endif()

# Default to Release for single-config generators when nothing is specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PEEK_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PEEK_BUILD_TESTS "Build and register tests" ON)
option(PEEK_WITH_PDF "Enable PDF first-page preview via the Poppler C++ API" ON)
option(PEEK_WITH_MULTIMEDIA "Enable video preview via Qt Multimedia" ON)

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

# Optional media backend: Qt Multimedia with the Widgets video output. Absence
# only disables the video/audio players; the extension dispatch stays compiled
# and media files fall back to an explicit unsupported message.
set(PEEK_HAVE_MULTIMEDIA OFF)
if(PEEK_WITH_MULTIMEDIA)
    find_package(Qt6 6.2 QUIET COMPONENTS Multimedia MultimediaWidgets)
    if(TARGET Qt6::Multimedia AND TARGET Qt6::MultimediaWidgets)
        set(PEEK_HAVE_MULTIMEDIA ON)
        message(STATUS "Peek: video and audio preview enabled (Qt Multimedia)")
    else()
        message(STATUS "Peek: Qt Multimedia development files not found,"
            " building without video and audio preview")
    endif()
else()
    message(STATUS "Peek: video and audio preview disabled by PEEK_WITH_MULTIMEDIA=OFF")
endif()

qt_standard_project_setup()

add_executable(peek
    src/main.cpp
    src/preview_request.cpp
    src/preview_request.h
    src/preview_util.h
    src/preview_window.cpp
    src/preview_window.h
    src/preview_provider.h
    src/text_preview.cpp
    src/text_preview.h
    src/text_provider.cpp
    src/text_provider.h
    src/markdown_preview.cpp
    src/markdown_preview.h
    src/markdown_provider.cpp
    src/markdown_provider.h
    src/image_provider.cpp
    src/image_provider.h
    src/video_preview.cpp
    src/video_preview.h
    src/audio_preview.cpp
    src/audio_preview.h
)

target_compile_definitions(peek PRIVATE PEEK_VERSION="${PROJECT_VERSION}")
target_link_libraries(peek PRIVATE Qt6::Widgets)

if(PEEK_HAVE_MULTIMEDIA)
    target_sources(peek PRIVATE src/audio_provider.cpp src/audio_provider.h)
    target_compile_definitions(peek PRIVATE PEEK_WITH_MULTIMEDIA)
    target_link_libraries(peek PRIVATE Qt6::Multimedia Qt6::MultimediaWidgets)
endif()

if(PEEK_HAVE_POPPLER)
    target_sources(peek PRIVATE src/pdf_preview.cpp src/pdf_preview.h
        src/pdf_provider.cpp src/pdf_provider.h)
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

# Optional static-analysis gate: `cmake --build --preset debug --target peek_tidy`
# runs clang-tidy over the application sources using the committed
# `.clang-tidy` config and the preset's compile_commands.json. Absence of the
# clang-tidy binary never fails the configure.
find_program(PEEK_CLANG_TIDY_PROGRAM clang-tidy)
if(PEEK_CLANG_TIDY_PROGRAM)
    add_custom_target(peek_tidy
        COMMAND ${PEEK_CLANG_TIDY_PROGRAM} -p ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-tidy on Peek sources"
        VERBATIM)
endif()

include(GNUInstallDirs)
install(TARGETS peek RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(PEEK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
