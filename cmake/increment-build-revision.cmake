# Increment build revision counter and update src/build_number.h.
# Called as a PRE_BUILD custom command — runs on every build.
#
# Input:  BUILD_NUMBER_FILE — the header file to update (src/build_number.h)

if(NOT DEFINED BUILD_NUMBER_FILE)
    message(FATAL_ERROR "BUILD_NUMBER_FILE must be defined")
endif()

# Read current build number from the header
set(REV 0)
if(EXISTS "${BUILD_NUMBER_FILE}")
    file(READ "${BUILD_NUMBER_FILE}" CONTENT)
    string(REGEX MATCH "JAMWIDE_BUILD_NUMBER ([0-9]+)" _match "${CONTENT}")
    if(CMAKE_MATCH_1)
        set(REV ${CMAKE_MATCH_1})
    endif()
endif()

# Increment
math(EXPR REV "${REV} + 1")

# Write back
file(WRITE "${BUILD_NUMBER_FILE}"
    "#pragma once\n"
    "#define JAMWIDE_BUILD_NUMBER ${REV}\n"
)

message(STATUS "JamWide build number: ${REV}")
