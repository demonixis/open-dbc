add_library(dbc_engine STATIC
  systems/engine_kernel.cpp
  systems/system_info.cpp
  systems/data_file_system.cpp
  systems/file_map_system.cpp
  systems/vr_system.cpp
  systems/vr_tracking.cpp
  systems/vr_presenter.cpp
  systems/file_io_system.cpp
  systems/camera_system.cpp
  systems/audio_system.cpp
  systems/midi_renderer.cpp
  systems/input_system.cpp
  systems/matrix_manager.cpp
  systems/terrain_mesh.cpp
  systems/shadow_mesh.cpp
  systems/triangle_order.cpp
  systems/bsp_world.cpp
  systems/light_manager.cpp
  fallback_font.cpp
  systems/image_store.cpp
  systems/object_manager.cpp
  systems/physics_system.cpp
  systems/platform_io.cpp
  systems/text_system.cpp
  systems/memblock_system.cpp
  systems/draw_system.cpp
  systems/graphics_device_system.cpp
  systems/core_system.cpp
  systems/renderer_animation.cpp
  systems/renderer_batches.cpp
  systems/renderer_gpu_meshes.cpp
  systems/renderer_hud.cpp
  systems/renderer_model_import.cpp
  systems/renderer_objects.cpp
  systems/renderer_primitives.cpp
  systems/renderer_shadows.cpp
  systems/renderer_system.cpp
  systems/renderer_terrain.cpp
  systems/renderer_transforms.cpp
  systems/renderer_view.cpp
  systems/renderer_world.cpp
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

# The General MIDI bank LOAD MUSIC renders with, embedded so the Studio and every exported runner have it.
set(DBC_MIDI_BANK_DIR "${PROJECT_SOURCE_DIR}/resources/midi")
set(DBC_MIDI_BANK_FILE VintageDreamsWaves-v2.sf2)
set(DBC_MIDI_BANK_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/generated/midi_bank.inc")
add_custom_command(
  OUTPUT "${DBC_MIDI_BANK_INCLUDE}"
  COMMAND ${CMAKE_COMMAND} -DOUTPUT=${DBC_MIDI_BANK_INCLUDE} -DSOURCE_DIR=${DBC_MIDI_BANK_DIR}
          -DFILES=${DBC_MIDI_BANK_FILE} -DARRAY=KMidiBankFiles
          -P "${PROJECT_SOURCE_DIR}/cmake/embed_files.cmake"
  DEPENDS "${DBC_MIDI_BANK_DIR}/${DBC_MIDI_BANK_FILE}" "${PROJECT_SOURCE_DIR}/cmake/embed_files.cmake"
  COMMENT "Embed the MIDI bank"
  VERBATIM)
add_custom_target(dbc_midi_bank DEPENDS "${DBC_MIDI_BANK_INCLUDE}")
add_dependencies(dbc_engine dbc_midi_bank)
target_include_directories(dbc_engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(dbc_engine SYSTEM PRIVATE "${tinysoundfont_SOURCE_DIR}")
if(NOT MSVC)
  # Unoptimized, a four-minute tune takes ten seconds to render, which stalls LOAD MUSIC in Debug builds.
  set_source_files_properties(systems/midi_renderer.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()

add_dependencies(dbc_engine opendbc_shaders)
dbc_enable_warnings(dbc_engine)


# Animations decode with the platform's media framework; other platforms load them inert.
if(APPLE)
    target_sources(dbc_engine PRIVATE systems/video_decoder_avfoundation.mm systems/system_info_apple.mm)
    set_source_files_properties(systems/video_decoder_avfoundation.mm systems/system_info_apple.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(dbc_engine PUBLIC "-framework Foundation" "-framework AVFoundation"
                          "-framework CoreMedia" "-framework CoreVideo")
    if(NOT IOS)
        # GET WEB CLIENT$ asks the workspace for the default browser.
        target_link_libraries(dbc_engine PUBLIC "-framework AppKit")
    endif()
elseif(WIN32)
    target_sources(dbc_engine PRIVATE systems/video_decoder_media_foundation.cpp)
    target_link_libraries(dbc_engine PRIVATE mfplat mfuuid ole32 oleaut32 windowscodecs ws2_32 shlwapi advapi32)
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
