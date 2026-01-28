#
# Helper to bring in OSD-Lite via CMake FetchContent, with local override
#

include(FetchContent)

set(OSD_LITE_GIT_REPOSITORY "https://github.com/NVIDIA-RTX/OSD-Lite.git" CACHE STRING "Git repository for osd_lite")
set(OSD_LITE_GIT_TAG "c0c3404b62ee4b39c1a7c77d72a13ee9712f37ca" CACHE STRING "Git tag/branch/commit for osd_lite")

# Folder name for IDE organization of osd_lite targets
set(OSD_LITE_FOLDER_NAME "external/osd_lite" CACHE STRING "Folder name for grouping osd_lite targets in IDEs")

# If a local source is specified, prefer it (e.g., for offline builds)
if(DEFINED OSD_LITE_SOURCE_DIR AND EXISTS "${OSD_LITE_SOURCE_DIR}/CMakeLists.txt")
  # Normalize to absolute path
  get_filename_component(OSD_LITE_SOURCE_DIR "${OSD_LITE_SOURCE_DIR}" ABSOLUTE)
  message(STATUS "Using local OSD-Lite at: ${OSD_LITE_SOURCE_DIR}")
  add_subdirectory("${OSD_LITE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/osd_lite-build" EXCLUDE_FROM_ALL)
  # Ensure OSD-Lite headers are treated as system to avoid third-party warnings
  include_directories(SYSTEM
    ${OSD_LITE_SOURCE_DIR}
    ${OSD_LITE_SOURCE_DIR}/opensubdiv
    ${OSD_LITE_SOURCE_DIR}/opensubdiv/tmr)
else()
  # Fetch from git
  message(STATUS "Fetching OSD-Lite from: ${OSD_LITE_GIT_REPOSITORY} @ ${OSD_LITE_GIT_TAG}")
  FetchContent_Declare(
    osd_lite
    GIT_REPOSITORY "${OSD_LITE_GIT_REPOSITORY}"
    GIT_TAG        "${OSD_LITE_GIT_TAG}"
    GIT_PROGRESS   TRUE
  )
  FetchContent_MakeAvailable(osd_lite)

  # Resolve source dir for include path adjustments
  FetchContent_GetProperties(osd_lite)
  if(osd_lite_SOURCE_DIR)
    message(STATUS "Using fetched OSD-Lite at: ${osd_lite_SOURCE_DIR}")
    # Ensure OSD-Lite headers are treated as system to avoid third-party warnings
    include_directories(SYSTEM
      ${osd_lite_SOURCE_DIR}
      ${osd_lite_SOURCE_DIR}/opensubdiv
      ${osd_lite_SOURCE_DIR}/opensubdiv/tmr)
  endif()
endif()


