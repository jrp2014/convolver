# SPDX-License-Identifier: GPL-2.0-or-later
# Optional distribution, separate from the C++ library package and exports.
# The package ships the CLI (PLUG-5) and the shared tools (PLUG-3 presets, HOST-4
# Equalizer APO export). convolver-vst3-preset stays a source-build alias.
if(NOT TARGET convolver_vst3 OR NOT TARGET convolver_cli OR NOT TARGET convolver_preset_tool
   OR NOT TARGET convolver_equalizer_apo)
    message(FATAL_ERROR "CONVOLVER_PACKAGE_PLUGINS requires VST3, I/O and the CLI tools "
        "(CONVOLVER_BUILD_VST3, CONVOLVER_BUILD_IO and CONVOLVER_BUILD_CLI)")
endif()
set(convolver_packaged_tools convolver_cli convolver_preset_tool convolver_equalizer_apo)
# CONVOLVER_DEBUG_SYMBOLS (the VST3 bundle's own CMakeLists keeps its). The
# CLI's symbols are convolver-cli.*, apart from the plug-in's Convolver.*.
convolver_keep_symbols(convolver_cli NAME convolver-cli)
foreach(tool IN ITEMS convolver_preset_tool convolver_equalizer_apo)
    convolver_keep_symbols(${tool})
endforeach()
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
get_target_property(convolver_bundle convolver_vst3 SMTG_PLUGIN_PACKAGE_PATH)
if(APPLE)
    # TARGET_BUNDLE_DIR follows the actual configuration under Ninja Multi-Config too.
    set(convolver_bundle "$<TARGET_BUNDLE_DIR:convolver_vst3>")
endif()
set_target_properties(convolver_vst3 ${convolver_packaged_tools} PROPERTIES
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
install(TARGETS ${convolver_packaged_tools} RUNTIME DESTINATION bin COMPONENT PluginDistribution)
set(examples_dir "${CMAKE_CURRENT_BINARY_DIR}/plugin-presets/$<CONFIG>")
add_custom_command(OUTPUT "${examples_dir}/examples.json"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/release/generate_examples.py"
        --output "${examples_dir}" --converter "$<TARGET_FILE:convolver_preset_tool>"
    DEPENDS convolver_preset_tool "${PROJECT_SOURCE_DIR}/scripts/release/generate_examples.py"
        "${PROJECT_SOURCE_DIR}/tests/plugins/fixtures.py"
    VERBATIM)
add_custom_target(convolver_plugin_examples ALL DEPENDS "${examples_dir}/examples.json")
install(DIRECTORY "${examples_dir}/" DESTINATION presets COMPONENT PluginDistribution)
install(FILES "${PROJECT_SOURCE_DIR}/examples/plugin-presets/README.md" DESTINATION presets COMPONENT PluginDistribution)
# The first-use examples (DOC-1) for the user journeys (REL-2): each folder's
# filter, input.wav and expected.wav, plus a preset made from its filter. Their
# README describes a source build, so the guides in docs/use/ take its place.
set(first_use_source "${PROJECT_SOURCE_DIR}/examples/first-use")
set(first_use_presets "${CMAKE_CURRENT_BINARY_DIR}/first-use-presets/$<CONFIG>")
add_custom_command(OUTPUT "${first_use_presets}/stamp"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${first_use_presets}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${first_use_presets}/mono" "${first_use_presets}/stereo"
        "${first_use_presets}/crossover"
    COMMAND convolver_preset_tool "${first_use_source}/mono/impulse.wav" "${first_use_presets}/mono/filter.vstpreset"
    COMMAND convolver_preset_tool "${first_use_source}/stereo/filter.cfg" "${first_use_presets}/stereo/filter.vstpreset"
    COMMAND convolver_preset_tool "${first_use_source}/crossover/filter.cfg"
        "${first_use_presets}/crossover/filter.vstpreset"
    COMMAND "${CMAKE_COMMAND}" -E touch "${first_use_presets}/stamp"
    DEPENDS convolver_preset_tool "${first_use_source}/mono/impulse.wav" "${first_use_source}/stereo/filter.cfg"
        "${first_use_source}/stereo/room.wav" "${first_use_source}/crossover/filter.cfg"
        "${first_use_source}/crossover/crossover.wav"
    VERBATIM)
add_custom_target(convolver_first_use_presets ALL DEPENDS "${first_use_presets}/stamp")
install(DIRECTORY "${first_use_source}/" DESTINATION examples/first-use COMPONENT PluginDistribution
    PATTERN README.md EXCLUDE)
install(DIRECTORY "${first_use_presets}/" DESTINATION examples/first-use COMPONENT PluginDistribution
    PATTERN stamp EXCLUDE)
# Every user page (docs/use/), with links to other pages pointing at the source
# repository's copy at the packaged commit.
file(GLOB guide_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/use/*.md")
list(SORT guide_sources)
set(packaged_guides)
foreach(guide IN LISTS guide_sources)
    file(RELATIVE_PATH guide_path "${PROJECT_SOURCE_DIR}" "${guide}")
    list(APPEND packaged_guides "${guide_path}")
endforeach()
set(guides_dir "${CMAKE_CURRENT_BINARY_DIR}/packaged-docs")
add_custom_command(OUTPUT "${guides_dir}.stamp"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${guides_dir}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/release/package_docs.py"
        --source "${PROJECT_SOURCE_DIR}" --output "${guides_dir}" --version "${PROJECT_VERSION}"
        --revision "${source_commit}" --stamp "${guides_dir}.stamp"
        ${packaged_guides}
    DEPENDS ${guide_sources} "${PROJECT_SOURCE_DIR}/scripts/release/package_docs.py"
    VERBATIM)
add_custom_target(convolver_packaged_guides ALL DEPENDS "${guides_dir}.stamp")
install(DIRECTORY "${guides_dir}/" DESTINATION docs COMPONENT PluginDistribution)
install(FILES "${PROJECT_SOURCE_DIR}/COPYING" DESTINATION licenses RENAME Convolver.txt COMPONENT PluginDistribution)
if(fft_backend STREQUAL "OOURA")
    # The vendored Ooura FFT's notice and provenance, which quote its licence.
    install(FILES "${PROJECT_SOURCE_DIR}/portable/third_party/ooura/README.md" DESTINATION licenses
        RENAME Ooura.txt COMPONENT PluginDistribution)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugin-VERSION.txt" "Convolver ${PROJECT_VERSION}\nDeveloper preview; production qualification incomplete.\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugin-VERSION.txt" DESTINATION . RENAME VERSION.txt COMPONENT PluginDistribution)
# Relocation is mandatory for release archives, tested with actual SDK tools.
if(CONVOLVER_BUILD_TESTS)
    foreach(release_test IN ITEMS manifest source artifact installer gate docs symbols)
        add_test(NAME release.${release_test} COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/release/${release_test}_tests.py")
    endforeach()
    set(validator_argument)
    # As plugins/vst3: the SDK's validator cannot load an instrumented bundle.
    if(CONVOLVER_VST3_RUNTIME_VALIDATION AND NOT CONVOLVER_SANITIZERS)
        set(validator_argument --validator "$<TARGET_FILE:validator>")
    endif()
    add_test(NAME release.stage COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/release/staging.py" --build "${CMAKE_BINARY_DIR}"
        --cmake "${CMAKE_COMMAND}" --config "$<CONFIG>"
        ${validator_argument} --probe "$<TARGET_FILE:convolver_vst3_package_probe>")
    set_tests_properties(release.stage PROPERTIES LABELS "packaging;vst3;real-libraries" TIMEOUT 180)
    add_dependencies(check convolver_plugin_examples convolver_first_use_presets convolver_vst3_package_probe)
    # release.stage carries the vst3 label and installs the whole distribution.
    add_dependencies(convolver_vst3_suite convolver_vst3_package_probe convolver_plugin_examples
        convolver_first_use_presets convolver_packaged_guides ${convolver_packaged_tools})
endif()
set(CPACK_PACKAGE_NAME Convolver)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Convolver-${PROJECT_VERSION}-preview-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_COMPONENTS_ALL PluginDistribution)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)
