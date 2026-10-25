# SPDX-License-Identifier: GPL-2.0-or-later
# The Audio Unit (AUv2 effect) adapter, Convolver.component, on Apple's
# AudioUnitSDK (Apache-2.0). The built component is distributed under
# GPL-3.0-or-later (owner's decision, 21 October 2026); see NOTICE.txt.
cmake_minimum_required(VERSION 3.20)
set(CONVOLVER_AU_SDK "" CACHE PATH "AudioUnitSDK source directory, the newest release (docs/develop/build.md)")
if(NOT EXISTS "${CONVOLVER_AU_SDK}/include/AudioUnitSDK/AUBase.h")
    message(FATAL_ERROR "Set CONVOLVER_AU_SDK to an AudioUnitSDK source directory; "
        "scripts/release/sources.py --latest --fetch ... audiounitsdk unpacks the newest release "
        "(docs/develop/build.md#supporting-libraries)")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # AudioUnitSDK supports macOS 11 and later.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target" FORCE)
endif()

# The SDK as published, compiled as C++23 (it needs std::expected).
file(GLOB ausdk_sources CONFIGURE_DEPENDS "${CONVOLVER_AU_SDK}/src/AudioUnitSDK/*.cpp")
add_library(convolver_ausdk STATIC ${ausdk_sources})
target_include_directories(convolver_ausdk SYSTEM PUBLIC "${CONVOLVER_AU_SDK}/include")
target_compile_features(convolver_ausdk PUBLIC cxx_std_23)
target_compile_options(convolver_ausdk PRIVATE -w)
set_target_properties(convolver_ausdk PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(convolver_ausdk PUBLIC "-framework AudioToolbox" "-framework CoreAudio"
    "-framework CoreFoundation" "-framework CoreMIDI")

# The exact SDK, for the Convolver log (CI-6).
include("${PROJECT_SOURCE_DIR}/cmake/ConvolverSdkDescription.cmake")
convolver_sdk_description("AudioUnitSDK" "${CONVOLVER_AU_SDK}")

# The adapter itself, shared by the component and its tests.
add_library(convolver_au_adapter STATIC effect.cpp)
target_include_directories(convolver_au_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/portable" "${Convolver_BINARY_DIR}/generated")
if(NOT TARGET convolver_filter_file)
    message(FATAL_ERROR "The Audio Unit loads filter files chosen in its window, so it needs "
        "CONVOLVER_BUILD_IO (libsndfile)")
endif()
target_link_libraries(convolver_au_adapter PUBLIC convolver_ausdk Convolver::host
    convolver_filter_file)
target_compile_options(convolver_au_adapter PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(convolver_au_adapter PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# AU-6: Convolver's window, in AppKit (Objective-C++ with ARC), compiled
# into the component and the window test. Objective-C classes share one
# namespace per process, so their names carry the version: two Convolver
# builds loaded in one host keep their own windows.
enable_language(OBJCXX)
string(REPLACE "." "_" au_class_version "${Convolver_VERSION}")
set(au_view_factory "ConvolverAUViewFactory_${au_class_version}")
set(au_view_sources "${CMAKE_CURRENT_SOURCE_DIR}/view_factory.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/diagnostic_view.mm")
set_source_files_properties(${au_view_sources} PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc;-Wall;-Wextra"
    COMPILE_DEFINITIONS "CONVOLVER_AU_VIEW_FACTORY=${au_view_factory};CONVOLVER_DIAGNOSTIC_VIEW=ConvolverDiagnosticView_${au_class_version}"
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
# The unit names the factory class and finds its own bundle for hosts.
target_compile_definitions(convolver_au_adapter PRIVATE
    "CONVOLVER_AU_VIEW_FACTORY_NAME=\"${au_view_factory}\""
    "CONVOLVER_AU_BUNDLE_ID=\"${CONVOLVER_ID_AU_BUNDLE}\"")

# AudioComponent versions are 0xMMMMmmbb: one byte each for minor and patch.
# Hosts rescan a component when its version changes, so a clamped number can
# hide an update from them.
set(au_minor ${Convolver_VERSION_MINOR})
set(au_patch ${Convolver_VERSION_PATCH})
if(au_minor GREATER 255 OR au_patch GREATER 255)
    message(WARNING "VERSION ${Convolver_VERSION}: an Audio Unit version holds at most 255 in its "
        "minor and patch numbers, so the component reports 255. Start a new minor version "
        "(docs/develop/versioning.md) so that hosts notice the update.")
    if(au_minor GREATER 255)
        set(au_minor 255)
    endif()
    if(au_patch GREATER 255)
        set(au_patch 255)
    endif()
endif()
math(EXPR CONVOLVER_AU_VERSION "(${Convolver_VERSION_MAJOR} << 16) | (${au_minor} << 8) | ${au_patch}")

add_library(convolver_au MODULE factory.cpp ${au_view_sources})
set_target_properties(convolver_au PROPERTIES OBJCXX_STANDARD 20 OBJCXX_EXTENSIONS OFF
    BUNDLE TRUE BUNDLE_EXTENSION component OUTPUT_NAME Convolver
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(convolver_au PRIVATE convolver_au_adapter "-framework AppKit")
# Export only the factory, so the host process sees none of the engine's symbols.
target_link_options(convolver_au PRIVATE "LINKER:-exported_symbol,_ConvolverAUFactory")
add_dependencies(convolver_au convolver_build_version)
# Licence notices inside the bundle, then an ad-hoc signature over the whole of it.
add_custom_command(TARGET convolver_au POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:convolver_au>/Resources/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:convolver_au>/Resources/licenses/NOTICE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/GPL-3.0.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:convolver_au>/Resources/licenses/GPL-3.0.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CONVOLVER_AU_SDK}/LICENSE.txt"
        "$<TARGET_BUNDLE_CONTENT_DIR:convolver_au>/Resources/licenses/AudioUnitSDK-LICENSE.txt"
    COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:convolver_au>"
    VERBATIM)

if(CONVOLVER_BUILD_TESTS)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    # AU-specific checks, through the Audio Unit API with the unit registered in-process.
    add_executable(convolver_au_tests "${PROJECT_SOURCE_DIR}/tests/au/adapter_tests.cpp")
    target_link_libraries(convolver_au_tests PRIVATE convolver_au_adapter convolver_presets)
    add_test(NAME au.adapter COMMAND convolver_au_tests)
    # The bundle: identities from the registry, one exported symbol, notices, signature.
    add_test(NAME au.bundle COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/au/bundle_tests.py"
        "${PROJECT_SOURCE_DIR}/plugins/identities.json" "$<TARGET_BUNDLE_DIR:convolver_au>" "${PROJECT_VERSION}")
    set_tests_properties(au.adapter au.bundle PROPERTIES LABELS "au;real-libraries" TIMEOUT 180)
    set(au_test_targets convolver_au convolver_au_tests)
    # PLUG-2: the shared harness, against the oracle and the CLI renders that
    # examples.first_use leaves behind.
    if(TARGET convolver_cli AND CONVOLVER_BUILD_NATIVE)
        add_executable(convolver_au_harness "${PROJECT_SOURCE_DIR}/tests/au/harness_tests.cpp"
            "${PROJECT_SOURCE_DIR}/tests/plugins/harness.cpp")
        target_link_libraries(convolver_au_harness PRIVATE convolver_au_adapter convolver_presets Convolver::io)
        add_test(NAME au.harness COMMAND convolver_au_harness "${PROJECT_BINARY_DIR}/first-use/fixtures.txt")
        set_tests_properties(au.harness PROPERTIES LABELS "au;real-libraries;plug2"
            TIMEOUT 300 FIXTURES_REQUIRED first_use_renders)
        list(APPEND au_test_targets convolver_au_harness)
    endif()
    # AU-6: the window, opened off screen on a unit registered in-process.
    add_executable(convolver_au_editor_tests "${PROJECT_SOURCE_DIR}/tests/au/editor_tests.mm"
        ${au_view_sources})
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/au/editor_tests.mm" PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
        COMPILE_DEFINITIONS "CONVOLVER_DIAGNOSTIC_VIEW=ConvolverDiagnosticView_${au_class_version};CONVOLVER_AU_VIEW_FACTORY=${au_view_factory}"
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(convolver_au_editor_tests PROPERTIES OBJCXX_STANDARD 23 OBJCXX_EXTENSIONS OFF)
    target_link_libraries(convolver_au_editor_tests PRIVATE convolver_au_adapter convolver_presets
        "-framework AppKit")
    add_test(NAME au.editor COMMAND convolver_au_editor_tests)
    set_tests_properties(au.editor PROPERTIES LABELS "au;real-libraries" TIMEOUT 180
        ENVIRONMENT "CONVOLVER_LOG=${CMAKE_CURRENT_BINARY_DIR}/convolver-editor-tests.log")
    list(APPEND au_test_targets convolver_au_editor_tests)
    # PLUG-7: lifecycle sequences against the reference model.
    add_executable(convolver_au_lifecycle "${PROJECT_SOURCE_DIR}/tests/au/lifecycle_tests.cpp"
        "${PROJECT_SOURCE_DIR}/tests/plugins/lifecycle.cpp" "${PROJECT_SOURCE_DIR}/tests/plugins/audio_guard.cpp")
    target_link_libraries(convolver_au_lifecycle PRIVATE convolver_au_adapter convolver_presets)
    add_test(NAME au.lifecycle COMMAND convolver_au_lifecycle ${CONVOLVER_LIFECYCLE_LENGTH})
    set_tests_properties(au.lifecycle PROPERTIES LABELS "au;real-libraries;plug7" TIMEOUT 600)
    list(APPEND au_test_targets convolver_au_lifecycle)
    foreach(target IN ITEMS convolver_au_tests convolver_au_harness convolver_au_lifecycle
            convolver_au_editor_tests)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
            target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
        endif()
    endforeach()
    add_dependencies(check ${au_test_targets})
    # DIAG-1: tests write the Convolver log in the build folder, never the user's.
    set_property(TEST au.adapter au.bundle APPEND PROPERTY ENVIRONMENT
        "CONVOLVER_LOG=${CMAKE_CURRENT_BINARY_DIR}/convolver-tests.log")
    if(TEST au.harness)
        set_property(TEST au.harness APPEND PROPERTY ENVIRONMENT
            "CONVOLVER_LOG=${CMAKE_CURRENT_BINARY_DIR}/convolver-tests.log")
    endif()
endif()

# The adapter, the SDK (only Convolver's targets link it) and the tests.
convolver_sanitize(convolver_ausdk convolver_au_adapter convolver_au convolver_au_tests
    convolver_au_harness convolver_au_lifecycle convolver_au_editor_tests)
