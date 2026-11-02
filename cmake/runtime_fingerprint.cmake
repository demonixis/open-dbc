file(GLOB_RECURSE OPENDBC_RUNTIME_FINGERPRINT_INPUTS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "src/*.cpp" "src/*.hpp" "src/*.inc" "src/*.mm"
    "tools/studio/src/*.cpp" "tools/studio/src/*.hpp" "tools/studio/src/*.mm"
    "tools/runner/*.cpp" "tools/runner/*.hpp" "tools/runner/*.mm" "tools/runner/*.in"
    "shaders/*.hlsl" "shaders/*.spv" "shaders/*.msl"
    "cmake/*.cmake" "cmake/*.patch")
list(APPEND OPENDBC_RUNTIME_FINGERPRINT_INPUTS
    "CMakeLists.txt"
    "src/engine/CMakeLists.txt"
    "src/interpreter/CMakeLists.txt"
    "src/language/CMakeLists.txt"
    "src/vm/CMakeLists.txt"
    "tools/studio/CMakeLists.txt"
    "tools/runner/CMakeLists.txt")
list(SORT OPENDBC_RUNTIME_FINGERPRINT_INPUTS)
set(OPENDBC_RUNTIME_FINGERPRINT_MATERIAL "OpenDBC-runtime-v1\n")
foreach(OPENDBC_RUNTIME_INPUT IN LISTS OPENDBC_RUNTIME_FINGERPRINT_INPUTS)
    # CONFIGURE_DEPENDS detects added/removed paths; this also tracks edits to existing inputs.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${OPENDBC_RUNTIME_INPUT}")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${OPENDBC_RUNTIME_INPUT}" OPENDBC_RUNTIME_INPUT_HASH)
    string(APPEND OPENDBC_RUNTIME_FINGERPRINT_MATERIAL
        "${OPENDBC_RUNTIME_INPUT}:${OPENDBC_RUNTIME_INPUT_HASH}\n")
endforeach()
string(SHA256 OPENDBC_RUNTIME_FINGERPRINT "${OPENDBC_RUNTIME_FINGERPRINT_MATERIAL}")
