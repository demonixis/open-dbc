add_library(dbc_engine STATIC
  systems/engine_kernel.cpp
  systems/vr_system.cpp
  systems/vr_tracking.cpp
  systems/vr_presenter.cpp
  systems/file_io_system.cpp
  systems/camera_system.cpp
  systems/audio_system.cpp
  systems/input_system.cpp
  systems/matrix_manager.cpp
  systems/terrain_mesh.cpp
  systems/bsp_world.cpp
  systems/light_manager.cpp
  systems/image_store.cpp
  systems/object_manager.cpp
  systems/physics_system.cpp
  systems/platform_io.cpp
  systems/text_system.cpp
  systems/memblock_system.cpp
  systems/draw_system.cpp
  systems/graphics_device_system.cpp
  systems/core_system.cpp
  systems/renderer_system.cpp
  systems/sprite_system.cpp
  systems/debug_system.cpp
)

target_link_libraries(dbc_engine
  PUBLIC
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    assimp::assimp
    glm::glm
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
  PRIVATE
    Jolt
)

add_dependencies(dbc_engine opendbc_shaders)
dbc_enable_warnings(dbc_engine)


if(APPLE)
    target_link_libraries(dbc_engine PUBLIC "-framework Foundation" "-framework AVFoundation")
endif()

if(TARGET OpenXR::headers)
    target_link_libraries(dbc_engine PRIVATE OpenXR::headers)
    target_compile_definitions(dbc_engine PRIVATE OPENDBC_OPENXR=1)
endif()
