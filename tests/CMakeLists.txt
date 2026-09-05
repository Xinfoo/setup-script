add_executable(
    arch-install-builder-model-tests
    test_model.c
    "${PROJECT_SOURCE_DIR}/src/model.c"
    "${PROJECT_SOURCE_DIR}/src/util.c"
)

add_executable(
    arch-install-builder-generator-tests
    test_generator.c
    "${PROJECT_SOURCE_DIR}/src/generator.c"
    "${PROJECT_SOURCE_DIR}/src/model.c"
    "${PROJECT_SOURCE_DIR}/src/util.c"
)

target_include_directories(
    arch-install-builder-model-tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)

target_include_directories(
    arch-install-builder-generator-tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    arch-install-builder-model-tests
    PRIVATE
        PkgConfig::JSONC
)

target_link_libraries(
    arch-install-builder-generator-tests
    PRIVATE
        PkgConfig::JSONC
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        arch-install-builder-model-tests
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

    target_compile_options(
        arch-install-builder-generator-tests
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
        target_compile_options(arch-install-builder-model-tests PRIVATE -Werror)
        target_compile_options(arch-install-builder-generator-tests PRIVATE -Werror)
    endif()

    if(ENABLE_SANITIZERS)
        target_compile_options(
            arch-install-builder-model-tests
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
                $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        target_link_options(
            arch-install-builder-model-tests
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
        target_compile_options(
            arch-install-builder-generator-tests
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
                $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        target_link_options(
            arch-install-builder-generator-tests
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
    endif()
endif()

add_test(NAME model COMMAND arch-install-builder-model-tests)
add_test(NAME generator-integration COMMAND arch-install-builder-generator-tests)

# LeakSanitizer cannot inspect processes in some sandboxed/debugger environments.
# AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
if(ENABLE_SANITIZERS)
    set_tests_properties(model generator-integration
                         PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
