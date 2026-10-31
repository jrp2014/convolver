# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)
set(CONVOLVER_VST3_SDK "" CACHE PATH "VST3 SDK source directory with submodules (docs/develop/build.md)")
set(CONVOLVER_PLUGINVAL "" CACHE FILEPATH "Optional pluginval executable for host-compatibility tests (the acceptance test needs pluginval 2)")
set(CONVOLVER_PLUGINVAL_ARGS "" CACHE STRING "Extra options for pluginval validate (a CMake list)")
if(NOT EXISTS "${CONVOLVER_VST3_SDK}/public.sdk/source/vst/vstaudioeffect.h")
    message(FATAL_ERROR "Set CONVOLVER_VST3_SDK to the VST3 SDK with its base, cmake, pluginterfaces and "
        "public.sdk submodules; scripts/release/sources.py --latest --fetch ... unpacks the newest release "
        "(docs/develop/build.md#supporting-libraries)")
endif()
# The SDK's Windows host tools use LoadLibraryW, which does not search the
# plug-in's directory for dependent DLLs. Keep the plug-in independent of the
# host executable's DLL search path; retain the ordinary dynamic MSVC runtime.
if(WIN32 AND TARGET FFTW3::fftw3f)
    get_target_property(fftw_type FFTW3::fftw3f TYPE)
    if(fftw_type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Windows VST3 builds need static FFTW; use vcpkg's x64-windows-static-md triplet")
    endif()
endif()
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
option(CONVOLVER_VST3_RUNTIME_VALIDATION "Run SDK tools that load plug-in bundles (disable when local policy rejects ad-hoc code)" ON)
# The SDK's tools are not instrumented, so they cannot load a bundle built
# with CONVOLVER_SANITIZERS (it links the instrumented core; its Linux build
# stopped at moduleinfotool). The cached option keeps its value for later
# builds without sanitizers; cmake/ConvolverPluginPackaging.cmake makes the
# same test for release.stage's validator.
if(CONVOLVER_SANITIZERS AND CONVOLVER_VST3_RUNTIME_VALIDATION)
    message(STATUS "CONVOLVER_SANITIZERS: the SDK tools cannot load an instrumented bundle, "
        "so they do not run in this build (CONVOLVER_VST3_RUNTIME_VALIDATION is ignored)")
    set(CONVOLVER_VST3_RUNTIME_VALIDATION OFF)
endif()
set(SMTG_CREATE_MODULE_INFO ${CONVOLVER_VST3_RUNTIME_VALIDATION} CACHE BOOL "" FORCE)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target" FORCE)
endif()
# SDK 3.8.1 assumes non-Xcode Unix generators are single-configuration.
# A generator expression prevents CMake appending a second config directory and
# keeps SDK resource/signing paths aligned with the actual bundle for Ninja Multi-Config.
if(CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
    set(CMAKE_BUILD_TYPE "$<CONFIG>")
endif()
add_subdirectory("${CONVOLVER_VST3_SDK}" sdk EXCLUDE_FROM_ALL)
# SDK Windows tools receive UTF-8 arguments but construct narrow filesystem paths.
# Give those tools a UTF-8 process code page so Unicode bundle paths round-trip.
# This changes only our test tools, not the plug-in or the user's host process.
if(WIN32)
    foreach(sdk_tool IN ITEMS validator moduleinfotool)
        if(TARGET ${sdk_tool})
            target_link_options(${sdk_tool} PRIVATE
                "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/tests/vst3/windows-utf8.manifest")
        endif()
    endforeach()
endif()
set(SDK_ROOT "${CONVOLVER_VST3_SDK}")
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
smtg_detect_xcode_version()
# The exact SDK, for the Convolver log (CI-6).
include("${PROJECT_SOURCE_DIR}/cmake/ConvolverSdkDescription.cmake")
convolver_sdk_description("VST3 SDK" "${CONVOLVER_VST3_SDK}")
# SDK entry-point/package helpers are loaded by its project.
# CONVOLVER_DEBUG_SYMBOLS on Linux: the SDK's Release strip keeps the symbols
# (cmake/ConvolverDebugSymbols.cmake).
if(CONVOLVER_SDK_STRIP)
    set(convolver_strip "${CMAKE_STRIP}")
    set(CMAKE_STRIP "${CONVOLVER_SDK_STRIP}")
endif()
smtg_add_vst3plugin(convolver_vst3 PACKAGE_NAME Convolver SOURCES_LIST processor.cpp factory.cpp)
if(CONVOLVER_SDK_STRIP)
    set(CMAKE_STRIP "${convolver_strip}")
endif()
target_include_directories(convolver_vst3 PRIVATE "${SDK_ROOT}" "${PROJECT_SOURCE_DIR}/portable")
target_link_libraries(convolver_vst3 PRIVATE Convolver::host sdk)
add_dependencies(convolver_vst3 convolver_build_version)
target_include_directories(convolver_vst3 PRIVATE "${Convolver_BINARY_DIR}/generated")
smtg_target_configure_version_file(convolver_vst3)
if(APPLE)
    target_link_libraries(convolver_vst3 PRIVATE "-framework Cocoa")
    set_target_properties(convolver_vst3 PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
endif()
if(CONVOLVER_BUILD_TESTS)
    add_executable(convolver_vst3_tests "${PROJECT_SOURCE_DIR}/tests/vst3/adapter_tests.cpp"
        processor.cpp "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_include_directories(convolver_vst3_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SDK_ROOT}" "${PROJECT_SOURCE_DIR}/portable"
        "${Convolver_BINARY_DIR}/generated")
    target_link_libraries(convolver_vst3_tests PRIVATE Convolver::host sdk sdk_hosting)
    if(APPLE)
        target_link_libraries(convolver_vst3_tests PRIVATE "-framework Cocoa")
    endif()
    add_dependencies(convolver_vst3 validator)
    add_test(NAME vst3.adapter COMMAND convolver_vst3_tests)
    get_target_property(bundle convolver_vst3 SMTG_PLUGIN_PACKAGE_PATH)
    if(APPLE)
        set(bundle "$<TARGET_BUNDLE_DIR:convolver_vst3>")
        add_test(NAME vst3.signature COMMAND codesign --verify --deep --strict "${bundle}")
        set_tests_properties(vst3.signature PROPERTIES LABELS "vst3;real-libraries")
    endif()
    if(CONVOLVER_VST3_RUNTIME_VALIDATION)
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
        add_test(NAME vst3.validator COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/vst3/validate_bundle.py" "$<TARGET_FILE:validator>" "${bundle}")
        # Extensive mode adds every pairing of 15 speaker arrangements and more automation patterns.
        add_test(NAME vst3.validator.extensive COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/vst3/validate_bundle.py" "$<TARGET_FILE:validator>" -e "${bundle}")
        set_tests_properties(vst3.validator vst3.validator.extensive
            PROPERTIES LABELS "vst3;real-libraries" TIMEOUT 180)
    else()
        message(STATUS "VST3 runtime validation and moduleinfo loading disabled: developer build only")
    endif()
    set_tests_properties(vst3.adapter PROPERTIES LABELS "vst3;real-libraries" TIMEOUT 180)
    # PLUG-1: the plug-in must not share an FFTW planner with its host process.
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME vst3.fft_symbols COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/vst3/fft_symbols.py" "${fft_backend}" "$<TARGET_FILE:convolver_vst3>")
    set_tests_properties(vst3.fft_symbols PROPERTIES LABELS "vst3;real-libraries" SKIP_RETURN_CODE 77)
    # PLUG-4: the bundle's class IDs, vendor and bundle identifier come from the registry.
    set(identity_options "")
    if(CONVOLVER_VST3_RUNTIME_VALIDATION)
        set(identity_options --require-moduleinfo)
    endif()
    add_test(NAME vst3.identities COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/vst3/identities.py" "${PROJECT_SOURCE_DIR}/plugins/identities.json"
        "${SDK_ROOT}" "${bundle}" ${identity_options})
    set_tests_properties(vst3.identities PROPERTIES LABELS "vst3;real-libraries")
    add_dependencies(check convolver_vst3 convolver_vst3_tests validator)
endif()

if(CONVOLVER_BUILD_IO)
    # The original VST3-only tool, kept for existing scripts: the shared preset
    # tool (PLUG-3) with the old arguments and messages. It needs no SDK.
    add_executable(convolver_vst3_preset "${PROJECT_SOURCE_DIR}/portable/preset_main.cpp")
    set_target_properties(convolver_vst3_preset PROPERTIES OUTPUT_NAME convolver-vst3-preset)
    target_compile_definitions(convolver_vst3_preset PRIVATE CONVOLVER_VST3_PRESET_ALIAS)
    target_link_libraries(convolver_vst3_preset PRIVATE convolver_presets convolver_filter_file
        Convolver::io)
    if(MINGW)
        target_link_options(convolver_vst3_preset PRIVATE -municode)
    endif()
    if(CONVOLVER_BUILD_TESTS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        # The SDK-free writer against Steinberg's PresetFile, both directions.
        add_executable(convolver_vst3_preset_format "${PROJECT_SOURCE_DIR}/tests/vst3/preset_format_tests.cpp"
            processor.cpp "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
        target_include_directories(convolver_vst3_preset_format PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SDK_ROOT}")
        target_link_libraries(convolver_vst3_preset_format PRIVATE convolver_presets sdk)
        if(APPLE)
            target_link_libraries(convolver_vst3_preset_format PRIVATE "-framework Cocoa")
        endif()
        add_test(NAME vst3.preset_format COMMAND convolver_vst3_preset_format)
        set_tests_properties(vst3.preset_format PROPERTIES LABELS "vst3;real-libraries")
        add_dependencies(check convolver_vst3_preset_format)
        set(shared_preset_tool "")
        if(TARGET convolver_preset_tool)
            set(shared_preset_tool "$<TARGET_FILE:convolver_preset_tool>")
        endif()
        add_test(NAME vst3.preset COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/vst3/preset_tests.py"
            "$<TARGET_FILE:convolver_vst3_preset>" "$<TARGET_FILE:convolver_vst3_tests>" ${shared_preset_tool})
        set_tests_properties(vst3.preset PROPERTIES LABELS "vst3;real-libraries" TIMEOUT 120)
        # PLUG-2: the shared harness, comparing with the oracle and the CLI renders
        # that examples.first_use leaves behind.
        if(TARGET convolver_cli AND CONVOLVER_BUILD_NATIVE)
            add_executable(convolver_vst3_harness "${PROJECT_SOURCE_DIR}/tests/vst3/harness_tests.cpp"
                "${PROJECT_SOURCE_DIR}/tests/plugins/harness.cpp" processor.cpp
                "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
            target_include_directories(convolver_vst3_harness PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                "${SDK_ROOT}" "${PROJECT_SOURCE_DIR}/portable" "${Convolver_BINARY_DIR}/generated")
            target_link_libraries(convolver_vst3_harness PRIVATE Convolver::host Convolver::io sdk sdk_hosting)
            if(APPLE)
                target_link_libraries(convolver_vst3_harness PRIVATE "-framework Cocoa")
            endif()
            add_test(NAME vst3.harness COMMAND convolver_vst3_harness
                "${PROJECT_BINARY_DIR}/first-use/fixtures.txt")
            set_tests_properties(vst3.harness PROPERTIES LABELS "vst3;real-libraries;plug2"
                TIMEOUT 300 FIXTURES_REQUIRED first_use_renders)
            add_dependencies(check convolver_vst3_harness)
            # PLUG-7: lifecycle sequences against the reference model.
            add_executable(convolver_vst3_lifecycle "${PROJECT_SOURCE_DIR}/tests/vst3/lifecycle_tests.cpp"
                "${PROJECT_SOURCE_DIR}/tests/plugins/lifecycle.cpp"
                "${PROJECT_SOURCE_DIR}/tests/plugins/audio_guard.cpp" processor.cpp
                "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
            target_include_directories(convolver_vst3_lifecycle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                "${SDK_ROOT}" "${PROJECT_SOURCE_DIR}/portable" "${Convolver_BINARY_DIR}/generated")
            target_link_libraries(convolver_vst3_lifecycle PRIVATE Convolver::host sdk sdk_hosting
                ${CMAKE_DL_LIBS})
            if(APPLE)
                target_link_libraries(convolver_vst3_lifecycle PRIVATE "-framework Cocoa")
            endif()
            add_test(NAME vst3.lifecycle COMMAND convolver_vst3_lifecycle ${CONVOLVER_LIFECYCLE_LENGTH})
            # COST: about 100 s on Windows CI, so ctest --parallel starts it first (CI-9).
            set_tests_properties(vst3.lifecycle PROPERTIES LABELS "vst3;real-libraries;plug7" TIMEOUT 300
                COST 100)
            add_dependencies(check convolver_vst3_lifecycle)
        endif()
        add_dependencies(check convolver_vst3_preset)
        # Tracktion pluginval: an independent (JUCE) host, downloaded separately.
        if(CONVOLVER_PLUGINVAL)
            add_test(NAME vst3.pluginval COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/vst3/pluginval_tests.py" validate
                "${CONVOLVER_PLUGINVAL}" "${bundle}" ${CONVOLVER_PLUGINVAL_ARGS})
            add_test(NAME vst3.pluginval.acceptance COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/vst3/pluginval_tests.py" acceptance
                "${CONVOLVER_PLUGINVAL}" "${bundle}" "$<TARGET_FILE:convolver_vst3_preset>")
            set_tests_properties(vst3.pluginval vst3.pluginval.acceptance
                PROPERTIES LABELS "vst3;real-libraries;pluginval" TIMEOUT 600)
            # pluginval 1 has no `test` command, so the acceptance test skips itself.
            set_tests_properties(vst3.pluginval.acceptance PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()
endif()

# CI-9: everything the vst3.* tests and their examples.first_use fixture run,
# so a job that runs only `ctest -L vst3` can build this rather than every test
# (as convolver_lv2_tests does for LV2). A test whose program is missing here
# fails (CTest cannot find it, or its script reports it), so add a new test's target.
if(CONVOLVER_BUILD_TESTS)
    add_custom_target(convolver_vst3_suite)
    add_dependencies(convolver_vst3_suite convolver_vst3 validator convolver_vst3_tests)
    foreach(target IN ITEMS convolver_vst3_preset convolver_vst3_preset_format convolver_vst3_harness
            convolver_vst3_lifecycle convolver_preset_tool convolver_cli)
        if(TARGET ${target})
            add_dependencies(convolver_vst3_suite ${target})
        endif()
    endforeach()
endif()

if(CONVOLVER_BUILD_TESTS AND CONVOLVER_PACKAGE_PLUGINS)
    add_executable(convolver_vst3_package_probe "${PROJECT_SOURCE_DIR}/tests/vst3/package_probe.cpp"
        processor.cpp "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
    target_include_directories(convolver_vst3_package_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SDK_ROOT}" "${PROJECT_SOURCE_DIR}/portable"
        "${Convolver_BINARY_DIR}/generated")
    target_link_libraries(convolver_vst3_package_probe PRIVATE Convolver::host sdk sdk_hosting)
    if(APPLE)
        target_link_libraries(convolver_vst3_package_probe PRIVATE "-framework Cocoa")
    endif()
endif()

# Preview candidates keep the plug-in's debugging symbols outside the bundle
# (CONVOLVER_DEBUG_SYMBOLS), before the bundle is signed below.
convolver_keep_symbols(convolver_vst3 SDK_STRIPPED)
if(APPLE)
    # SDK 3.8.1 seals the bundle before moduleinfotool adds moduleinfo.json.
    # Seal the complete resources last, and verify without loading the bundle.
    add_custom_command(TARGET convolver_vst3 POST_BUILD
        COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:convolver_vst3>"
        COMMAND codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:convolver_vst3>"
        VERBATIM)
endif()

# DIAG-1: tests write the Convolver log in the build folder, never the user's.
get_property(convolver_vst3_tests_here DIRECTORY PROPERTY TESTS)
if(convolver_vst3_tests_here)
    set_property(TEST ${convolver_vst3_tests_here} APPEND PROPERTY ENVIRONMENT
        "CONVOLVER_LOG=${CMAKE_CURRENT_BINARY_DIR}/convolver-tests.log")
endif()

# The adapter as each test compiles it, and the tests. The bundle's own
# sources and the SDK's libraries are not compiled with the sanitizers: the
# SDK's tools (validator, moduleinfotool) link those libraries without the
# runtimes. The bundle still links the instrumented core.
convolver_sanitize(convolver_vst3_tests convolver_vst3_preset
    convolver_vst3_preset_format convolver_vst3_harness convolver_vst3_lifecycle
    convolver_vst3_package_probe)
