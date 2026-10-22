# SPDX-License-Identifier: GPL-2.0-or-later
# The LV2 bundle (LV2-1): BUILD/lv2/convolver.lv2/ holds the plug-in library,
# manifest.ttl and convolver.ttl, generated from plugins/identities.json.
# Point LV2_PATH at BUILD/lv2 to try it in a host.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CONVOLVER_LV2 REQUIRED IMPORTED_TARGET lv2>=1.18)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

set(lv2_bundle_dir "${PROJECT_BINARY_DIR}/lv2/${CONVOLVER_ID_LV2_BUNDLE}")
set(lv2_binary "convolver${CMAKE_SHARED_MODULE_SUFFIX}")
add_library(convolver_lv2 MODULE plugin.cpp)
target_include_directories(convolver_lv2 PRIVATE "${PROJECT_SOURCE_DIR}/portable"
    "${Convolver_BINARY_DIR}/generated")
target_link_libraries(convolver_lv2 PRIVATE Convolver::host convolver_presets PkgConfig::CONVOLVER_LV2)
target_compile_features(convolver_lv2 PRIVATE cxx_std_20)
# "$<1:...>" keeps multi-configuration generators from adding a subfolder.
set_target_properties(convolver_lv2 PROPERTIES PREFIX "" OUTPUT_NAME convolver
    LIBRARY_OUTPUT_DIRECTORY "$<1:${lv2_bundle_dir}>" CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    target_compile_options(convolver_lv2 PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(convolver_lv2 PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Export lv2_descriptor and nothing else, so a host that also loads the VST3
# build, or another version of this one, cannot bind to our internal symbols.
if(APPLE)
    target_link_options(convolver_lv2 PRIVATE "LINKER:-exported_symbol,_lv2_descriptor")
elseif(UNIX)
    target_link_options(convolver_lv2 PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
    set_property(TARGET convolver_lv2 APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
endif()
if(CONVOLVER_SANITIZERS)
    target_compile_options(convolver_lv2 PRIVATE -fsanitize=address,undefined
        -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()

add_custom_command(
    OUTPUT "${lv2_bundle_dir}/manifest.ttl" "${lv2_bundle_dir}/convolver.ttl"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_ttl.py"
        "${PROJECT_SOURCE_DIR}/plugins/identities.json" "${PROJECT_SOURCE_DIR}/VERSION"
        "${lv2_binary}" "${lv2_bundle_dir}"
    DEPENDS generate_ttl.py "${PROJECT_SOURCE_DIR}/plugins/identities.json" "${PROJECT_SOURCE_DIR}/VERSION"
    COMMENT "Writing the LV2 bundle's Turtle files"
    VERBATIM)
add_custom_target(convolver_lv2_ttl DEPENDS "${lv2_bundle_dir}/manifest.ttl" "${lv2_bundle_dir}/convolver.ttl")
add_dependencies(convolver_lv2 convolver_lv2_ttl)

if(CONVOLVER_BUILD_TESTS)
    pkg_check_modules(CONVOLVER_LILV REQUIRED IMPORTED_TARGET lilv-0>=0.24)
    add_test(NAME lv2.exports COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/lv2/exports.py" "$<TARGET_FILE:convolver_lv2>")
    set_tests_properties(lv2.exports PROPERTIES LABELS "lv2;real-libraries" SKIP_RETURN_CODE 77)
    # PLUG-1: the plug-in must not share an FFTW planner with its host process.
    add_test(NAME lv2.fft_symbols COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/vst3/fft_symbols.py" "${fft_backend}" "$<TARGET_FILE:convolver_lv2>")
    set_tests_properties(lv2.fft_symbols PROPERTIES LABELS "lv2;real-libraries" SKIP_RETURN_CODE 77)
    add_dependencies(check convolver_lv2)
    if(CONVOLVER_BUILD_IO AND TARGET convolver_cli AND TARGET convolver_preset_tool)
        foreach(name adapter harness)
            set(sources "${PROJECT_SOURCE_DIR}/tests/lv2/${name}_tests.cpp" "${PROJECT_SOURCE_DIR}/tests/lv2/host.cpp")
            if(name STREQUAL "harness")
                list(APPEND sources "${PROJECT_SOURCE_DIR}/tests/plugins/harness.cpp")
            endif()
            add_executable(convolver_lv2_${name} ${sources})
            target_include_directories(convolver_lv2_${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                "${PROJECT_SOURCE_DIR}/portable" "${Convolver_BINARY_DIR}/generated")
            target_link_libraries(convolver_lv2_${name} PRIVATE Convolver::host Convolver::io
                convolver_presets PkgConfig::CONVOLVER_LILV PkgConfig::CONVOLVER_LV2)
            if(CONVOLVER_SANITIZERS)
                target_compile_options(convolver_lv2_${name} PRIVATE -fsanitize=address,undefined
                    -fno-sanitize-recover=all -fno-omit-frame-pointer)
            endif()
            add_dependencies(convolver_lv2_${name} convolver_lv2)
            add_dependencies(check convolver_lv2_${name})
        endforeach()
        # Ports, status, log messages, presets written by convolver-preset and
        # lilv's own state round trip.
        add_test(NAME lv2.adapter COMMAND convolver_lv2_adapter "${lv2_bundle_dir}"
            "${PROJECT_SOURCE_DIR}/examples/first-use" "$<TARGET_FILE:convolver_preset_tool>"
            "${PROJECT_BINARY_DIR}/lv2-test")
        set_tests_properties(lv2.adapter PROPERTIES LABELS "lv2;real-libraries" TIMEOUT 600)
        # PLUG-2: the shared harness, comparing with the oracle and the CLI renders
        # that examples.first_use leaves behind.
        add_test(NAME lv2.harness COMMAND convolver_lv2_harness "${lv2_bundle_dir}"
            "${PROJECT_BINARY_DIR}/first-use/fixtures.txt")
        # About three minutes under ASan; sanitizer builds need the margin.
        set_tests_properties(lv2.harness PROPERTIES LABELS "lv2;real-libraries;plug2"
            TIMEOUT 600 FIXTURES_REQUIRED first_use_renders)
    endif()
endif()
