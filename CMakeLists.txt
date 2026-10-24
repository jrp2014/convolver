# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)
include(cmake/ConvolverVersion.cmake)
project(Convolver VERSION ${CONVOLVER_VERSION} LANGUAGES CXX)

set(CONVOLVER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CONVOLVER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
include(cmake/ConvolverBuildVersion.cmake)
include(cmake/ConvolverIdentities.cmake)
include(cmake/ConvolverDebugSymbols.cmake)
add_custom_target(convolver_build_version
    COMMAND "${CMAKE_COMMAND}" "-DCONVOLVER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DCONVOLVER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConvolverBuildVersion.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/convolver_version.hpp"
    VERBATIM)

include(CMakeDependentOption)
include(GNUInstallDirs)
set(convolver_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(convolver_top_level ON)
    include(CTest)
endif()
set(convolver_testing_allowed ON)
if(DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
    set(convolver_testing_allowed OFF)
endif()
cmake_dependent_option(CONVOLVER_BUILD_TESTS "Build Convolver tests" ${convolver_top_level} "convolver_testing_allowed" OFF)
if(CONVOLVER_BUILD_TESTS)
    enable_testing()
endif()
option(CONVOLVER_PACKAGE_PLUGINS "Stage VST3/tools/examples for distribution" OFF)
option(CONVOLVER_BUILD_VST3 "Build the optional VST3 adapter (requires SDK)" OFF)
option(CONVOLVER_BUILD_LV2 "Build the optional LV2 adapter (requires the lv2 headers; tests need lilv)" OFF)
option(CONVOLVER_BUILD_AU "Build the optional Audio Unit adapter (macOS; requires AudioUnitSDK)" OFF)
option(CONVOLVER_BUILD_NATIVE "Build the portable FFT/libsndfile core and CLI" ON)
option(CONVOLVER_SANITIZERS "Instrument native targets with ASan and UBSan" OFF)
# PLUG-7: lifecycle sequences are enumerated up to this many operations after
# each start; LV2, with its worker operations, has about twice as many to
# choose from. Sanitizers make each sequence ten to twenty times slower, so
# their builds enumerate one fewer (docs/develop/testing.md#lifecycle-sequences-plug-7).
if(CONVOLVER_SANITIZERS)
    set(CONVOLVER_LIFECYCLE_LENGTH 3)
    set(CONVOLVER_LV2_LIFECYCLE_LENGTH 2)
else()
    set(CONVOLVER_LIFECYCLE_LENGTH 4)
    set(CONVOLVER_LV2_LIFECYCLE_LENGTH 3)
endif()
option(CONVOLVER_BUILD_IO "Build the libsndfile/configuration adapter" ON)
option(CONVOLVER_BUILD_CLI "Build the command-line renderer" ${CONVOLVER_BUILD_IO})
option(CONVOLVER_PACKAGE_TESTS "Test installed and source-tree consumers" ON)
option(CONVOLVER_DOCUMENTATION_TESTS "Check historical documentation integrity" ${CONVOLVER_BUILD_IO})
option(CONVOLVER_BUILD_BENCHMARKS "Build the native processing benchmark" OFF)
option(CONVOLVER_BUILD_FUZZERS "Build Clang libFuzzer targets (needs CONVOLVER_BUILD_IO)" OFF)
set(legacy_default OFF)
if(CONVOLVER_BUILD_IO AND CMAKE_SYSTEM_NAME MATCHES "^(Darwin|Linux)$" AND
   CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$" AND NOT CMAKE_CROSSCOMPILING)
    set(legacy_default ON)
endif()
option(CONVOLVER_LEGACY_TESTS "Run Clang extraction regression harnesses" ${legacy_default})
# HOST-4: run the Equalizer APO export through Equalizer APO itself (Windows).
# Its Benchmark.exe reads config.txt from the folder in the registry value
# HKLM\SOFTWARE\EqualizerAPO\ConfigPath; set both to a folder made for the test.
set(CONVOLVER_EQUALIZER_APO_BENCHMARK "" CACHE FILEPATH "Optional Equalizer APO Benchmark.exe for export.equalizer_apo.runtime")
set(CONVOLVER_EQUALIZER_APO_CONFIG "" CACHE PATH "Empty folder that Equalizer APO's ConfigPath registry value names")

if(CONVOLVER_BUILD_NATIVE)
    if(CONVOLVER_BUILD_CLI AND NOT CONVOLVER_BUILD_IO)
        message(FATAL_ERROR "CONVOLVER_BUILD_CLI requires CONVOLVER_BUILD_IO; disable CLI for a core-only build.")
    endif()
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    # AUTO picks FFTW, except in plug-in builds: a host process may load other
    # plug-ins that use the same FFTW, and FFTW's planner is not thread-safe, so
    # plug-in builds use Ooura (Accelerate on Apple). Set FFTW, ACCELERATE or
    # OOURA explicitly to override. One backend serves every target in a build.
    set(CONVOLVER_FFT_BACKEND "AUTO" CACHE STRING
        "FFT backend: AUTO (FFTW; OOURA or ACCELERATE for plug-in builds), FFTW, ACCELERATE or OOURA")
    set_property(CACHE CONVOLVER_FFT_BACKEND PROPERTY STRINGS AUTO FFTW ACCELERATE OOURA)
    string(TOUPPER "${CONVOLVER_FFT_BACKEND}" fft_backend)
    set(convolver_plugin_build OFF)
    if(CONVOLVER_BUILD_VST3 OR CONVOLVER_BUILD_LV2 OR CONVOLVER_BUILD_AU)
        set(convolver_plugin_build ON)
    endif()
    if(fft_backend STREQUAL "AUTO")
        if(NOT convolver_plugin_build)
            set(fft_backend "FFTW")
        elseif(APPLE)
            set(fft_backend "ACCELERATE")
        else()
            set(fft_backend "OOURA")
        endif()
    elseif(fft_backend STREQUAL "FFTW" AND convolver_plugin_build)
        message(WARNING "CONVOLVER_FFT_BACKEND=FFTW in a plug-in build: FFTW's planner is not "
            "thread-safe, and another plug-in or the host may use the same FFTW at the same "
            "time. Use AUTO (Ooura, or Accelerate on Apple) for plug-ins you distribute.")
    endif()
    if(fft_backend STREQUAL "FFTW")
        find_package(ConvolverFFTW REQUIRED)
        set(fft_source portable/fft_fftw.cpp)
        set(fft_library FFTW3::fftw3f)
    elseif(fft_backend STREQUAL "ACCELERATE")
        if(NOT APPLE)
            message(FATAL_ERROR "The Accelerate FFT backend requires an Apple platform.")
        endif()
        set(fft_source portable/fft_accelerate.cpp)
        # Resolve the framework against the consumer's selected SDK at link time.
        set(fft_library "-framework Accelerate")
    elseif(fft_backend STREQUAL "OOURA")
        # Built-in split-radix FFT: no FFT library dependency.
        set(fft_source portable/fft_ooura.cpp portable/third_party/ooura/fftsg.cpp)
        set(fft_library "")
        # Third-party code is compiled as published, without the project's warnings.
        set_source_files_properties(portable/third_party/ooura/fftsg.cpp PROPERTIES
            COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
    else()
        message(FATAL_ERROR "CONVOLVER_FFT_BACKEND must be AUTO, FFTW, ACCELERATE or OOURA.")
    endif()
    message(STATUS "Convolver FFT backend: ${fft_backend} (CONVOLVER_FFT_BACKEND=${CONVOLVER_FFT_BACKEND})")
    find_package(Threads REQUIRED)
    add_library(convolver_core STATIC portable/engine.cpp portable/kernels.cpp portable/attenuation.cpp
        ${fft_source})
    add_library(Convolver::core ALIAS convolver_core)
    set_target_properties(convolver_core PROPERTIES EXPORT_NAME core POSITION_INDEPENDENT_CODE ON)
    target_include_directories(convolver_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(convolver_core PUBLIC cxx_std_20)
    target_link_libraries(convolver_core PRIVATE ${fft_library} Threads::Threads)
    # PERF-4: on x86-64 the multiply-accumulate kernels are also compiled for AVX2
    # with FMA and chosen at run time (portable/kernels.cpp), so release binaries
    # keep the x86-64 baseline. The AVX2 set fuses multiplies with explicit
    # intrinsics; nothing is contracted implicitly (owner's decision, 24 October 2026).
    set(convolver_x86_64 OFF)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        if("x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
            set(convolver_x86_64 ON)
        endif()
    elseif(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        # The target, not the host: an ARM64 or ARM64EC build on an x64 machine has none.
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
            set(convolver_x86_64 ON)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(convolver_x86_64 ON)
    endif()
    option(CONVOLVER_AVX2_KERNELS "Add AVX2+FMA processing kernels chosen at run time (x86-64)" ${convolver_x86_64})
    if(CONVOLVER_AVX2_KERNELS)
        target_sources(convolver_core PRIVATE portable/kernels_avx2.cpp)
        target_compile_definitions(convolver_core PRIVATE CONVOLVER_AVX2_KERNELS)
        # No implicit contraction in either set, even with -march flags added by the
        # user (other targets have only the baseline and keep the default): the baseline stays unfused and the AVX2 set fuses only where its
        # intrinsics say. MSVC and clang-cl get the contraction pragma in
        # portable/kernels.inc; MSVC's /arch:AVX2 already allows FMA instructions.
        if(MSVC)
            set(contract_options "")
            set(avx2_options /arch:AVX2)
            if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
                list(APPEND avx2_options /clang:-mfma) # clang-cl: not implied by every version
            endif()
        elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
            # One compiler run per architecture: pass the flags to x86_64 only.
            set(contract_options -Xarch_x86_64 -ffp-contract=off)
            set(avx2_options -Xarch_x86_64 -mavx2 -Xarch_x86_64 -mfma ${contract_options})
        else()
            set(contract_options -ffp-contract=off)
            set(avx2_options -mavx2 -mfma ${contract_options})
        endif()
        set_source_files_properties(portable/kernels.cpp PROPERTIES COMPILE_OPTIONS "${contract_options}")
        set_source_files_properties(portable/kernels_avx2.cpp PROPERTIES COMPILE_OPTIONS "${avx2_options}")
    endif()
    # Experimental only: compare vDSP arithmetic with the existing split kernels.
    option(CONVOLVER_EXPERIMENT_VDSP_MAC "Benchmark vDSP complex accumulation (Apple only)" OFF)
    if(CONVOLVER_EXPERIMENT_VDSP_MAC)
        if(NOT APPLE)
            message(FATAL_ERROR "The vDSP MAC experiment requires Apple Accelerate.")
        endif()
        target_sources(convolver_core PRIVATE portable/kernels_vdsp.cpp)
        target_compile_definitions(convolver_core PRIVATE CONVOLVER_EXPERIMENT_VDSP_MAC)
        target_link_libraries(convolver_core PRIVATE "-framework Accelerate")
    endif()
    add_library(convolver_host STATIC portable/host.cpp portable/host_log.cpp portable/state.cpp)
    add_library(Convolver::host ALIAS convolver_host)
    set_target_properties(convolver_host PROPERTIES EXPORT_NAME host POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(convolver_host PUBLIC Convolver::core)
    add_dependencies(convolver_host convolver_build_version) # The version in the Convolver log.
    # The speaker table of plugins/identities.json (PLUG-4), used by speaker_notice.
    target_include_directories(convolver_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(native_targets convolver_core convolver_host)
    # PLUG-3: preset files for every plug-in format, without any plug-in SDK.
    # Private to this build (not installed); adapters and the preset tool link it.
    add_library(convolver_presets STATIC portable/presets.cpp)
    set_target_properties(convolver_presets PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(convolver_presets PUBLIC portable "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(convolver_presets PUBLIC Convolver::host)
    list(APPEND native_targets convolver_presets)
    if(CONVOLVER_BUILD_IO)
        find_package(ConvolverSndFile REQUIRED)
        add_library(convolver_io STATIC portable/io.cpp portable/config.cpp portable/config_parse.cpp
            portable/inspection.cpp)
        add_library(Convolver::io ALIAS convolver_io)
        # Position independent: the LV2 plug-in links it.
        set_target_properties(convolver_io PROPERTIES EXPORT_NAME io POSITION_INDEPENDENT_CODE ON)
        target_include_directories(convolver_io PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        target_compile_features(convolver_io PUBLIC cxx_std_20)
        # Inspection reports the engine's own latency/tail/storage analysis.
        target_link_libraries(convolver_io PUBLIC Convolver::core PRIVATE SndFile::sndfile)
        list(APPEND native_targets convolver_io)
    endif()
    if(CONVOLVER_BUILD_CLI)
        add_executable(convolver_cli portable/main.cpp portable/inspection_cli.cpp)
        add_dependencies(convolver_cli convolver_build_version)
        target_include_directories(convolver_cli PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set_target_properties(convolver_cli PROPERTIES OUTPUT_NAME convolver)
        if(MINGW)
            target_link_options(convolver_cli PRIVATE -municode)
        endif()
        target_link_libraries(convolver_cli PRIVATE Convolver::core Convolver::io)
        add_executable(convolver_preset_tool portable/preset_main.cpp)
        set_target_properties(convolver_preset_tool PROPERTIES OUTPUT_NAME convolver-preset)
        if(MINGW)
            target_link_options(convolver_preset_tool PRIVATE -municode)
        endif()
        target_link_libraries(convolver_preset_tool PRIVATE convolver_presets Convolver::io)
        # Equalizer APO export: a filter for system-wide playback on Windows.
        add_executable(convolver_equalizer_apo portable/equalizer_apo.cpp portable/equalizer_apo_main.cpp)
        add_dependencies(convolver_equalizer_apo convolver_build_version)
        target_include_directories(convolver_equalizer_apo PRIVATE portable "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set_target_properties(convolver_equalizer_apo PROPERTIES OUTPUT_NAME convolver-equalizer-apo)
        if(MINGW)
            target_link_options(convolver_equalizer_apo PRIVATE -municode)
        endif()
        target_link_libraries(convolver_equalizer_apo PRIVATE Convolver::io)
        list(APPEND native_targets convolver_cli convolver_preset_tool convolver_equalizer_apo)
    endif()
    if(CONVOLVER_BUILD_BENCHMARKS)
        add_executable(convolver_benchmark tests/native/benchmark.cpp)
        target_include_directories(convolver_benchmark PRIVATE portable)
        target_link_libraries(convolver_benchmark PRIVATE convolver_core)
        add_executable(convolver_mac_benchmark tests/native/benchmarks/vdsp_mac.cpp)
        target_include_directories(convolver_mac_benchmark PRIVATE portable)
        target_link_libraries(convolver_mac_benchmark PRIVATE convolver_core)
        list(APPEND native_targets convolver_benchmark convolver_mac_benchmark)
        if(CONVOLVER_BUILD_IO)
            # Startup, reset, replacement and loading at a host callback size (PERF-5).
            add_executable(convolver_callback_benchmark tests/native/callback_benchmark.cpp)
            target_include_directories(convolver_callback_benchmark PRIVATE portable)
            target_link_libraries(convolver_callback_benchmark PRIVATE convolver_core convolver_host Convolver::io)
            list(APPEND native_targets convolver_callback_benchmark)
        endif()
    endif()
    if(CONVOLVER_BUILD_TESTS)
        # Every native test executable is registered here (the Python-driven
        # native tests are added further down), so the check target builds it
        # and it gets the shared compile, warning and sanitizer settings
        # (applied to native_targets below), the test labels and the timeout.
        set(test_targets "")
        function(convolver_native_test name target)
            cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "SOURCES;LIBRARIES;ARGS;LABELS")
            if(NOT arg_LABELS)
                set(arg_LABELS native real-libraries)
            endif()
            add_executable(${target} ${arg_SOURCES})
            target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/portable")
            target_link_libraries(${target} PRIVATE ${arg_LIBRARIES})
            add_test(NAME ${name} COMMAND ${target} ${arg_ARGS})
            set_tests_properties(${name} PROPERTIES LABELS "${arg_LABELS}" TIMEOUT 120)
            set(test_targets ${test_targets} ${target} PARENT_SCOPE)
        endfunction()
        convolver_native_test(native.fft convolver_fft_tests
            SOURCES tests/native/fft_tests.cpp LIBRARIES convolver_core)
        convolver_native_test(native.engine convolver_engine_tests
            SOURCES tests/native/engine_tests.cpp LIBRARIES convolver_core)
        if(CONVOLVER_EXPERIMENT_VDSP_MAC)
            target_compile_definitions(convolver_engine_tests PRIVATE CONVOLVER_EXPERIMENT_VDSP_MAC)
        endif()
        convolver_native_test(native.kernel_divergence convolver_kernel_divergence_tests
            SOURCES tests/native/kernel_divergence_tests.cpp LIBRARIES convolver_core)
        convolver_native_test(native.host convolver_host_tests
            SOURCES tests/native/host_tests.cpp LIBRARIES Convolver::host Threads::Threads)
        # PLUG-7: lifecycle sequences against the reference model.
        convolver_native_test(native.host_lifecycle convolver_host_lifecycle_tests
            SOURCES tests/native/host_lifecycle_tests.cpp tests/plugins/lifecycle.cpp
                tests/plugins/audio_guard.cpp
            LIBRARIES Convolver::host ${CMAKE_DL_LIBS}
            ARGS ${CONVOLVER_LIFECYCLE_LENGTH})
        convolver_native_test(native.state convolver_state_tests
            SOURCES tests/native/state_tests.cpp LIBRARIES Convolver::host)
        convolver_native_test(native.presets convolver_preset_tests
            SOURCES tests/native/preset_tests.cpp LIBRARIES convolver_presets)
        convolver_native_test(native.utilities convolver_utility_tests
            SOURCES tests/native/utility_tests.cpp LIBRARIES Convolver::core Threads::Threads)
        if(CONVOLVER_BUILD_IO)
            convolver_native_test(native.io convolver_io_tests
                SOURCES tests/native/io_tests.cpp LIBRARIES convolver_io SndFile::sndfile Threads::Threads)
            convolver_native_test(native.config_parse convolver_config_parse_tests
                SOURCES tests/native/config_parse_tests.cpp LIBRARIES convolver_io)
            convolver_native_test(native.inspection convolver_inspection_tests
                SOURCES tests/native/inspection_tests.cpp LIBRARIES convolver_io)
            convolver_native_test(native.limits convolver_limits_tests
                SOURCES tests/native/limits_tests.cpp LIBRARIES convolver_io Convolver::host)
        endif()
        # Replay fuzz seeds and minimized regressions deterministically, without libFuzzer.
        set(fuzz_harnesses config_text config_list host_state ir_file)
        if(CONVOLVER_BUILD_IO)
            foreach(harness IN LISTS fuzz_harnesses)
                convolver_native_test(fuzz.replay.${harness} convolver_fuzz_replay_${harness}
                    SOURCES tests/fuzz/fuzz_${harness}.cpp tests/fuzz/replay_main.cpp
                    LIBRARIES convolver_io Convolver::host
                    ARGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/seeds/${harness}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/regressions/${harness}"
                    LABELS fuzz replay)
            endforeach()
        endif()
        list(APPEND native_targets ${test_targets})
    endif()
    if(CONVOLVER_BUILD_FUZZERS)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CONVOLVER_BUILD_IO)
            message(FATAL_ERROR "CONVOLVER_BUILD_FUZZERS needs Clang (with libFuzzer) and CONVOLVER_BUILD_IO.")
        endif()
        foreach(harness config_text config_list host_state ir_file)
            add_executable(fuzz_${harness} tests/fuzz/fuzz_${harness}.cpp)
            target_include_directories(fuzz_${harness} PRIVATE portable)
            target_link_libraries(fuzz_${harness} PRIVATE convolver_io Convolver::host)
            target_link_options(fuzz_${harness} PRIVATE -fsanitize=fuzzer)
            list(APPEND native_targets fuzz_${harness})
        endforeach()
    endif()
    foreach(target IN LISTS native_targets)
        set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
        if(APPLE)
            set_target_properties(${target} PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
        elseif(UNIX)
            set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(CONVOLVER_BUILD_FUZZERS)
            # Coverage instrumentation for libraries and harnesses alike.
            target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
        endif()
        if(CONVOLVER_SANITIZERS)
            if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
                message(FATAL_ERROR "Native ASan/UBSan option requires GCC or Clang with sanitizer runtimes.")
            endif()
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
            target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all)
        endif()
    endforeach()
    include(CMakePackageConfigHelpers)
    set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Convolver")
    install(TARGETS convolver_core convolver_host EXPORT ConvolverCoreTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT ConvolverCoreTargets NAMESPACE Convolver:: DESTINATION ${package_dir})
    install(FILES include/convolver/engine.hpp include/convolver/host.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/convolver)
    if(CONVOLVER_BUILD_IO)
        install(TARGETS convolver_io EXPORT ConvolverIOTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(EXPORT ConvolverIOTargets NAMESPACE Convolver:: DESTINATION ${package_dir})
        install(FILES include/convolver/io.hpp include/convolver/inspection.hpp
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/convolver)
    endif()
    if(CONVOLVER_BUILD_CLI)
        install(TARGETS convolver_cli convolver_preset_tool convolver_equalizer_apo
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    configure_package_config_file(cmake/ConvolverConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/ConvolverConfig.cmake" INSTALL_DESTINATION ${package_dir})
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ConvolverConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ConvolverConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ConvolverConfigVersion.cmake"
        cmake/FindConvolverFFTW.cmake cmake/FindConvolverSndFile.cmake DESTINATION ${package_dir})

endif()

if(CONVOLVER_BUILD_TESTS)
    if((CONVOLVER_BUILD_NATIVE AND CONVOLVER_BUILD_CLI) OR CONVOLVER_LEGACY_TESTS OR CONVOLVER_DOCUMENTATION_TESTS)
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    endif()
    if(CONVOLVER_BUILD_NATIVE AND CONVOLVER_BUILD_CLI)
        if(GIT_FOUND)
            add_test(NAME versioning COMMAND "${CMAKE_COMMAND}" -E env
                "CONVOLVER_TEST_CMAKE=${CMAKE_COMMAND}" "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/versioning_tests.py")
            set_tests_properties(versioning PROPERTIES LABELS "build;versioning;repository" TIMEOUT 120)
        endif()
        add_test(NAME identities COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/identities_tests.py")
        set_tests_properties(identities PROPERTIES LABELS "build;identities;repository")
        add_test(NAME native.cli COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/cli_tests.py" "$<TARGET_FILE:convolver_cli>")
        set_tests_properties(native.cli PROPERTIES LABELS "native;real-libraries;cli" TIMEOUT 120)
        add_test(NAME native.fft_planning COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/fft_planning_tests.py" "$<TARGET_FILE:convolver_cli>")
        set_tests_properties(native.fft_planning PROPERTIES LABELS "native;real-libraries;cli" TIMEOUT 120)
        add_test(NAME native.inspection_cli COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/inspection_cli_tests.py" "$<TARGET_FILE:convolver_cli>")
        set_tests_properties(native.inspection_cli PROPERTIES LABELS "native;real-libraries;cli" TIMEOUT 120)
        # PLUG-3: presets for every format, read back by independent decoders.
        add_test(NAME plugins.presets COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/preset_tests.py"
            "$<TARGET_FILE:convolver_preset_tool>")
        set_tests_properties(plugins.presets PROPERTIES LABELS "plugins;real-libraries;cli" TIMEOUT 120)
        # Equalizer APO export, run through a model of Equalizer APO's commands.
        add_test(NAME export.equalizer_apo COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/equalizer_apo_tests.py"
            "$<TARGET_FILE:convolver_equalizer_apo>")
        set_tests_properties(export.equalizer_apo PROPERTIES LABELS "native;real-libraries;cli" TIMEOUT 300)
        if(CONVOLVER_EQUALIZER_APO_BENCHMARK OR CONVOLVER_EQUALIZER_APO_CONFIG)
            if(NOT WIN32 OR NOT CONVOLVER_EQUALIZER_APO_BENCHMARK OR NOT CONVOLVER_EQUALIZER_APO_CONFIG)
                message(FATAL_ERROR "export.equalizer_apo.runtime runs on Windows and needs both "
                    "CONVOLVER_EQUALIZER_APO_BENCHMARK (Benchmark.exe) and CONVOLVER_EQUALIZER_APO_CONFIG "
                    "(the folder Equalizer APO's ConfigPath registry value names).")
            endif()
            add_test(NAME export.equalizer_apo.runtime COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/equalizer_apo_tests.py"
                "$<TARGET_FILE:convolver_equalizer_apo>"
                --benchmark "${CONVOLVER_EQUALIZER_APO_BENCHMARK}" --config "${CONVOLVER_EQUALIZER_APO_CONFIG}")
            set_tests_properties(export.equalizer_apo.runtime PROPERTIES
                LABELS "native;real-libraries;cli;equalizer-apo" TIMEOUT 600)
        endif()
        add_test(NAME compatibility.corpus COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/run_corpus.py"
            "$<TARGET_FILE:convolver_cli>")
        set_tests_properties(compatibility.corpus PROPERTIES LABELS "compatibility;real-libraries;cli" TIMEOUT 300)
        # DOC-1 and PLUG-2: the shared fixtures, and the documented first-use commands run
        # as written. The renders it leaves are the CLI reference for plug-in comparisons.
        add_test(NAME examples.fixtures COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/fixtures.py" check
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/first-use")
        add_test(NAME examples.first_use COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/fixtures.py" run-documented
            --cli "$<TARGET_FILE:convolver_cli>" --work "${PROJECT_BINARY_DIR}/first-use")
        set_tests_properties(examples.first_use PROPERTIES LABELS "examples;real-libraries;cli"
            TIMEOUT 120 FIXTURES_SETUP first_use_renders)
        # The fixtures check reads the repository only, and no build option changes it
        # (docs/develop/testing.md#which-configurations-run-which-tests).
        set_tests_properties(examples.fixtures PROPERTIES LABELS "examples;repository" TIMEOUT 120)
    endif()
    if(CONVOLVER_LEGACY_TESTS)
        if(NOT legacy_default)
            message(FATAL_ERROR "Legacy extraction tests require native macOS/Linux with Clang. Set CONVOLVER_LEGACY_TESTS=OFF for other toolchains.")
        endif()
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_FLAGS "-std=c++17 -fms-extensions -fsanitize=address,undefined -fno-sanitize-recover=all")
        check_cxx_source_compiles("int main() { return 0; }" CONVOLVER_HAS_SANITIZERS)
        unset(CMAKE_REQUIRED_FLAGS)
        if(NOT CONVOLVER_HAS_SANITIZERS)
            message(FATAL_ERROR "Clang with ASan/UBSan runtimes is required for legacy tests.")
        endif()
        function(convolver_regression name labels)
            set(test_environment "CXX=${CMAKE_CXX_COMPILER}" "PYTHONDONTWRITEBYTECODE=1")
            if(APPLE AND CMAKE_OSX_SYSROOT)
                # Extraction harnesses invoke the compiler outside CMake.
                # Xcode's direct compiler path needs the selected SDK too.
                list(APPEND test_environment "SDKROOT=${CMAKE_OSX_SYSROOT}")
            endif()
            add_test(NAME regression.${name} COMMAND "${CMAKE_COMMAND}" -E env
                ${test_environment}
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression/${name}.py")
            set_tests_properties(regression.${name} PROPERTIES
                LABELS "regression;extracted-production;sanitizers;${labels}" TIMEOUT 300)
        endfunction()
        convolver_regression(core_regressions "dsp")
        convolver_regression(host_regressions "host;sample-conversion")
        convolver_regression(gain_regressions "gain")
        convolver_regression(branch_regressions "branches")
        convolver_regression(historical_diagnostics "diagnostics")
        convolver_regression(historical_io_regressions "io")
        convolver_regression(pipe_regressions "pipe;io")
        convolver_regression(pipe_dsp_regressions "pipe;dsp")
    endif()
    if(CONVOLVER_DOCUMENTATION_TESTS)
        add_test(NAME documentation COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression/documentation_regressions.py")
        set_tests_properties(documentation PROPERTIES LABELS "docs;provenance;repository" TIMEOUT 30)
    endif()
    if(CONVOLVER_BUILD_NATIVE AND CONVOLVER_PACKAGE_TESTS)
        configure_file(tests/consumer/cache.cmake.in consumer-cache.cmake @ONLY)
        configure_file(tests/consumer/verify.cmake.in consumer-verification.cmake @ONLY)
        # One CTest entry per consumer, so they can run side by side. The
        # install step copies the consumer project and relocates the package
        # once for all of them.
        set(consumer_cases installed-core installed-host source-core source-no-ctest missing-component)
        if(CONVOLVER_BUILD_IO)
            list(APPEND consumer_cases installed-io)
        else()
            list(APPEND consumer_cases missing-io)
        endif()
        foreach(step IN ITEMS install LISTS consumer_cases)
            add_test(NAME packaging.consumer.${step} COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>"
                "-DSTEP=${step}" -P "${CMAKE_CURRENT_BINARY_DIR}/consumer-verification.cmake")
            set_tests_properties(packaging.consumer.${step} PROPERTIES LABELS "packaging;consumer;real-libraries"
                TIMEOUT 180)
        endforeach()
        set_tests_properties(packaging.consumer.install PROPERTIES FIXTURES_SETUP consumer_package)
        list(TRANSFORM consumer_cases PREPEND packaging.consumer.)
        set_tests_properties(${consumer_cases} PROPERTIES FIXTURES_REQUIRED consumer_package)
    endif()
    add_custom_target(check COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -C "$<CONFIG>"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" USES_TERMINAL VERBATIM)
    if(CONVOLVER_BUILD_NATIVE)
        add_dependencies(check ${native_targets})
    endif()
endif()

if(CONVOLVER_BUILD_VST3)
    if(NOT CONVOLVER_BUILD_NATIVE)
        message(FATAL_ERROR "VST3 requires the portable core")
    endif()
    add_subdirectory(plugins/vst3)
endif()

if(CONVOLVER_BUILD_LV2)
    if(NOT CONVOLVER_BUILD_NATIVE)
        message(FATAL_ERROR "LV2 requires the portable core")
    endif()
    add_subdirectory(plugins/lv2)
endif()

if(CONVOLVER_BUILD_AU)
    if(NOT APPLE OR NOT CONVOLVER_BUILD_NATIVE)
        message(FATAL_ERROR "The Audio Unit adapter needs macOS and the portable core")
    endif()
    add_subdirectory(plugins/au)
endif()

if(CONVOLVER_PACKAGE_PLUGINS)
    include(cmake/ConvolverPluginPackaging.cmake)
endif()
