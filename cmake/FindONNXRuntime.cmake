# FindONNXRuntime.cmake
# --------------------
# Find the ONNX Runtime C/C++ library.
#
# This module first attempts to use ONNX Runtime's shipped CMake config
# (find_package in CONFIG mode). If that fails, it falls back to manual
# header/library discovery.
#
# Inputs (optional):
#   ONNXRUNTIME_ROOT  - Root directory of an ONNX Runtime installation.
#                       Defaults to ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-osx-arm64-*
#
# Outputs:
#   ONNXRuntime_FOUND            - TRUE if ONNX Runtime was found
#   ONNXRuntime_INCLUDE_DIRS     - Include directories
#   ONNXRuntime_LIBRARIES        - Library files to link
#   ONNXRuntime_VERSION          - Version string (if available)
#
# Imported target:
#   onnxruntime::onnxruntime     - The ONNX Runtime shared library target

# ---------------------------------------------------------------------------
# 1. Determine the search root
# ---------------------------------------------------------------------------
if(NOT ONNXRUNTIME_ROOT)
    # Auto-detect from third_party directory
    file(GLOB _ort_candidates
        "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/onnxruntime-osx-arm64-*"
    )
    # Filter out non-directories (e.g. leftover tarballs)
    foreach(_candidate IN LISTS _ort_candidates)
        if(IS_DIRECTORY "${_candidate}")
            set(ONNXRUNTIME_ROOT "${_candidate}")
            break()
        endif()
    endforeach()
    unset(_ort_candidates)
    unset(_candidate)
endif()

if(NOT ONNXRUNTIME_ROOT)
    message(STATUS "FindONNXRuntime: ONNXRUNTIME_ROOT not set and no installation "
                   "found under third_party/onnxruntime/")
endif()

# ---------------------------------------------------------------------------
# 2. Try CONFIG mode first (uses shipped onnxruntimeConfig.cmake)
# ---------------------------------------------------------------------------
if(ONNXRUNTIME_ROOT)
    find_package(onnxruntime CONFIG QUIET
        PATHS "${ONNXRUNTIME_ROOT}"
        NO_DEFAULT_PATH
    )
endif()

if(onnxruntime_FOUND AND TARGET onnxruntime::onnxruntime)
    # The shipped config creates onnxruntime::onnxruntime but sets
    # INTERFACE_INCLUDE_DIRECTORIES to <prefix>/include/onnxruntime.
    # The actual headers live at <prefix>/include/, so we fix that up.
    get_target_property(_ort_inc onnxruntime::onnxruntime INTERFACE_INCLUDE_DIRECTORIES)
    if(_ort_inc AND NOT EXISTS "${_ort_inc}")
        # Replace the incorrect path with the real one
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include"
        )
        set(_ort_inc "${ONNXRUNTIME_ROOT}/include")
    endif()

    set(ONNXRuntime_FOUND TRUE)
    set(ONNXRuntime_INCLUDE_DIRS "${_ort_inc}")
    get_target_property(ONNXRuntime_LIBRARIES onnxruntime::onnxruntime IMPORTED_LOCATION_RELEASE)
    if(NOT ONNXRuntime_LIBRARIES)
        get_target_property(ONNXRuntime_LIBRARIES onnxruntime::onnxruntime IMPORTED_LOCATION)
    endif()

    # Read version
    if(EXISTS "${ONNXRUNTIME_ROOT}/VERSION_NUMBER")
        file(READ "${ONNXRUNTIME_ROOT}/VERSION_NUMBER" ONNXRuntime_VERSION)
        string(STRIP "${ONNXRuntime_VERSION}" ONNXRuntime_VERSION)
    endif()

    message(STATUS "FindONNXRuntime: Found via CMake config at ${ONNXRUNTIME_ROOT} "
                   "(version ${ONNXRuntime_VERSION})")
    unset(_ort_inc)
    return()
endif()

# ---------------------------------------------------------------------------
# 3. Fallback: manual discovery
# ---------------------------------------------------------------------------
find_path(ONNXRuntime_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS
        "${ONNXRUNTIME_ROOT}/include"
        /usr/local/include
        /opt/homebrew/include
    NO_DEFAULT_PATH
)

find_library(ONNXRuntime_LIBRARY
    NAMES onnxruntime
    PATHS
        "${ONNXRUNTIME_ROOT}/lib"
        /usr/local/lib
        /opt/homebrew/lib
    NO_DEFAULT_PATH
)

# Read version from VERSION_NUMBER file if present
if(ONNXRUNTIME_ROOT AND EXISTS "${ONNXRUNTIME_ROOT}/VERSION_NUMBER")
    file(READ "${ONNXRUNTIME_ROOT}/VERSION_NUMBER" ONNXRuntime_VERSION)
    string(STRIP "${ONNXRuntime_VERSION}" ONNXRuntime_VERSION)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime
    REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR
    VERSION_VAR ONNXRuntime_VERSION
)

if(ONNXRuntime_FOUND AND NOT TARGET onnxruntime::onnxruntime)
    add_library(onnxruntime::onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
    )
    # On macOS the dylib uses @rpath
    if(APPLE)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_SONAME "@rpath/libonnxruntime.dylib"
        )
    endif()
endif()

set(ONNXRuntime_INCLUDE_DIRS "${ONNXRuntime_INCLUDE_DIR}")
set(ONNXRuntime_LIBRARIES "${ONNXRuntime_LIBRARY}")

mark_as_advanced(ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY)
