# run_test.cmake — CTest script for BASIC regression tests
# Called with: -DOPENDBC=... -DTEST_SCRIPT=... -DDIALECT=... -DEXPECTED_FILE=... -DOUTPUT_FILE=...

# Ensure output directory exists
get_filename_component(output_dir ${OUTPUT_FILE} DIRECTORY)
file(MAKE_DIRECTORY ${output_dir})

# Run the DBA script with log capture and frame limit
execute_process(
    COMMAND ${OPENDBC} run ${TEST_SCRIPT} --dialect ${DIALECT} --log-output ${OUTPUT_FILE} --max-frames 5
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 30
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "opendbc run failed (exit ${exit_code}):\n${stderr}")
endif()

# Read expected and actual output
file(READ ${EXPECTED_FILE} expected)
file(READ ${OUTPUT_FILE} actual)

# Compare
if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR
        "Output mismatch for ${TEST_SCRIPT}\n"
        "--- Expected ---\n${expected}\n"
        "--- Actual ---\n${actual}\n"
    )
endif()

message(STATUS "PASS: ${TEST_SCRIPT}")
