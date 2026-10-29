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
  systems/triangle_order.cpp
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
  systems/video_system.cpp
  systems/network_system.cpp
  systems/dll_system.cpp
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
    enet
)

target_include_directories(dbc_engine PRIVATE ${PROJECT_SOURCE_DIR}/sdk)
target_include_directories(dbc_engine PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_dependencies(dbc_engine opendbc_shaders)
dbc_enable_warnings(dbc_engine)


# Animations decode with the platform's media framework; other platforms load them inert.
if(APPLE)
    target_sources(dbc_engine PRIVATE systems/video_decoder_avfoundation.mm)
    set_source_files_properties(systems/video_decoder_avfoundation.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(dbc_engine PUBLIC "-framework Foundation" "-framework AVFoundation"
                          "-framework CoreMedia" "-framework CoreVideo")
elseif(WIN32)
    target_sources(dbc_engine PRIVATE systems/video_decoder_media_foundation.cpp)
    target_link_libraries(dbc_engine PRIVATE mfplat mfuuid ole32 oleaut32 windowscodecs)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET GLOBAL
        gstreamer-1.0>=1.16 gstreamer-app-1.0>=1.16 gstreamer-video-1.0>=1.16)
    target_sources(dbc_engine PRIVATE systems/video_decoder_gstreamer.cpp)
    target_link_libraries(dbc_engine PRIVATE PkgConfig::GSTREAMER)
else()
    target_sources(dbc_engine PRIVATE systems/video_decoder_unavailable.cpp)
endif()

if(TARGET OpenXR::headers)
    target_link_libraries(dbc_engine PRIVATE OpenXR::headers)
    target_compile_definitions(dbc_engine PRIVATE OPENDBC_OPENXR=1)
endif()
