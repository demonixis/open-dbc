if(NOT DEFINED OPENDBC_GLSLANG OR NOT EXISTS "${OPENDBC_GLSLANG}")
    message(FATAL_ERROR "OPENDBC_GLSLANG must point to the pinned glslangValidator")
endif()
if(NOT DEFINED OPENDBC_SHADER_SOURCE_DIR OR NOT DEFINED OPENDBC_SHADER_VERIFY_DIR)
    message(FATAL_ERROR "shader source and verification directories are required")
endif()

file(REMOVE_RECURSE "${OPENDBC_SHADER_VERIFY_DIR}")
file(MAKE_DIRECTORY "${OPENDBC_SHADER_VERIFY_DIR}")
foreach(OPENDBC_SHADER IN ITEMS dbclassic_tri vr_transfer)
    foreach(OPENDBC_STAGE IN ITEMS vert frag)
        set(OPENDBC_SOURCE "${OPENDBC_SHADER_SOURCE_DIR}/${OPENDBC_SHADER}.${OPENDBC_STAGE}.glsl")
        set(OPENDBC_EXPECTED "${OPENDBC_SHADER_SOURCE_DIR}/${OPENDBC_SHADER}.${OPENDBC_STAGE}.spv")
        set(OPENDBC_GENERATED "${OPENDBC_SHADER_VERIFY_DIR}/${OPENDBC_SHADER}.${OPENDBC_STAGE}.spv")
        execute_process(
            COMMAND "${OPENDBC_GLSLANG}" -V --target-env vulkan1.0 -S "${OPENDBC_STAGE}"
                    "${OPENDBC_SOURCE}" -o "${OPENDBC_GENERATED}"
            RESULT_VARIABLE OPENDBC_RESULT
            OUTPUT_VARIABLE OPENDBC_OUTPUT
            ERROR_VARIABLE OPENDBC_ERROR)
        if(NOT OPENDBC_RESULT EQUAL 0)
            message(FATAL_ERROR "glslangValidator failed: ${OPENDBC_OUTPUT}${OPENDBC_ERROR}")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files "${OPENDBC_EXPECTED}" "${OPENDBC_GENERATED}"
            RESULT_VARIABLE OPENDBC_COMPARE)
        if(NOT OPENDBC_COMPARE EQUAL 0)
            message(FATAL_ERROR "canonical shader is stale: ${OPENDBC_EXPECTED}")
        endif()
    endforeach()
endforeach()
message(STATUS "Canonical SPIR-V shaders match pinned glslang")
