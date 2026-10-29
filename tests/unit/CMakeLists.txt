include(doctest)

set(ENGINE_SRC_DIR ${CMAKE_SOURCE_DIR}/src)

add_executable(test_language_profile test_language_profile.cpp)
target_link_libraries(test_language_profile PRIVATE doctest_with_main dbc_language)
doctest_discover_tests(test_language_profile PROPERTIES LABELS "unit-language")

add_executable(test_language_catalogue test_language_catalogue.cpp)
target_compile_definitions(test_language_catalogue PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_language_catalogue PRIVATE doctest_with_main dbc_language)
doctest_discover_tests(test_language_catalogue PROPERTIES LABELS "unit-language-qualification")

find_package(Threads REQUIRED)
add_executable(test_frontend test_frontend.cpp)
target_compile_definitions(test_frontend PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_frontend PRIVATE doctest_with_main opendbc_vm Threads::Threads)
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
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/text_encoding.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/recovery.cpp
    )
    target_include_directories(test_studio_editor PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_studio_editor PRIVATE doctest_with_main dbc_language imgui_texteditor
        nlohmann_json::nlohmann_json)
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
        ${CMAKE_SOURCE_DIR}/tools/studio/src/scene/heightmap.cpp
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
    doctest_discover_tests(test_studio_scene_render PROPERTIES LABELS "unit-studio")

    add_executable(test_studio_media
        test_studio_media.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/media_catalogue.cpp
    )
    target_include_directories(test_studio_media PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src ${ENGINE_SRC_DIR})
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
doctest_discover_tests(test_renderer PROPERTIES LABELS "unit-renderer")

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
doctest_discover_tests(test_engine_builtins PROPERTIES LABELS "unit")

if(WIN32)
    add_executable(test_video_decoder test_video_decoder_windows.cpp
                   ${CMAKE_SOURCE_DIR}/src/engine/systems/video_decoder_media_foundation.cpp)
    target_include_directories(test_video_decoder PRIVATE ${ENGINE_SRC_DIR})
    target_compile_definitions(test_video_decoder PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_video_decoder PRIVATE doctest_with_main spdlog::spdlog_header_only SDL3::SDL3
                          mfplat mfuuid ole32 oleaut32 windowscodecs)
    dbc_enable_warnings(test_video_decoder)
    doctest_discover_tests(test_video_decoder PROPERTIES LABELS "unit-video")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(test_engine_builtins PRIVATE PkgConfig::GSTREAMER)
    add_executable(test_video_decoder test_video_decoder.cpp
                   ${CMAKE_SOURCE_DIR}/src/engine/systems/video_decoder_gstreamer.cpp)
    target_include_directories(test_video_decoder PRIVATE ${ENGINE_SRC_DIR})
    target_compile_definitions(test_video_decoder PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_video_decoder PRIVATE doctest_with_main SDL3::SDL3
                          spdlog::spdlog_header_only PkgConfig::GSTREAMER)
    dbc_enable_warnings(test_video_decoder)
    doctest_discover_tests(test_video_decoder PROPERTIES LABELS "unit-video"
                          ENVIRONMENT "OPENDBC_HEADLESS=1")
    add_test(NAME video_decoder_without_plugins COMMAND test_video_decoder
             "--test-case=GStreamer reports missing playback plugins" --no-skip)
    set_tests_properties(video_decoder_without_plugins PROPERTIES LABELS "unit-video"
        ENVIRONMENT "OPENDBC_HEADLESS=1;GST_PLUGIN_SYSTEM_PATH_1_0=;GST_PLUGIN_PATH_1_0=;GST_REGISTRY_1_0=${CMAKE_CURRENT_BINARY_DIR}/video-empty-registry.bin")
endif()

add_executable(test_network test_network.cpp)
target_link_libraries(test_network PRIVATE doctest_with_main dbc_interpreter)
target_compile_definitions(test_network PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
doctest_discover_tests(test_network PROPERTIES LABELS "unit")

# Plugins for the CALL DLL tests, named like a library of any platform so LOAD DLL "name.dll" finds them.
add_library(test_dll_plugin MODULE plugins/test_dll_plugin.c)
target_include_directories(test_dll_plugin PRIVATE ${PROJECT_SOURCE_DIR}/sdk)
add_library(test_not_plugin MODULE plugins/test_not_plugin.c)
set_target_properties(test_dll_plugin test_not_plugin PROPERTIES PREFIX "" DEBUG_POSTFIX "" C_VISIBILITY_PRESET hidden)
if(APPLE)
    set_target_properties(test_dll_plugin test_not_plugin PROPERTIES SUFFIX ".dylib")
endif()

add_executable(test_dll test_dll.cpp)
target_link_libraries(test_dll PRIVATE doctest_with_main dbc_interpreter)
target_compile_definitions(test_dll PRIVATE TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:test_dll_plugin>")
add_dependencies(test_dll test_dll_plugin test_not_plugin)
doctest_discover_tests(test_dll PROPERTIES LABELS "unit")

add_executable(test_input_haptics test_input_haptics.cpp)
target_include_directories(test_input_haptics PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_input_haptics PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_input_haptics PROPERTIES LABELS "unit")

add_executable(test_command_line test_command_line.cpp)
target_link_libraries(test_command_line PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_command_line PROPERTIES LABELS "unit-cli")

add_executable(test_program_image test_program_image.cpp)
target_link_libraries(test_program_image PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_program_image PROPERTIES LABELS "unit-cli")

add_executable(test_project_resolver test_project_resolver.cpp)
target_link_libraries(test_project_resolver PRIVATE doctest_with_main dbc_interpreter)
doctest_discover_tests(test_project_resolver PROPERTIES LABELS "unit-cli;unit-language")

# --- Test: debug system ---
add_executable(test_debug_system test_debug_system.cpp)
target_include_directories(test_debug_system PRIVATE ${ENGINE_SRC_DIR})
target_link_libraries(test_debug_system PRIVATE doctest_with_main dbc_engine)
doctest_discover_tests(test_debug_system PROPERTIES LABELS "unit")

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
    doctest_discover_tests(test_shader_compiler PROPERTIES LABELS "unit-studio-shaders")

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

    add_executable(test_studio_snippets
        test_studio_snippets.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/builtin_snippets.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/snippet_library.cpp
    )
    add_dependencies(test_studio_snippets opendbc_studio_snippets)
    target_include_directories(test_studio_snippets PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src
                               ${CMAKE_BINARY_DIR}/tools/studio/generated)
    target_compile_definitions(test_studio_snippets PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_studio_snippets PRIVATE doctest_with_main dbc_interpreter)
    doctest_discover_tests(test_studio_snippets PROPERTIES LABELS "unit-studio")

    add_executable(test_artifact_export
        test_artifact_export.cpp
        test_export_readiness.cpp
        test_process_runner.cpp
        test_studio_execution.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/android_keystore.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/artifact_export_service.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/export_android.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/export_common.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/export_desktop.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/export_ios.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/export_job.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/export_readiness.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/execution_adapter.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/process_runner.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/toolchain_locator.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/toolchain/windows_resources.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/settings.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/paths.cpp
        ${CMAKE_SOURCE_DIR}/tools/studio/src/core/project_settings.cpp
    )
    target_include_directories(test_artifact_export PRIVATE ${CMAKE_SOURCE_DIR}/tools/studio/src)
    target_link_libraries(test_artifact_export PRIVATE
        doctest_with_main
        dbc_interpreter
        nlohmann_json::nlohmann_json
    )
    target_compile_definitions(test_artifact_export PRIVATE
        OPENDBC_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        OPENDBC_TEST_RUNNER="$<TARGET_FILE:opendbc-runner>")
    add_dependencies(test_artifact_export opendbc-runner)
    if(WIN32)
        target_sources(test_artifact_export PRIVATE test_windows_export.cpp)
        target_compile_definitions(test_artifact_export PRIVATE
            OPENDBC_TEST_STUDIO="$<TARGET_FILE:opendbc>"
            OPENDBC_TEST_VERSION="${OPENDBC_VERSION}")
        target_link_libraries(test_artifact_export PRIVATE version)
        add_dependencies(test_artifact_export opendbc)
    endif()
    doctest_discover_tests(test_artifact_export PROPERTIES LABELS "unit-studio-packaging")
endif()

# The tests are OpenDBC code too.
foreach(test_target IN ITEMS test_language_profile test_language_catalogue test_frontend test_vm test_studio_language
        test_studio_run_plan
        test_engine_math test_collision_math test_bsp_world test_terrain_mesh test_engine_builtins test_network test_dll test_input_haptics test_command_line
        test_project_resolver test_debug_system
        test_shader_compiler test_project_settings test_studio_new_project test_studio_snippets
        test_artifact_export)
    if(TARGET ${test_target})
        dbc_enable_warnings(${test_target})
    endif()
endforeach()

if(TARGET OpenXR::headers)
    add_executable(test_vr test_vr.cpp)
    target_link_libraries(test_vr PRIVATE doctest_with_main dbc_interpreter OpenXR::headers)
    target_compile_definitions(test_vr PRIVATE OPENDBC_OPENXR=1)
    dbc_enable_warnings(test_vr)
    doctest_discover_tests(test_vr PROPERTIES LABELS "unit-vr")
endif()
