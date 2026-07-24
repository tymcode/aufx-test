# Increments the build number in VERSION and regenerates AppVersion.h/.cpp.
# Expected cache vars: AUFX_VERSION_FILE, AUFX_GEN_DIR

if (NOT DEFINED AUFX_VERSION_FILE OR NOT DEFINED AUFX_GEN_DIR)
  message (FATAL_ERROR "BumpVersion.cmake requires AUFX_VERSION_FILE and AUFX_GEN_DIR")
endif()

if (NOT EXISTS "${AUFX_VERSION_FILE}")
  message (FATAL_ERROR "VERSION file not found: ${AUFX_VERSION_FILE}")
endif()

file (STRINGS "${AUFX_VERSION_FILE}" _version_lines)
list (LENGTH _version_lines _n)
if (_n LESS 1)
  message (FATAL_ERROR "VERSION file is empty: ${AUFX_VERSION_FILE}")
endif()

list (GET _version_lines 0 AUFX_SEMVER)
string (STRIP "${AUFX_SEMVER}" AUFX_SEMVER)

set (AUFX_BUILD 0)
if (_n GREATER 1)
  list (GET _version_lines 1 AUFX_BUILD)
  string (STRIP "${AUFX_BUILD}" AUFX_BUILD)
endif()

if (NOT AUFX_BUILD MATCHES "^[0-9]+$")
  set (AUFX_BUILD 0)
endif()

math (EXPR AUFX_BUILD "${AUFX_BUILD} + 1")

file (WRITE "${AUFX_VERSION_FILE}" "${AUFX_SEMVER}\n${AUFX_BUILD}\n")

string (TIMESTAMP AUFX_BUILD_YEAR "%Y")
set (AUFX_VERSION_FULL "${AUFX_SEMVER}+${AUFX_BUILD}")

file (MAKE_DIRECTORY "${AUFX_GEN_DIR}")

configure_file (
  "${CMAKE_CURRENT_LIST_DIR}/AppVersion.h.in"
  "${AUFX_GEN_DIR}/AppVersion.h"
  @ONLY)

# Write .cpp with UTF-8 copyright (©) via a here-string.
file (WRITE "${AUFX_GEN_DIR}/AppVersion.cpp"
"#include \"AppVersion.h\"

namespace aufx_version
{
    const char* appName() { return AUFX_APP_NAME; }
    const char* versionString() { return AUFX_VERSION_STRING; }
    const char* buildYear() { return AUFX_BUILD_YEAR; }
    int buildNumber() { return AUFX_VERSION_BUILD; }
}
")

message (STATUS "AU Effects Explorer version -> ${AUFX_VERSION_FULL}")
