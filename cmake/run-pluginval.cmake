# Downloads pluginval if needed, then validates the VST3 bundle.
# Called via: cmake --build build --target validate
#
# Variables passed in from CMakeLists.txt:
#   PLUGINVAL_URL  - Download URL for platform-specific pluginval zip
#   PLUGINVAL_EXE  - Expected path to pluginval executable after extraction
#   PLUGINVAL_DIR  - Directory to store pluginval
#   VST3_PATH      - Path to the VST3 bundle to validate

if(NOT EXISTS "${PLUGINVAL_EXE}")
    message(STATUS "Downloading pluginval...")
    file(MAKE_DIRECTORY "${PLUGINVAL_DIR}")
    file(DOWNLOAD "${PLUGINVAL_URL}" "${PLUGINVAL_DIR}/pluginval.zip"
         STATUS dl_status SHOW_PROGRESS)
    list(GET dl_status 0 dl_code)
    if(NOT dl_code EQUAL 0)
        list(GET dl_status 1 dl_msg)
        message(FATAL_ERROR "Failed to download pluginval: ${dl_msg}")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf "${PLUGINVAL_DIR}/pluginval.zip"
        WORKING_DIRECTORY "${PLUGINVAL_DIR}"
        RESULT_VARIABLE unzip_result)
    if(NOT unzip_result EQUAL 0)
        message(FATAL_ERROR "Failed to extract pluginval")
    endif()
    message(STATUS "pluginval downloaded to ${PLUGINVAL_DIR}")
endif()

message(STATUS "Validating: ${VST3_PATH}")
execute_process(
    COMMAND "${PLUGINVAL_EXE}"
            --validate-in-process
            --strictness-level 5
            --timeout-ms 120000
            --validate "${VST3_PATH}"
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "pluginval validation FAILED (exit code: ${result})")
endif()

message(STATUS "pluginval validation PASSED")
