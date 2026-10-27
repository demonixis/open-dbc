# CTest script: builds a BASIC test into a program image with opendbc, runs it with opendbc-runner
# and compares the log with the same golden as the source run.
# Called with: -DOPENDBC=... -DRUNNER=... -DTEST_SCRIPT=... -DDIALECT=... -DEXPECTED_FILE=... -DOUTPUT_FILE=...

get_filename_component(output_dir ${OUTPUT_FILE} DIRECTORY)
get_filename_component(asset_root ${TEST_SCRIPT} DIRECTORY)
get_filename_component(test_name ${TEST_SCRIPT} NAME_WE)
file(MAKE_DIRECTORY ${output_dir})
set(image "${output_dir}/${test_name}.obc")

execute_process(
    COMMAND ${OPENDBC} build ${TEST_SCRIPT} --dialect ${DIALECT} --output ${image}
    RESULT_VARIABLE exit_code
    ERROR_VARIABLE stderr
    TIMEOUT 30
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "opendbc build failed (exit ${exit_code}):\n${stderr}")
endif()

execute_process(
    COMMAND ${RUNNER} ${image} --asset-root ${asset_root} --log-output ${OUTPUT_FILE} --max-frames 5
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 30
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "opendbc-runner failed (exit ${exit_code}):\n${stderr}")
endif()

file(READ ${EXPECTED_FILE} expected)
file(READ ${OUTPUT_FILE} actual)
if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR
        "Output mismatch for ${TEST_SCRIPT} run from its program image\n"
        "--- Expected ---\n${expected}\n"
        "--- Actual ---\n${actual}\n"
    )
endif()
message(STATUS "PASS: ${TEST_SCRIPT} (program image)")
