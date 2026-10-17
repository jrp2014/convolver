cmake_minimum_required(VERSION 3.20)

# This project runs portable harnesses extracted from the production sources.
# It does not build the historical Windows applications, plug-ins, or engine.
project(ConvolverRegressionTests LANGUAGES CXX)

include(CTest)
if(BUILD_TESTING)
    if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Darwin|Linux)$" OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "The portable regression harnesses require a native macOS or Linux build.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
        message(FATAL_ERROR "The regression harnesses require Clang. Configure with -DCMAKE_CXX_COMPILER=clang++.")
    endif()
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

    # The scripts always compile with ASan and UBSan; fail early if this Clang
    # installation cannot compile and link with the required instrumentation.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-std=c++17 -fms-extensions -fsanitize=address,undefined -fno-sanitize-recover=all")
    check_cxx_source_compiles("int main() { return 0; }" CONVOLVER_HAS_SANITIZERS)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT CONVOLVER_HAS_SANITIZERS)
        message(FATAL_ERROR "Clang with working AddressSanitizer and UndefinedBehaviorSanitizer runtimes is required.")
    endif()

    function(convolver_regression name labels)
        add_test(NAME regression.${name}
            COMMAND "${CMAKE_COMMAND}" -E env
                "CXX=${CMAKE_CXX_COMPILER}"
                "PYTHONDONTWRITEBYTECODE=1"
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression/${name}.py")
        set_tests_properties(regression.${name} PROPERTIES
            LABELS "regression;extracted-production;sanitizers;${labels}"
            TIMEOUT 300)
    endfunction()

    convolver_regression(core_regressions "dsp")
    convolver_regression(host_regressions "host;sample-conversion")
    convolver_regression(gain_regressions "gain")
    convolver_regression(branch_regressions "branches")
    convolver_regression(historical_diagnostics "diagnostics")
    convolver_regression(historical_io_regressions "io")
    convolver_regression(pipe_regressions "pipe;io")
    convolver_regression(pipe_dsp_regressions "pipe;dsp")

    add_test(NAME documentation
        COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression/documentation_regressions.py")
    set_tests_properties(documentation PROPERTIES LABELS "docs;provenance" TIMEOUT 30)

    add_custom_target(check
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -C "$<CONFIG>"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        USES_TERMINAL
        VERBATIM
        COMMENT "Running sanitized portable regression harnesses")
endif()
