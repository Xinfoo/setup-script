add_executable(
    arch-install-builder-model-tests
    "${PROJECT_SOURCE_DIR}/old/tests/test_model.c"
    ${MODEL_SOURCES}
    ${COMMON_SOURCES}
    "${PROJECT_SOURCE_DIR}/src/generator/shell.c"
)

add_executable(
    arch-install-builder-generator-tests
    "${PROJECT_SOURCE_DIR}/old/tests/test_generator.c"
    ${GENERATOR_SOURCES}
    ${PACKAGE_SOURCE}
    ${MODEL_SOURCES}
    ${COMMON_SOURCES}
)

add_dependencies(
    arch-install-builder-generator-tests
    arch-install-builder-generator-templates
)

add_executable(
    arch-install-builder-package-tests
    "${PROJECT_SOURCE_DIR}/old/tests/test_packages.c"
    ${PACKAGE_SOURCE}
    ${COMMON_SOURCES}
)

foreach(target
        arch-install-builder-model-tests
        arch-install-builder-generator-tests
        arch-install-builder-package-tests)
    target_include_directories(
        ${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_BINARY_DIR}"
    )
    target_link_libraries(${target} PRIVATE PkgConfig::JSONC)
    configure_project_target(${target})
endforeach()

add_test(NAME model COMMAND arch-install-builder-model-tests)
add_test(NAME generator-integration COMMAND arch-install-builder-generator-tests)
add_test(NAME packages COMMAND arch-install-builder-package-tests)

# 某些沙箱或调试器环境不允许 LeakSanitizer 检查进程；其余检查保持启用。
if(ENABLE_SANITIZERS)
    set_tests_properties(
        model generator-integration packages
        PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
endif()
