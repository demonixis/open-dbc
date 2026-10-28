include(doctest)

set(ENGINE_SRC_DIR ${CMAKE_SOURCE_DIR}/src)

add_executable(test_language_profile test_language_profile.cpp)
target_link_libraries(test_language_profile PRIVATE doctest_with_main dbc_language)
doctest_discover_tests(test_language_profile PROPERTIES LABELS "unit-language")

add_executable(test_language_catalogue test_language_catalogue.cpp)
target_compile_definitions(test_language_catalogue PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_language_catalogue PRIVATE doctest_with_main dbc_language)
doctest_discover_tests(test_language_catalogue PROPERTIES LABELS "unit-language-qualification")

add_executable(test_frontend test_frontend.cpp)
target_compile_definitions(test_frontend PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_frontend PRIVATE doctest_with_main opendbc_vm)
doctest_discover_tests(test_frontend PROPERTIES LABELS "unit-language-frontend-qualification")

add_executable(test_vm test_vm.cpp)
target_compile_definitions(test_vm PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_vm PRIVATE doctest_with_main opendbc_vm)
doctest_discover_tests(test_vm PROPERTIES LABELS "unit-language-vm")

if(NOT ANDROID AND NOT IOS)
    add_executable(test_studio_language
        test_studio_language.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/code_outline.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/command_database.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/program_info.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/reference.cpp
    )
    target_include_directories(test_studio_language PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_studio_language PRIVATE doctest_with_main dbc_language)
    doctest_discover_tests(test_studio_language PROPERTIES LABELS "unit-language-studio")

    add_executable(test_studio_editor
        test_studio_editor.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/command_database.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/language.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/dba/reference.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/panels/editor_panel.cpp
    )
    target_include_directories(test_studio_editor PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_studio_editor PRIVATE doctest_with_main dbc_language imgui_texteditor)
    doctest_discover_tests(test_studio_editor PROPERTIES LABELS "unit-studio")

    add_executable(test_studio_scene
        test_studio_scene.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/scene_codegen.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/scene_document.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/scene_integration.cpp
    )
    target_include_directories(test_studio_scene PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_compile_definitions(test_studio_scene PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_studio_scene PRIVATE doctest_with_main dbc_language glm::glm)
    doctest_discover_tests(test_studio_scene PROPERTIES LABELS "unit-studio")

    add_executable(test_studio_scene_render
        test_studio_scene_render.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/gpu_texture.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/scene_document.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/scene_geometry.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/scene_renderer.cpp
    )
    add_dependencies(test_studio_scene_render opendbc_scene_shaders)
    target_include_directories(test_studio_scene_render PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src
                               ${CMAKE_BINARY_DIR}/tools/studio/generated)
    target_compile_definitions(test_studio_scene_render PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                               TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/tests")
    target_link_libraries(test_studio_scene_render PRIVATE doctest_with_main dbc_engine dbc_language)
    doctest_discover_tests(test_studio_scene_render PROPERTIES LABELS "unit-studio"
                           RESOURCE_LOCK "opendbc_runtime")

    add_executable(test_studio_media
        test_studio_media.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/media_catalogue.cpp
    )
    target_include_directories(test_studio_media PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_studio_media PRIVATE doctest_with_main)
    doctest_discover_tests(test_studio_media PROPERTIES LABELS "unit-studio")

    add_executable(test_studio_run_plan
        test_studio_run_plan.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/active_run_planner.cpp
    )
    target_include_directories(test_studio_run_plan PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_studio_run_plan PRIVATE doctest_with_main dbc_interpreter)
    doctest_discover_tests(test_studio_run_plan PROPERTIES LABELS "unit-studio")
endif()

# --- Test: engine math ---
add_executable(test_engine_math test_engine_math.cpp)
target_include_directories(test_engine_math PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_engine_math PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_engine_math PROPERTIES LABELS "unit")

add_executable(test_renderer test_renderer.cpp)
target_include_directories(test_renderer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_renderer PRIVATE doctest_with_main dbc_engine)
dbc_enable_warnings(test_renderer)
doctest_discover_tests(test_renderer PROPERTIES LABELS "unit-renderer" RESOURCE_LOCK "opendbc_runtime")

# --- Test: collision math ---
add_executable(test_collision_math test_collision_math.cpp)
target_include_directories(test_collision_math PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_collision_math PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_collision_math PROPERTIES LABELS "unit")

add_executable(test_bsp_world test_bsp_world.cpp)
target_compile_definitions(test_bsp_world PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(test_bsp_world PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_bsp_world PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_bsp_world PROPERTIES LABELS "unit")

add_executable(test_terrain_mesh test_terrain_mesh.cpp)
target_include_directories(test_terrain_mesh PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_terrain_mesh PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_terrain_mesh PROPERTIES LABELS "unit")

# --- Test: typed engine builtins ---
add_executable(test_engine_builtins test_engine_builtins.cpp)
target_compile_definitions(test_engine_builtins PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_engine_builtins PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_engine_builtins PROPERTIES LABELS "unit" RESOURCE_LOCK "opendbc_runtime")

add_executable(test_input_haptics test_input_haptics.cpp)
target_include_directories(test_input_haptics PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_input_haptics PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_input_haptics PROPERTIES LABELS "unit" RESOURCE_LOCK "opendbc_runtime")

add_executable(test_command_line test_command_line.cpp)
target_link_libraries(test_command_line PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_command_line PROPERTIES LABELS "unit-cli")

add_executable(test_program_image test_program_image.cpp)
target_link_libraries(test_program_image PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_program_image PROPERTIES LABELS "unit-cli" RESOURCE_LOCK "opendbc_runtime")

add_executable(test_project_resolver test_project_resolver.cpp)
target_link_libraries(test_project_resolver PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_project_resolver PROPERTIES LABELS "unit-cli;unit-language")

# --- Test: debug system ---
add_executable(test_debug_system test_debug_system.cpp)
target_include_directories(test_debug_system PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_debug_system PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_debug_system PROPERTIES LABELS "unit" RESOURCE_LOCK "opendbc_runtime")

if(TARGET nlohmann_json::nlohmann_json AND NOT ANDROID AND NOT IOS)
    add_executable(test_dxc_optimizer test_dxc_optimizer.cpp)
    target_include_directories(test_dxc_optimizer SYSTEM PRIVATE
        ${sdl3_shadercross_SOURCE_DIR}/external/DirectXShaderCompiler/include)
    target_compile_definitions(test_dxc_optimizer PRIVATE DXC_API_IMPORT=)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # DXC's public COM declarations use Clang's UUID extensions.
        target_compile_options(test_dxc_optimizer PRIVATE -fms-extensions)
    endif()
    target_link_libraries(test_dxc_optimizer PRIVATE
        doctest_with_main SDL3_shadercross::SDL3_shadercross)
    doctest_discover_tests(test_dxc_optimizer PROPERTIES LABELS "unit-studio-shaders")

    add_executable(test_shader_compiler
        test_shader_compiler.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/shader_compiler.cpp
    )
    target_include_directories(test_shader_compiler PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_shader_compiler PRIVATE
        doctest_with_main
        dbc_interpreter
        SDL3::SDL3
        SDL3_shadercross::SDL3_shadercross
        spirv-cross-core
        nlohmann_json::nlohmann_json
    )
    doctest_discover_tests(test_shader_compiler PROPERTIES LABELS "unit-studio-shaders" RESOURCE_LOCK "opendbc_runtime")

    add_executable(test_project_settings
        test_project_settings.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/settings.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/paths.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/project_settings.cpp
    )
    target_include_directories(test_project_settings PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_project_settings PRIVATE
        doctest_with_main
        dbc_interpreter
        nlohmann_json::nlohmann_json
    )
    doctest_discover_tests(test_project_settings PROPERTIES LABELS "unit-studio")

    add_executable(test_studio_new_project
        test_studio_new_project.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/new_project.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/project_settings.cpp
    )
    target_include_directories(test_studio_new_project PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_studio_new_project PRIVATE
        doctest_with_main
        dbc_interpreter
        nlohmann_json::nlohmann_json
    )
    doctest_discover_tests(test_studio_new_project PROPERTIES LABELS "unit-studio")

    add_executable(test_artifact_export
        test_artifact_export.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/artifact_export_service.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/project_settings.cpp
    )
    target_include_directories(test_artifact_export PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_artifact_export PRIVATE
        doctest_with_main
        dbc_interpreter
        nlohmann_json::nlohmann_json
    )
    target_compile_definitions(test_artifact_export PRIVATE
        OPENDBC_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    doctest_discover_tests(test_artifact_export PROPERTIES LABELS "unit-studio-packaging" RESOURCE_LOCK "opendbc_runtime")
endif()

# The tests are OpenDBC code too.
foreach(test_target IN ITEMS test_language_profile test_language_catalogue test_frontend test_vm test_studio_language
        test_studio_run_plan
        test_engine_math test_collision_math test_bsp_world test_terrain_mesh test_engine_builtins test_input_haptics test_command_line
        test_project_resolver test_debug_system
        test_shader_compiler test_project_settings test_studio_new_project test_artifact_export)
    if(TARGET ${test_target})
        dbc_enable_warnings(${test_target})
    endif()
endforeach()

if(TARGET OpenXR::headers)
    add_executable(test_vr test_vr.cpp)
    target_link_libraries(test_vr PRIVATE doctest_with_main dbc_interpreter OpenXR::headers)
    target_compile_definitions(test_vr PRIVATE OPENDBC_OPENXR=1)
    dbc_enable_warnings(test_vr)
    doctest_discover_tests(test_vr PROPERTIES LABELS "unit-vr" RESOURCE_LOCK "opendbc_runtime")
endif()
