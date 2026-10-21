# SPDX-License-Identifier: GPL-2.0-or-later
# Prefer upstream package targets; preserve support for distribution packages
# and configure/make installations which only provide headers and libraries.
if(NOT TARGET FFTW3::fftw3f)
    find_package(FFTW3f CONFIG QUIET)
endif()
if(TARGET FFTW3::fftw3f)
    set(ConvolverFFTW_FOUND TRUE)
else()
    find_path(FFTW3F_INCLUDE_DIR fftw3.h)
    find_library(FFTW3F_LIBRARY NAMES fftw3f libfftw3f-3)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(ConvolverFFTW REQUIRED_VARS FFTW3F_INCLUDE_DIR FFTW3F_LIBRARY)
    if(ConvolverFFTW_FOUND)
        add_library(FFTW3::fftw3f UNKNOWN IMPORTED)
        set_target_properties(FFTW3::fftw3f PROPERTIES
            IMPORTED_LOCATION "${FFTW3F_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFTW3F_INCLUDE_DIR}")
    endif()
endif()
if(ConvolverFFTW_FOUND AND WIN32)
    get_target_property(fftw3f_type FFTW3::fftw3f TYPE)
    if(fftw3f_type STREQUAL "SHARED_LIBRARY")
        # Exported FFTW data symbols need DLL import decoration on Windows.
        set_property(TARGET FFTW3::fftw3f APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS FFTW_DLL)
    endif()
endif()
