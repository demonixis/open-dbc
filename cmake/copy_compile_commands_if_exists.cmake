if(EXISTS "${INPUT_FILE}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${INPUT_FILE}" "${OUTPUT_FILE}"
    RESULT_VARIABLE copy_result
  )
  if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to copy compile_commands.json.")
  endif()
else()
  message(STATUS "compile_commands.json is not available for this generator yet.")
endif()
