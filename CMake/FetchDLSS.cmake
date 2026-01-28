#
# FetchDLSS.cmake
#
# Fetches the NVIDIA DLSS loader package (aka NGX SDK) via FetchContent.
# This library is used to load the DLSS-RR model at runtime.
#
# Creates imported target:
#   DLSS::DLSS - The DLSS loader library
#
# CRT Selection (Windows):
#   The SDK provides both static (/MT) and dynamic (/MD) CRT versions.
#   This module automatically uses the version matching your project's CRT setting.
#
# Runtime Dependencies:
#   The DLSS runtime library (DLL/SO) must be beside the executable.
#   Use dlss_setup_runtime_dependencies(TARGET) after linking to set this up.
#
# Local Override:
#   Set DLSS_ROOT to use a local SDK instead of fetching.
#

include(FetchContent)

set(DLSS_VERSION "310.5.3" CACHE STRING "DLSS SDK version")
set(DLSS_URL "https://github.com/NVIDIA/DLSS/archive/refs/tags/v${DLSS_VERSION}.tar.gz" CACHE STRING "URL for DLSS SDK archive")
set(DLSS_URL_HASH "6b54a684b5b31e819a51742ad534abb4e8cdada76572f061a5d3149c7432a0a1" CACHE STRING "SHA256 hash for DLSS archive")

# Folder name for IDE organization
set(DLSS_FOLDER_NAME "external/dlss" CACHE STRING "Folder name for grouping DLSS in IDEs")

# If a local source is specified, prefer it (e.g., for offline builds or existing SDK installs)
if(DEFINED DLSS_ROOT AND EXISTS "${DLSS_ROOT}/include/nvsdk_ngx.h")
    get_filename_component(DLSS_ROOT "${DLSS_ROOT}" ABSOLUTE)
    message(STATUS "DLSS: Using local SDK at: ${DLSS_ROOT}")
    set(_dlss_source_dir "${DLSS_ROOT}")
else()
    # Fetch from archive
    message(STATUS "DLSS: Fetching SDK v${DLSS_VERSION} from GitHub...")
    if(DLSS_URL_HASH)
        FetchContent_Declare(dlss URL "${DLSS_URL}" URL_HASH SHA256=${DLSS_URL_HASH})
    else()
        FetchContent_Declare(dlss URL "${DLSS_URL}")
    endif()
    FetchContent_MakeAvailable(dlss)
    FetchContent_GetProperties(dlss)
    set(_dlss_source_dir "${dlss_SOURCE_DIR}")
    message(STATUS "DLSS: Fetched to: ${_dlss_source_dir}")
endif()

# Locate components within the SDK
set(DLSS_INCLUDE_DIR "${_dlss_source_dir}/include")

if(WIN32)
    # Determine CRT flavor (dynamic /MD vs static /MT)
    set(_dlss_crt_flavor "d")  # dynamic by default
    if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "MultiThreaded[^D]*$")
        set(_dlss_crt_flavor "s")
    endif()

    set(DLSS_LIBRARY_RELEASE "${_dlss_source_dir}/lib/Windows_x86_64/x64/nvsdk_ngx_${_dlss_crt_flavor}.lib")
    set(DLSS_LIBRARY_DEBUG "${_dlss_source_dir}/lib/Windows_x86_64/x64/nvsdk_ngx_${_dlss_crt_flavor}_dbg.lib")
    set(DLSS_RUNTIME_LIBRARY_DEVELOP "${_dlss_source_dir}/lib/Windows_x86_64/dev/nvngx_dlssd.dll")
    set(DLSS_RUNTIME_LIBRARY_RELEASE "${_dlss_source_dir}/lib/Windows_x86_64/rel/nvngx_dlssd.dll")
else()
    set(DLSS_LIBRARY_RELEASE "${_dlss_source_dir}/lib/Linux_x86_64/libnvsdk_ngx.a")
    set(DLSS_LIBRARY_DEBUG "${DLSS_LIBRARY_RELEASE}")
    file(GLOB _dlss_dev_so "${_dlss_source_dir}/lib/Linux_x86_64/dev/libnvidia-ngx-dlssd.so*")
    file(GLOB _dlss_rel_so "${_dlss_source_dir}/lib/Linux_x86_64/rel/libnvidia-ngx-dlssd.so*")
    if(_dlss_dev_so)
        list(GET _dlss_dev_so 0 DLSS_RUNTIME_LIBRARY_DEVELOP)
    endif()
    if(_dlss_rel_so)
        list(GET _dlss_rel_so 0 DLSS_RUNTIME_LIBRARY_RELEASE)
    endif()
endif()

# Validate we found the essentials
if(EXISTS "${DLSS_INCLUDE_DIR}/nvsdk_ngx.h" AND EXISTS "${DLSS_LIBRARY_RELEASE}")
    set(DLSS_FOUND TRUE)
else()
    set(DLSS_FOUND FALSE)
    message(WARNING "DLSS: SDK structure not as expected. Include: ${DLSS_INCLUDE_DIR}, Lib: ${DLSS_LIBRARY_RELEASE}")
    return()
endif()

# Create imported target
if(NOT TARGET DLSS::DLSS)
    add_library(DLSS::DLSS STATIC IMPORTED GLOBAL)
    set_target_properties(DLSS::DLSS PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX;CUDA"
        INTERFACE_COMPILE_DEFINITIONS "DLSS_ENABLED"
        INTERFACE_INCLUDE_DIRECTORIES "${DLSS_INCLUDE_DIR}"
        IMPORTED_LOCATION_RELEASE "${DLSS_LIBRARY_RELEASE}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${DLSS_LIBRARY_RELEASE}"
        IMPORTED_LOCATION_DEBUG "${DLSS_LIBRARY_DEBUG}"
    )
endif()

message(STATUS "DLSS: Found SDK v${DLSS_VERSION}")
message(STATUS "DLSS: Release library: ${DLSS_LIBRARY_RELEASE}")
message(STATUS "DLSS: Debug library: ${DLSS_LIBRARY_DEBUG}")

# Function to copy runtime library beside executable
function(dlss_setup_runtime_dependencies TARGET)
    if(NOT DLSS_RUNTIME_LIBRARY_DEVELOP AND NOT DLSS_RUNTIME_LIBRARY_RELEASE)
        message(WARNING "DLSS: Runtime libraries not found, skipping runtime setup")
        return()
    endif()

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,${DLSS_RUNTIME_LIBRARY_DEVELOP},${DLSS_RUNTIME_LIBRARY_RELEASE}>"
            "$<TARGET_FILE_DIR:${TARGET}>/$<IF:$<PLATFORM_ID:Windows>,nvngx_dlssd.dll,libnvidia-ngx-dlssd.so>"
        COMMENT "DLSS: Copying runtime library"
    )
endfunction()
