add_executable(opendbc_shaderc opendbc_shaderc.cpp)
target_link_libraries(opendbc_shaderc PRIVATE SDL3::SDL3 SDL3_shadercross::SDL3_shadercross)
dbc_enable_warnings(opendbc_shaderc)

foreach(OPENDBC_SHADER_MODE IN ITEMS verify update)
    add_custom_target(opendbc_${OPENDBC_SHADER_MODE}_shaders
        COMMAND ${CMAKE_COMMAND}
            -DOPENDBC_SHADERC=$<TARGET_FILE:opendbc_shaderc>
            -DOPENDBC_SHADER_SOURCE_DIR=${OPENDBC_SHADER_SOURCE_DIR}
            -DOPENDBC_SHADER_VERIFY_DIR=${CMAKE_BINARY_DIR}/verified-shaders
            -DOPENDBC_SHADER_MODE=${OPENDBC_SHADER_MODE}
            -P ${CMAKE_SOURCE_DIR}/cmake/verify_shaders.cmake
        DEPENDS opendbc_shaderc
        COMMENT "Compile the engine HLSL shaders with the pinned DXC (${OPENDBC_SHADER_MODE})"
        VERBATIM)
endforeach()
