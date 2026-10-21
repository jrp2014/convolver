# SPDX-License-Identifier: GPL-2.0-or-later
# Optional distribution, separate from the C++ library package and exports.
if(NOT TARGET convolver_vst3_preset)
    message(FATAL_ERROR "CONVOLVER_PACKAGE_PLUGINS requires VST3 and I/O")
endif()
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
get_target_property(convolver_bundle convolver_vst3 SMTG_PLUGIN_PACKAGE_PATH)
if(APPLE)
    # TARGET_BUNDLE_DIR follows the actual configuration under Ninja Multi-Config too.
    set(convolver_bundle "$<TARGET_BUNDLE_DIR:convolver_vst3>")
endif()
set_target_properties(convolver_vst3 convolver_vst3_preset PROPERTIES
    SKIP_BUILD_RPATH TRUE INSTALL_RPATH "" INSTALL_RPATH_USE_LINK_PATH FALSE)
set(CONVOLVER_DISTRIBUTION_LICENSE_DIR "" CACHE PATH "Notices produced by build_dependencies.py")
if(CONVOLVER_DISTRIBUTION_LICENSE_DIR)
    install(DIRECTORY "${CONVOLVER_DISTRIBUTION_LICENSE_DIR}/" DESTINATION licenses COMPONENT PluginDistribution)
endif()
set(source_commit "unknown")
set(source_dirty false)
if(EXISTS "${PROJECT_SOURCE_DIR}/SOURCE_COMMIT")
    file(STRINGS "${PROJECT_SOURCE_DIR}/SOURCE_COMMIT" source_commit LIMIT_COUNT 1)
else()
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE source_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE source_status COMMAND_ERROR_IS_FATAL ANY)
        if(source_status)
            set(source_dirty true)
        endif()
    endif()
endif()
# Generated from the actual configure/build, never hand-copied into policy.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/release-build-info-$<CONFIG>.json" CONTENT
"{\n  \"version\": \"${PROJECT_VERSION}\",\n  \"source_dirty\": ${source_dirty},\n  \"source_commit\": \"${source_commit}\",\n  \"backend\": \"${fft_backend}\",\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\",\n  \"configuration\": \"$<CONFIG>\",\n  \"flags\": \"${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}\",\n  \"runtime\": \"${CMAKE_MSVC_RUNTIME_LIBRARY}\"\n}\n")
# The SDK supplies the whole bundle path, including configuration expressions.
install(DIRECTORY "${convolver_bundle}" DESTINATION VST3 COMPONENT PluginDistribution USE_SOURCE_PERMISSIONS)
install(TARGETS convolver_vst3_preset RUNTIME DESTINATION bin COMPONENT PluginDistribution)
set(examples_dir "${CMAKE_CURRENT_BINARY_DIR}/plugin-presets/$<CONFIG>")
add_custom_command(OUTPUT "${examples_dir}/examples.json"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/release/generate_examples.py"
        --output "${examples_dir}" --converter "$<TARGET_FILE:convolver_vst3_preset>"
    DEPENDS convolver_vst3_preset "${PROJECT_SOURCE_DIR}/scripts/release/generate_examples.py"
    VERBATIM)
add_custom_target(convolver_plugin_examples ALL DEPENDS "${examples_dir}/examples.json")
install(DIRECTORY "${examples_dir}/" DESTINATION presets COMPONENT PluginDistribution)
install(FILES "${PROJECT_SOURCE_DIR}/examples/plugin-presets/README.md" DESTINATION presets COMPONENT PluginDistribution)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/plugins" DESTINATION docs COMPONENT PluginDistribution)
install(FILES "${PROJECT_SOURCE_DIR}/COPYING" DESTINATION licenses RENAME Convolver.txt COMPONENT PluginDistribution)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugin-VERSION.txt" "Convolver ${PROJECT_VERSION}\nDeveloper preview; production qualification incomplete.\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugin-VERSION.txt" DESTINATION . RENAME VERSION.txt COMPONENT PluginDistribution)
# Relocation is mandatory for release archives, tested with actual SDK tools.
if(CONVOLVER_BUILD_TESTS)
    foreach(release_test IN ITEMS manifest source artifact installer gate)
        add_test(NAME release.${release_test} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/release/${release_test}_tests.py")
    endforeach()
    set(validator_argument)
    if(CONVOLVER_VST3_RUNTIME_VALIDATION)
        set(validator_argument --validator "$<TARGET_FILE:validator>")
    endif()
    add_test(NAME release.stage COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/release/staging.py" --build "${CMAKE_BINARY_DIR}"
        --cmake "${CMAKE_COMMAND}" --config "$<CONFIG>"
        ${validator_argument} --probe "$<TARGET_FILE:convolver_vst3_package_probe>")
    set_tests_properties(release.stage PROPERTIES LABELS "packaging;vst3;real-libraries" TIMEOUT 180)
    add_dependencies(check convolver_plugin_examples convolver_vst3_package_probe)
endif()
set(CPACK_PACKAGE_NAME Convolver)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Convolver-${PROJECT_VERSION}-preview-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_COMPONENTS_ALL PluginDistribution)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)
