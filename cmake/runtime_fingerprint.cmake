# The runtime fingerprint hashes every source that shapes the runtime. Configure only lists those sources;
# the build hashes them, so editing one refreshes the fingerprint without configuring again.
file(GLOB_RECURSE OPENDBC_RUNTIME_FINGERPRINT_INPUTS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "src/*.cpp" "src/*.hpp" "src/*.inc" "src/*.mm"
    "tools/studio/src/*.cpp" "tools/studio/src/*.hpp" "tools/studio/src/*.mm"
    "tools/runner/*.cpp" "tools/runner/*.hpp" "tools/runner/*.mm" "tools/runner/*.in"
    "sdk/*.h"
    "shaders/*.hlsl" "shaders/*.spv" "shaders/*.msl"
    "cmake/*.cmake" "cmake/*.patch")
list(APPEND OPENDBC_RUNTIME_FINGERPRINT_INPUTS
    "CMakeLists.txt"
    "src/engine/CMakeLists.txt"
    "src/interpreter/CMakeLists.txt"
    "src/language/CMakeLists.txt"
    "src/vm/CMakeLists.txt"
    "tools/studio/CMakeLists.txt"
    "tools/runner/CMakeLists.txt"
    "resources/midi/VintageDreamsWaves-v2.sf2")
list(REMOVE_DUPLICATES OPENDBC_RUNTIME_FINGERPRINT_INPUTS)
list(SORT OPENDBC_RUNTIME_FINGERPRINT_INPUTS)

set(OPENDBC_RUNTIME_MANIFEST "${CMAKE_BINARY_DIR}/opendbc-runtime.json")
set(OPENDBC_RUNTIME_FINGERPRINT_DIR "${CMAKE_BINARY_DIR}/generated/runtime")
set(OPENDBC_RUNTIME_FINGERPRINT_HEADER "${OPENDBC_RUNTIME_FINGERPRINT_DIR}/opendbc_runtime_fingerprint.h")
set(OPENDBC_RUNTIME_INPUT_LIST "${CMAKE_BINARY_DIR}/opendbc-runtime-inputs.txt")
string(JOIN "\n" OPENDBC_RUNTIME_INPUT_LINES ${OPENDBC_RUNTIME_FINGERPRINT_INPUTS})
# Rewritten only when the list changes, so an unchanged configure leaves the fingerprint step alone.
file(CONFIGURE OUTPUT "${OPENDBC_RUNTIME_INPUT_LIST}" CONTENT "${OPENDBC_RUNTIME_INPUT_LINES}\n" @ONLY)

set(OPENDBC_RUNTIME_FINGERPRINT_COMMAND
    "${CMAKE_COMMAND}"
    "-DOPENDBC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DOPENDBC_RUNTIME_INPUT_LIST=${OPENDBC_RUNTIME_INPUT_LIST}"
    "-DOPENDBC_VERSION=${OPENDBC_VERSION}"
    "-DOPENDBC_REVISION=${OPENDBC_REVISION}"
    "-DOPENDBC_RUNTIME_MANIFEST=${OPENDBC_RUNTIME_MANIFEST}"
    "-DOPENDBC_RUNTIME_FINGERPRINT_HEADER=${OPENDBC_RUNTIME_FINGERPRINT_HEADER}"
    -P "${CMAKE_CURRENT_LIST_DIR}/write_runtime_fingerprint.cmake")

# Configure-time checks of prebuilt export hosts read the manifest, so configure writes it as well.
execute_process(COMMAND ${OPENDBC_RUNTIME_FINGERPRINT_COMMAND} RESULT_VARIABLE OPENDBC_RUNTIME_FINGERPRINT_RESULT)
if(NOT OPENDBC_RUNTIME_FINGERPRINT_RESULT EQUAL 0)
    message(FATAL_ERROR "Cannot compute the OpenDBC runtime fingerprint")
endif()

list(TRANSFORM OPENDBC_RUNTIME_FINGERPRINT_INPUTS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
    OUTPUT_VARIABLE OPENDBC_RUNTIME_FINGERPRINT_DEPENDS)
add_custom_command(
    OUTPUT "${OPENDBC_RUNTIME_MANIFEST}" "${OPENDBC_RUNTIME_FINGERPRINT_HEADER}"
    COMMAND ${OPENDBC_RUNTIME_FINGERPRINT_COMMAND}
    DEPENDS ${OPENDBC_RUNTIME_FINGERPRINT_DEPENDS} "${OPENDBC_RUNTIME_INPUT_LIST}"
            "${CMAKE_CURRENT_LIST_DIR}/write_runtime_fingerprint.cmake"
            "${CMAKE_CURRENT_LIST_DIR}/opendbc-runtime.json.in"
    COMMENT "Hashing the OpenDBC runtime sources"
    VERBATIM)
add_custom_target(opendbc_runtime_fingerprint
    DEPENDS "${OPENDBC_RUNTIME_MANIFEST}" "${OPENDBC_RUNTIME_FINGERPRINT_HEADER}")
