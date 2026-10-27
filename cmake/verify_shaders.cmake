if(NOT DEFINED OPENDBC_SHADERC OR NOT EXISTS "${OPENDBC_SHADERC}")
    message(FATAL_ERROR "OPENDBC_SHADERC must point to opendbc_shaderc")
endif()
if(NOT DEFINED OPENDBC_SHADER_SOURCE_DIR OR NOT DEFINED OPENDBC_SHADER_VERIFY_DIR)
    message(FATAL_ERROR "shader source and verification directories are required")
endif()
if(NOT OPENDBC_SHADER_MODE MATCHES "^(verify|update)$")
    message(FATAL_ERROR "OPENDBC_SHADER_MODE must be verify or update")
endif()

file(REMOVE_RECURSE "${OPENDBC_SHADER_VERIFY_DIR}")
file(MAKE_DIRECTORY "${OPENDBC_SHADER_VERIFY_DIR}")
foreach(OPENDBC_SHADER IN ITEMS dbclassic_tri vr_transfer)
    execute_process(
        COMMAND "${OPENDBC_SHADERC}" "${OPENDBC_SHADER_SOURCE_DIR}/${OPENDBC_SHADER}.hlsl"
                "${OPENDBC_SHADER_VERIFY_DIR}/${OPENDBC_SHADER}"
        RESULT_VARIABLE OPENDBC_RESULT
        OUTPUT_VARIABLE OPENDBC_OUTPUT
        ERROR_VARIABLE OPENDBC_ERROR)
    if(NOT OPENDBC_RESULT EQUAL 0)
        message(FATAL_ERROR "opendbc_shaderc failed: ${OPENDBC_OUTPUT}${OPENDBC_ERROR}")
    endif()
    foreach(OPENDBC_OUTPUT_NAME IN ITEMS vert.spv frag.spv vert.msl frag.msl)
        set(OPENDBC_EXPECTED "${OPENDBC_SHADER_SOURCE_DIR}/${OPENDBC_SHADER}.${OPENDBC_OUTPUT_NAME}")
        set(OPENDBC_GENERATED "${OPENDBC_SHADER_VERIFY_DIR}/${OPENDBC_SHADER}.${OPENDBC_OUTPUT_NAME}")
        if(OPENDBC_SHADER_MODE STREQUAL "update")
            file(COPY_FILE "${OPENDBC_GENERATED}" "${OPENDBC_EXPECTED}" ONLY_IF_DIFFERENT)
            continue()
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files "${OPENDBC_EXPECTED}" "${OPENDBC_GENERATED}"
            RESULT_VARIABLE OPENDBC_COMPARE)
        if(NOT OPENDBC_COMPARE EQUAL 0)
            message(FATAL_ERROR "canonical shader is stale: ${OPENDBC_EXPECTED}")
        endif()
    endforeach()
endforeach()
if(OPENDBC_SHADER_MODE STREQUAL "update")
    message(STATUS "Canonical shaders regenerated with the pinned DXC")
else()
    message(STATUS "Canonical shaders match the pinned DXC")
endif()
