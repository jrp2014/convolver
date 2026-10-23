# SPDX-License-Identifier: GPL-2.0-or-later
# The Audio Unit (AUv2 effect) adapter, Convolver.component, on Apple's
# AudioUnitSDK (Apache-2.0). The built component is distributed under
# GPL-3.0-or-later (owner's decision, 21 October 2026); see NOTICE.txt.
cmake_minimum_required(VERSION 3.20)
set(CONVOLVER_AU_SDK "" CACHE PATH "AudioUnitSDK 1.4.0 source directory (packaging/release-policy.json)")
if(NOT EXISTS "${CONVOLVER_AU_SDK}/include/AudioUnitSDK/AUBase.h")
    message(FATAL_ERROR "Set CONVOLVER_AU_SDK to the pinned AudioUnitSDK 1.4.0; "
        "scripts/release/sources.py --fetch ... audiounitsdk unpacks it (docs/develop/build.md)")
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

# The adapter itself, shared by the component and its tests.
add_library(convolver_au_adapter STATIC effect.cpp)
target_include_directories(convolver_au_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/portable" "${Convolver_BINARY_DIR}/generated")
target_link_libraries(convolver_au_adapter PUBLIC convolver_ausdk Convolver::host)
target_compile_options(convolver_au_adapter PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(convolver_au_adapter PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

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

add_library(convolver_au MODULE factory.cpp)
set_target_properties(convolver_au PROPERTIES
    BUNDLE TRUE BUNDLE_EXTENSION component OUTPUT_NAME Convolver
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(convolver_au PRIVATE convolver_au_adapter)
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
    foreach(target IN ITEMS convolver_au_tests convolver_au_harness)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
            target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
        endif()
    endforeach()
    add_dependencies(check ${au_test_targets})
endif()
