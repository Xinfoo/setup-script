cmake_minimum_required(VERSION 3.20)

project(
    arch_install_builder
    VERSION 0.1.0
    DESCRIPTION "TTY builder for Arch Linux installation scripts"
    LANGUAGES C
)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer in Debug builds" OFF)
option(ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)

add_executable(
    arch-install-builder
    src/main.c
    src/util.c
    src/model.c
    src/packages.c
    src/detect.c
    src/ui.c
    src/generator.c
)

target_include_directories(
    arch-install-builder
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    arch-install-builder
    PRIVATE
        PkgConfig::NCURSESW
        PkgConfig::JSONC
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        arch-install-builder
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wstrict-prototypes
            -Wmissing-prototypes
    )

    if(ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(arch-install-builder PRIVATE -Werror)
    endif()

    if(ENABLE_SANITIZERS)
        target_compile_options(
            arch-install-builder
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
                $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        target_link_options(
            arch-install-builder
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
    endif()
elseif(MSVC)
    target_compile_options(arch-install-builder PRIVATE /W4)
    if(ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(arch-install-builder PRIVATE /WX)
    endif()
endif()

if(BUILD_TESTING AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

install(
    TARGETS arch-install-builder
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
