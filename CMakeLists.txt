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

set(
    GENERATOR_SOURCES
    "${PROJECT_SOURCE_DIR}/src/generator/generator.c"
    "${PROJECT_SOURCE_DIR}/src/generator/writer.c"
    "${PROJECT_SOURCE_DIR}/src/generator/plan.c"
    "${PROJECT_SOURCE_DIR}/src/generator/runtime.c"
    "${PROJECT_SOURCE_DIR}/src/generator/chroot.c"
    "${PROJECT_SOURCE_DIR}/src/generator/finish.c"
)

set(GENERATOR_TEMPLATE_OUTPUTS)
function(embed_generator_template output_name source_name)
    set(source "${PROJECT_SOURCE_DIR}/src/generator/templates/${source_name}.sh")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/generator/${output_name}.inc")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DINPUT=${source}"
            "-DOUTPUT=${output}"
            -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
        DEPENDS
            "${source}"
            "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
        COMMENT "Embedding generator template ${source_name}.sh"
        VERBATIM
    )
    list(APPEND GENERATOR_TEMPLATE_OUTPUTS "${output}")
    set(GENERATOR_TEMPLATE_OUTPUTS "${GENERATOR_TEMPLATE_OUTPUTS}" PARENT_SCOPE)
endfunction()

embed_generator_template(preamble preamble)
embed_generator_template(runtime_state runtime-state)
embed_generator_template(runtime_cleanup runtime-cleanup)
embed_generator_template(runtime_logging runtime-logging)
embed_generator_template(runtime_local_mirror runtime-local-mirror)
embed_generator_template(runtime_secure_boot_input runtime-secure-boot-input)
embed_generator_template(runtime_storage_validation runtime-storage-validation)
embed_generator_template(runtime_keep_probe runtime-keep-probe)
embed_generator_template(runtime_preflight runtime-preflight)
embed_generator_template(runtime_plan_confirmation runtime-plan-confirmation)
embed_generator_template(runtime_partitioning runtime-partitioning)
embed_generator_template(runtime_filesystems runtime-filesystems)
embed_generator_template(runtime_target_mirror runtime-target-mirror)
embed_generator_template(runtime_package_source runtime-package-source)
embed_generator_template(chroot_preamble chroot-preamble)
embed_generator_template(chroot_base chroot-base)
embed_generator_template(chroot_desktop chroot-desktop)
embed_generator_template(chroot_optional_software chroot-optional-software)
embed_generator_template(chroot_bootloader chroot-bootloader)
embed_generator_template(chroot_system chroot-system)
embed_generator_template(finish_secure_boot finish-secure-boot)
embed_generator_template(finish_target_package finish-target-package)
embed_generator_template(finish_firmware finish-firmware)
embed_generator_template(finish_main finish-main)

add_custom_target(
    arch-install-builder-generator-templates
    DEPENDS ${GENERATOR_TEMPLATE_OUTPUTS}
)

add_executable(
    arch-install-builder
    src/main.c
    src/util.c
    src/model.c
    src/packages.c
    src/detect.c
    src/ui/ui.c
    src/ui/dialogs.c
    src/ui/storage.c
    src/ui/screens.c
    ${GENERATOR_SOURCES}
)

add_dependencies(
    arch-install-builder
    arch-install-builder-generator-templates
)

target_include_directories(
    arch-install-builder
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}"
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
