# SPDX-License-Identifier: GPL-2.0-or-later
# Run at build time: switching commits or editing sources must refresh the identity.
file(READ "${CONVOLVER_SOURCE_DIR}/VERSION" CONVOLVER_BUILD_VERSION)
string(STRIP "${CONVOLVER_BUILD_VERSION}" CONVOLVER_BUILD_VERSION)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --show-toplevel
        WORKING_DIRECTORY "${CONVOLVER_SOURCE_DIR}"
        OUTPUT_VARIABLE git_root OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    file(REAL_PATH "${CONVOLVER_SOURCE_DIR}" source_root)
    if(git_root)
        file(REAL_PATH "${git_root}" git_root)
    endif()
    # A source archive inside another Git checkout does not inherit its identity.
    if(git_root STREQUAL source_root)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${CONVOLVER_SOURCE_DIR}"
            OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_result)
        if(revision_result EQUAL 0)
            string(APPEND CONVOLVER_BUILD_VERSION "+g${revision}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
                WORKING_DIRECTORY "${CONVOLVER_SOURCE_DIR}"
                OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(changes)
                string(APPEND CONVOLVER_BUILD_VERSION ".dirty")
            endif()
        endif()
    endif()
endif()
configure_file("${CONVOLVER_SOURCE_DIR}/cmake/convolver_version.hpp.in"
    "${CONVOLVER_BINARY_DIR}/generated/convolver_version.hpp" @ONLY)
