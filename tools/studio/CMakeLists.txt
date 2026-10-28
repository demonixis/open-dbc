cmake_minimum_required(VERSION 3.24)
project(OpenDBCStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Dear ImGui (docking branch) -------------------------------------------
# No CMakeLists upstream: FetchContent only populates, the target is built here.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 9b4eb24cee2071e61dc1f9ef3e5228097cdde720
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)

target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --- ImGuiColorTextEdit (goossens fork) ------------------------------------
# Syntax-highlighting code editor widget: markers, squigglies, autocomplete,
# hover callbacks and a built-in find/replace window.
FetchContent_Declare(
    imgui_texteditor
    GIT_REPOSITORY https://github.com/goossens/ImGuiColorTextEdit.git
    GIT_TAG cfc3f68ab307d695c66fde5fc90f16a8f14ca6cb
)
FetchContent_MakeAvailable(imgui_texteditor)

add_library(imgui_texteditor STATIC
    ${imgui_texteditor_SOURCE_DIR}/TextEditor.cpp
    ${imgui_texteditor_SOURCE_DIR}/extras/TrieAutoComplete.cpp
)

target_include_directories(imgui_texteditor SYSTEM PUBLIC
    ${imgui_texteditor_SOURCE_DIR}
    ${imgui_texteditor_SOURCE_DIR}/extras
)

target_link_libraries(imgui_texteditor PUBLIC imgui)

# --- nlohmann/json ---------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
)
FetchContent_MakeAvailable(nlohmann_json)

set(OPENDBC_GENERATED_LICENSE_DIR "${CMAKE_BINARY_DIR}/licenses/third-party")
file(MAKE_DIRECTORY "${OPENDBC_GENERATED_LICENSE_DIR}")
set(OPENDBC_THIRD_PARTY_LICENSES
    "${sdl3_SOURCE_DIR}/LICENSE.txt|SDL3.txt"
    "${sdl3_image_SOURCE_DIR}/LICENSE.txt|SDL3_image.txt"
    "${sdl3_mixer_SOURCE_DIR}/LICENSE.txt|SDL3_mixer.txt"
    "${sdl3_mixer_SOURCE_DIR}/src/timidity/COPYING|Timidity.txt"
    "${sdl3_mixer_SOURCE_DIR}/src/dr_libs/LICENSE|dr_mp3.txt"
    "${sdl3_ttf_SOURCE_DIR}/LICENSE.txt|SDL3_ttf.txt"
    "${sdl3_ttf_SOURCE_DIR}/external/freetype/docs/FTL.TXT|FreeType.txt"
    "${assimp_SOURCE_DIR}/LICENSE|Assimp.txt"
    "${assimp_SOURCE_DIR}/contrib/rapidjson/license.txt|RapidJSON.txt"
    "${glm_SOURCE_DIR}/copying.txt|GLM.txt"
    "${spdlog_SOURCE_DIR}/LICENSE|spdlog.txt"
    "${imgui_SOURCE_DIR}/LICENSE.txt|Dear_ImGui.txt"
    "${imgui_texteditor_SOURCE_DIR}/LICENSE|ImGuiColorTextEdit.txt"
    "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT|nlohmann_json.txt"
    "${unordered_dense_SOURCE_DIR}/LICENSE|unordered_dense.txt"
    "${joltphysics_SOURCE_DIR}/LICENSE|JoltPhysics.txt")
if(TARGET OpenXR::headers)
    list(APPEND OPENDBC_THIRD_PARTY_LICENSES
        "${OPENDBC_OPENXR_SOURCE_DIR}/LICENSE|OpenXR-SDK.txt"
        "${OPENDBC_OPENXR_SOURCE_DIR}/src/external/jsoncpp/LICENSE|jsoncpp.txt")
endif()
if(TARGET SDL3_shadercross::SDL3_shadercross)
    list(APPEND OPENDBC_THIRD_PARTY_LICENSES
        "${sdl3_shadercross_SOURCE_DIR}/LICENSE.txt|SDL3_shadercross.txt"
        "${sdl3_shadercross_SOURCE_DIR}/external/SPIRV-Cross/LICENSE|SPIRV-Cross.txt"
        "${sdl3_shadercross_SOURCE_DIR}/external/SPIRV-Tools/LICENSE|SPIRV-Tools.txt"
        "${sdl3_shadercross_SOURCE_DIR}/external/SPIRV-Headers/LICENSE|SPIRV-Headers.txt")
    if(SDLSHADERCROSS_DXC)
        list(APPEND OPENDBC_THIRD_PARTY_LICENSES
            "${sdl3_shadercross_SOURCE_DIR}/external/DirectXShaderCompiler/LICENSE.TXT|DirectXShaderCompiler.txt")
    endif()
endif()
set(OPENDBC_GENERATED_LICENSES)
foreach(OPENDBC_LICENSE_ENTRY IN LISTS OPENDBC_THIRD_PARTY_LICENSES)
    string(REPLACE "|" ";" OPENDBC_LICENSE_PARTS "${OPENDBC_LICENSE_ENTRY}")
    list(GET OPENDBC_LICENSE_PARTS 0 OPENDBC_LICENSE_SOURCE)
    list(GET OPENDBC_LICENSE_PARTS 1 OPENDBC_LICENSE_NAME)
    if(NOT EXISTS "${OPENDBC_LICENSE_SOURCE}")
        message(FATAL_ERROR "Pinned dependency license is missing: ${OPENDBC_LICENSE_SOURCE}")
    endif()
    configure_file("${OPENDBC_LICENSE_SOURCE}"
        "${OPENDBC_GENERATED_LICENSE_DIR}/${OPENDBC_LICENSE_NAME}" COPYONLY)
    list(APPEND OPENDBC_GENERATED_LICENSES
        "${OPENDBC_GENERATED_LICENSE_DIR}/${OPENDBC_LICENSE_NAME}")
endforeach()

# --- OpenDBC ---------------------------------------------------------------
set(STUDIO_SOURCES
    src/main.cpp
    src/app.cpp
    src/app.hpp
    src/theme.cpp
    src/theme.hpp

    src/toolchain/execution_adapter.cpp
    src/toolchain/execution_adapter.hpp
    src/toolchain/active_run_planner.cpp
    src/toolchain/active_run_planner.hpp
    src/toolchain/artifact_export_service.cpp
    src/toolchain/artifact_export_service.hpp
    src/toolchain/icon_generator.cpp
    src/toolchain/icon_generator.hpp

    src/core/file_dialog.cpp
    src/core/file_dialog.hpp
    src/core/media_catalogue.cpp
    src/core/media_catalogue.hpp
    src/core/paths.cpp
    src/core/paths.hpp
    src/core/project_settings.cpp
    src/core/project_settings.hpp
    src/core/platform.hpp
    src/core/settings.cpp
    src/core/settings.hpp
    src/core/workspace.cpp
    src/core/workspace.hpp

    src/dba/code_outline.cpp
    src/dba/code_outline.hpp
    src/dba/command_database.cpp
    src/dba/command_database.hpp
    src/dba/language.cpp
    src/dba/language.hpp
    src/dba/program_info.cpp
    src/dba/program_info.hpp
    src/dba/reference.cpp
    src/dba/reference.hpp

    src/panels/asset_preview.cpp
    src/panels/asset_preview.hpp
    src/panels/code_panel.cpp
    src/panels/code_panel.hpp
    src/panels/editor_panel.cpp
    src/panels/editor_panel.hpp
    src/panels/launcher_window.cpp
    src/panels/launcher_window.hpp
    src/panels/media_panel.cpp
    src/panels/media_panel.hpp
    src/panels/output_panel.cpp
    src/panels/output_panel.hpp
    src/panels/project_panel.cpp
    src/panels/project_panel.hpp
    src/panels/project_settings_window.cpp
    src/panels/project_settings_window.hpp
    src/panels/scene_editor.cpp
    src/panels/scene_editor.hpp

    src/scene/scene_codegen.cpp
    src/scene/scene_codegen.hpp
    src/scene/scene_document.cpp
    src/scene/scene_document.hpp
    src/scene/scene_integration.cpp
    src/scene/scene_integration.hpp
)

if(NOT ANDROID AND NOT IOS)
    list(APPEND STUDIO_SOURCES
        src/toolchain/shader_compiler.cpp
        src/toolchain/shader_compiler.hpp)
endif()

if(APPLE AND NOT IOS)
    set(STUDIO_ICON "${CMAKE_CURRENT_LIST_DIR}/../../resources/editor.icns")
    set_source_files_properties("${STUDIO_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    list(APPEND STUDIO_SOURCES "${STUDIO_ICON}")
elseif(WIN32)
    list(APPEND STUDIO_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../../resources/editor.rc")
endif()

if(ANDROID)
    add_library(opendbc SHARED ${STUDIO_SOURCES})
    set_target_properties(opendbc PROPERTIES OUTPUT_NAME main)
    target_link_options(opendbc PRIVATE $<$<CONFIG:Release>:-Wl,--strip-all>)
else()
    add_executable(opendbc MACOSX_BUNDLE ${STUDIO_SOURCES})
endif()

if(COMMAND opendbc_deploy_resources)
    opendbc_deploy_resources(opendbc)
endif()

if(ANDROID)
    target_compile_definitions(opendbc PRIVATE DBC_STUDIO_SANDBOXED=1 DBC_STUDIO_ANDROID=1)
elseif(IOS)
    # One target covers iOS, iPadOS, visionOS (Designed for iPad) and Mac
    # Catalyst. Catalyst is a Mac but it is still the sandboxed product: no
    # export or child processes.
    target_compile_definitions(opendbc PRIVATE DBC_STUDIO_SANDBOXED=1 DBC_STUDIO_IOS=1)
endif()

target_include_directories(opendbc PRIVATE src)

target_link_libraries(opendbc PRIVATE
    dbc_interpreter
    imgui_texteditor
    nlohmann_json::nlohmann_json
)
if(NOT ANDROID AND NOT IOS)
    target_link_libraries(opendbc PRIVATE SDL3_shadercross::SDL3_shadercross spirv-cross-core)
endif()

target_compile_definitions(opendbc PRIVATE
    OPENDBC_VERSION_STRING="${OPENDBC_VERSION}"
)
dbc_enable_warnings(opendbc)

set(OPENDBC_REFERENCE_FILES
    "${CMAKE_SOURCE_DIR}/docs/reference/darkbasic_reference.md"
    "${CMAKE_SOURCE_DIR}/docs/reference/dbpro_reference.md"
    "${CMAKE_SOURCE_DIR}/docs/reference/open-dbc_reference.md")
set(OPENDBC_REPOSITORY_LICENSES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/licenses/Dark-Basic-Pro-LICENSE.md"
    "${CMAKE_SOURCE_DIR}/licenses/Gradle-wrapper-LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/licenses/MuseScore-General-SoundFont-LICENSE.md"
    "${CMAKE_SOURCE_DIR}/licenses/NotoSans-LICENSE.md"
    "${CMAKE_SOURCE_DIR}/licenses/stb_image-LICENSE.md")
if(APPLE)
    set_source_files_properties(${OPENDBC_REFERENCE_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/docs/NOTICE.md" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(${OPENDBC_REPOSITORY_LICENSES} ${OPENDBC_GENERATED_LICENSES}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/editor.png"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties("${OPENDBC_RUNTIME_MANIFEST}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(opendbc PRIVATE ${OPENDBC_REFERENCE_FILES} "${CMAKE_SOURCE_DIR}/docs/NOTICE.md"
        "${CMAKE_SOURCE_DIR}/resources/editor.png" "${OPENDBC_RUNTIME_MANIFEST}"
        ${OPENDBC_REPOSITORY_LICENSES} ${OPENDBC_GENERATED_LICENSES})
else()
    foreach(OPENDBC_REFERENCE_FILE IN LISTS OPENDBC_REFERENCE_FILES)
        add_custom_command(TARGET opendbc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENDBC_REFERENCE_FILE}" "$<TARGET_FILE_DIR:opendbc>"
            COMMENT "Copying OpenDBC reference documentation"
            VERBATIM)
    endforeach()
    add_custom_command(TARGET opendbc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:opendbc>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/NOTICE.md"
            "$<TARGET_FILE_DIR:opendbc>/NOTICE.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/resources/editor.png"
            "$<TARGET_FILE_DIR:opendbc>/editor.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENDBC_RUNTIME_MANIFEST}"
            "$<TARGET_FILE_DIR:opendbc>/opendbc-runtime.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENDBC_REPOSITORY_LICENSES}
            ${OPENDBC_GENERATED_LICENSES} "$<TARGET_FILE_DIR:opendbc>/licenses"
        COMMENT "Copying OpenDBC notices and dependency licenses" VERBATIM)
endif()

# Desktop exports start from opendbc-runner, which ships inside the Studio product.
if(NOT ANDROID AND NOT IOS)
    add_dependencies(opendbc opendbc-runner)
    if(APPLE)
        add_custom_command(TARGET opendbc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Helpers/opendbc-runner.app"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:opendbc-runner>"
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Helpers/opendbc-runner.app"
            COMMENT "Embedding opendbc-runner in the Studio bundle" VERBATIM)
    else()
        add_custom_command(TARGET opendbc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:opendbc-runner>"
                "$<TARGET_FILE_DIR:opendbc-runner>/opendbc-runner-host.json" "$<TARGET_FILE_DIR:opendbc>"
            COMMENT "Placing opendbc-runner beside the Studio" VERBATIM)
        # PROGRAMS installs the file as built, so its hash still matches the host manifest.
        install(PROGRAMS "$<TARGET_FILE:opendbc-runner>" DESTINATION bin COMPONENT Runtime)
        install(FILES "$<TARGET_FILE_DIR:opendbc-runner>/opendbc-runner-host.json" DESTINATION bin
            COMPONENT Runtime)
    endif()
endif()

if(IOS)
    # --- Sandboxed product: iOS, iPadOS, visionOS, Mac Catalyst -------------
    configure_file(ios/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)

    set(STUDIO_IOS_SHADERS ${OPENDBC_CANONICAL_SHADERS})
    set_source_files_properties(${STUDIO_IOS_SHADERS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/shaders")

    target_sources(opendbc PRIVATE ${STUDIO_IOS_SHADERS})

    set_target_properties(opendbc PROPERTIES
        BUNDLE TRUE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OPENDBC_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
        XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "YES"
        XCODE_ATTRIBUTE_SUPPORTS_XR_DESIGNED_FOR_IPHONE_IPAD "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OPENDBC_IOS_TEAM_ID}"
        XCODE_GENERATE_SCHEME ON
    )
elseif(APPLE)
    # --- Full product: signed outside the App Store, drives the toolchains --
    set_target_properties(opendbc PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "OpenDBC"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${OPENDBC_DESKTOP_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${OPENDBC_BUILD_NUMBER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${OPENDBC_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "editor.icns"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OPENDBC_DESKTOP_BUNDLE_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OPENDBC_IOS_TEAM_ID}"
        XCODE_GENERATE_SCHEME ON
    )
    # Touch the .app bundle to flush the macOS icon cache after each build
    add_custom_command(TARGET opendbc POST_BUILD
        COMMAND touch "$<TARGET_BUNDLE_DIR:opendbc>"
        VERBATIM
    )
endif()

set(OPENDBC_IOS_EXPORT_HOST "" CACHE PATH "Qualified iOS OpenDBC.app export host")
set(OPENDBC_IOS_SIMULATOR_EXPORT_HOST "" CACHE PATH "Qualified iOS Simulator OpenDBC.app host")
set(OPENDBC_ANDROID_EXPORT_HOST_LIBRARY "" CACHE FILEPATH "Qualified Android arm64-v8a libmain.so export host")

function(opendbc_require_matching_host HOST_MANIFEST HOST_BINARY DESCRIPTION)
    if(NOT EXISTS "${HOST_MANIFEST}" OR IS_SYMLINK "${HOST_MANIFEST}")
        message(FATAL_ERROR "${DESCRIPTION} has no regular host manifest")
    endif()
    file(SHA256 "${OPENDBC_RUNTIME_MANIFEST}" OPENDBC_LOCAL_RUNTIME_MANIFEST_HASH)
    file(SHA256 "${HOST_BINARY}" OPENDBC_ACTUAL_HOST_BINARY_HASH)
    file(READ "${HOST_MANIFEST}" OPENDBC_HOST_MANIFEST_JSON)
    string(JSON OPENDBC_HOST_MANIFEST_FORMAT ERROR_VARIABLE OPENDBC_HOST_MANIFEST_ERROR
        GET "${OPENDBC_HOST_MANIFEST_JSON}" format)
    string(JSON OPENDBC_HOST_RUNTIME_MANIFEST_HASH ERROR_VARIABLE OPENDBC_HOST_RUNTIME_ERROR
        GET "${OPENDBC_HOST_MANIFEST_JSON}" runtime_manifest_sha256)
    string(JSON OPENDBC_HOST_BINARY_HASH ERROR_VARIABLE OPENDBC_HOST_BINARY_ERROR
        GET "${OPENDBC_HOST_MANIFEST_JSON}" binary_sha256)
    if(OPENDBC_HOST_MANIFEST_ERROR OR OPENDBC_HOST_RUNTIME_ERROR OR OPENDBC_HOST_BINARY_ERROR OR
       NOT OPENDBC_HOST_MANIFEST_FORMAT EQUAL 1 OR
       NOT OPENDBC_LOCAL_RUNTIME_MANIFEST_HASH STREQUAL OPENDBC_HOST_RUNTIME_MANIFEST_HASH OR
       NOT OPENDBC_ACTUAL_HOST_BINARY_HASH STREQUAL OPENDBC_HOST_BINARY_HASH)
        message(FATAL_ERROR "${DESCRIPTION} was not built from the current OpenDBC runtime sources")
    endif()
endfunction()

function(opendbc_require_android_system_dependencies HOST_MANIFEST DESCRIPTION)
    file(READ "${HOST_MANIFEST}" OPENDBC_ANDROID_HOST_MANIFEST_JSON)
    string(JSON OPENDBC_ANDROID_NEEDED_COUNT ERROR_VARIABLE OPENDBC_ANDROID_NEEDED_ERROR
        LENGTH "${OPENDBC_ANDROID_HOST_MANIFEST_JSON}" needed)
    if(OPENDBC_ANDROID_NEEDED_ERROR)
        message(FATAL_ERROR "${DESCRIPTION} has no Android dependency inventory")
    endif()
    if(OPENDBC_ANDROID_NEEDED_COUNT GREATER 0)
        math(EXPR OPENDBC_ANDROID_NEEDED_LAST "${OPENDBC_ANDROID_NEEDED_COUNT} - 1")
        foreach(OPENDBC_ANDROID_NEEDED_INDEX RANGE ${OPENDBC_ANDROID_NEEDED_LAST})
            string(JSON OPENDBC_ANDROID_NEEDED GET "${OPENDBC_ANDROID_HOST_MANIFEST_JSON}"
                needed ${OPENDBC_ANDROID_NEEDED_INDEX})
            if(NOT OPENDBC_ANDROID_NEEDED MATCHES
               "^(libandroid|libc|libdl|liblog|libm|libOpenSLES|libz)\\.so$")
                message(FATAL_ERROR
                    "${DESCRIPTION} has forbidden dependency ${OPENDBC_ANDROID_NEEDED}")
            endif()
        endforeach()
    endif()
endfunction()

function(opendbc_require_canonical_host_shaders HOST_ROOT DESCRIPTION)
    foreach(OPENDBC_SOURCE_SHADER IN LISTS OPENDBC_CANONICAL_SHADERS)
        get_filename_component(OPENDBC_SHADER_NAME "${OPENDBC_SOURCE_SHADER}" NAME)
        set(OPENDBC_HOST_SHADER "${HOST_ROOT}/shaders/${OPENDBC_SHADER_NAME}")
        if(NOT EXISTS "${OPENDBC_HOST_SHADER}" OR IS_SYMLINK "${OPENDBC_HOST_SHADER}")
            message(FATAL_ERROR "${DESCRIPTION} has no regular ${OPENDBC_SHADER_NAME}")
        endif()
        file(SHA256 "${OPENDBC_HOST_SHADER}" OPENDBC_HOST_SHADER_HASH)
        file(SHA256 "${OPENDBC_SOURCE_SHADER}" OPENDBC_SOURCE_SHADER_HASH)
        if(NOT OPENDBC_HOST_SHADER_HASH STREQUAL OPENDBC_SOURCE_SHADER_HASH)
            message(FATAL_ERROR "${DESCRIPTION} does not contain the canonical OpenDBC shaders")
        endif()
    endforeach()
endfunction()

if(NOT IOS AND NOT ANDROID)
    if(APPLE AND OPENDBC_IOS_EXPORT_HOST)
        set(OPENDBC_IOS_EXPORT_HOST_MANIFEST
            "${OPENDBC_IOS_EXPORT_HOST}.opendbc-host.json")
        if(NOT IS_DIRECTORY "${OPENDBC_IOS_EXPORT_HOST}")
            message(FATAL_ERROR "OPENDBC_IOS_EXPORT_HOST must name an iOS .app directory")
        endif()
        if(NOT EXISTS "${OPENDBC_IOS_EXPORT_HOST}/opendbc-runner")
            message(FATAL_ERROR "OPENDBC_IOS_EXPORT_HOST has no OpenDBC executable")
        endif()
        opendbc_require_matching_host("${OPENDBC_IOS_EXPORT_HOST_MANIFEST}"
            "${OPENDBC_IOS_EXPORT_HOST}/opendbc-runner" "OPENDBC_IOS_EXPORT_HOST")
        opendbc_require_canonical_host_shaders("${OPENDBC_IOS_EXPORT_HOST}" "OPENDBC_IOS_EXPORT_HOST")
        execute_process(COMMAND /usr/bin/xcrun vtool -show-build
            "${OPENDBC_IOS_EXPORT_HOST}/opendbc-runner"
            RESULT_VARIABLE OPENDBC_IOS_HOST_STATUS OUTPUT_VARIABLE OPENDBC_IOS_HOST_BUILD)
        if(NOT OPENDBC_IOS_HOST_STATUS EQUAL 0 OR
           NOT OPENDBC_IOS_HOST_BUILD MATCHES "platform IOS" OR
           OPENDBC_IOS_HOST_BUILD MATCHES "platform IOSSIMULATOR")
            message(FATAL_ERROR "OPENDBC_IOS_EXPORT_HOST must be an iphoneos device host")
        endif()
        add_custom_command(TARGET opendbc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/ios"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${OPENDBC_IOS_EXPORT_HOST}"
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/ios/OpenDBC.app"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENDBC_IOS_EXPORT_HOST_MANIFEST}"
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/ios/opendbc-host.json"
            COMMENT "Embedding the qualified iOS export host" VERBATIM)
    endif()
    if(APPLE AND OPENDBC_IOS_SIMULATOR_EXPORT_HOST)
        set(OPENDBC_IOS_SIMULATOR_EXPORT_HOST_MANIFEST
            "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}.opendbc-host.json")
        if(NOT IS_DIRECTORY "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}" OR
           NOT EXISTS "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}/opendbc-runner")
            message(FATAL_ERROR "OPENDBC_IOS_SIMULATOR_EXPORT_HOST must name an iOS Simulator .app")
        endif()
        opendbc_require_matching_host("${OPENDBC_IOS_SIMULATOR_EXPORT_HOST_MANIFEST}"
            "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}/opendbc-runner" "OPENDBC_IOS_SIMULATOR_EXPORT_HOST")
        opendbc_require_canonical_host_shaders(
            "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}" "OPENDBC_IOS_SIMULATOR_EXPORT_HOST")
        execute_process(COMMAND /usr/bin/xcrun vtool -show-build
            "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}/opendbc-runner"
            RESULT_VARIABLE OPENDBC_IOS_SIM_HOST_STATUS OUTPUT_VARIABLE OPENDBC_IOS_SIM_HOST_BUILD)
        if(NOT OPENDBC_IOS_SIM_HOST_STATUS EQUAL 0 OR
           NOT OPENDBC_IOS_SIM_HOST_BUILD MATCHES "platform IOSSIMULATOR")
            message(FATAL_ERROR "OPENDBC_IOS_SIMULATOR_EXPORT_HOST must be an iphonesimulator host")
        endif()
        add_custom_command(TARGET opendbc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/ios-simulator"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST}"
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/ios-simulator/OpenDBC.app"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENDBC_IOS_SIMULATOR_EXPORT_HOST_MANIFEST}"
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/ios-simulator/opendbc-host.json"
            COMMENT "Embedding the qualified iOS Simulator host" VERBATIM)
    endif()
    if(OPENDBC_ANDROID_EXPORT_HOST_LIBRARY)
        if(NOT EXISTS "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}" OR
           IS_DIRECTORY "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}" OR
           IS_SYMLINK "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}")
            message(FATAL_ERROR
                "OPENDBC_ANDROID_EXPORT_HOST_LIBRARY must be a regular non-symlink file")
        endif()
        file(READ "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}" OPENDBC_ANDROID_HOST_HEADER
            OFFSET 0 LIMIT 20 HEX)
        string(TOLOWER "${OPENDBC_ANDROID_HOST_HEADER}" OPENDBC_ANDROID_HOST_HEADER)
        string(LENGTH "${OPENDBC_ANDROID_HOST_HEADER}" OPENDBC_ANDROID_HOST_HEADER_LENGTH)
        if(OPENDBC_ANDROID_HOST_HEADER_LENGTH LESS 40)
            message(FATAL_ERROR "OPENDBC_ANDROID_EXPORT_HOST_LIBRARY has a truncated ELF header")
        endif()
        string(SUBSTRING "${OPENDBC_ANDROID_HOST_HEADER}" 0 14 OPENDBC_ANDROID_HOST_IDENT)
        string(SUBSTRING "${OPENDBC_ANDROID_HOST_HEADER}" 36 4 OPENDBC_ANDROID_HOST_MACHINE)
        if(NOT OPENDBC_ANDROID_HOST_IDENT STREQUAL "7f454c46020101" OR
           NOT OPENDBC_ANDROID_HOST_MACHINE STREQUAL "b700")
            message(FATAL_ERROR
                "OPENDBC_ANDROID_EXPORT_HOST_LIBRARY must be an ELF64 little-endian AArch64 library")
        endif()
        get_filename_component(OPENDBC_ANDROID_EXPORT_HOST_DIRECTORY
            "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}" DIRECTORY)
        opendbc_require_matching_host("${OPENDBC_ANDROID_EXPORT_HOST_DIRECTORY}/opendbc-host.json"
            "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}" "OPENDBC_ANDROID_EXPORT_HOST_LIBRARY")
        opendbc_require_android_system_dependencies(
            "${OPENDBC_ANDROID_EXPORT_HOST_DIRECTORY}/opendbc-host.json"
            "OPENDBC_ANDROID_EXPORT_HOST_LIBRARY")
        if(APPLE)
            set(OPENDBC_ANDROID_TEMPLATE_OUTPUT
                "$<TARGET_BUNDLE_DIR:opendbc>/Contents/Resources/export-hosts/android/template")
        else()
            set(OPENDBC_ANDROID_TEMPLATE_OUTPUT "$<TARGET_FILE_DIR:opendbc>/export-hosts/android/template")
        endif()
        add_custom_command(TARGET opendbc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/examples/android/export-template" "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/.gradle"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/examples/android/gradlew"
                "${CMAKE_SOURCE_DIR}/examples/android/gradlew.bat" "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/gradle/wrapper"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/examples/android/gradle/wrapper"
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/gradle/wrapper"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${sdl3_SOURCE_DIR}/android-project/app/src/main/java"
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/java"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/jniLibs/arm64-v8a"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}"
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/jniLibs/arm64-v8a/libmain.so"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OPENDBC_CANONICAL_SHADERS}
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/shaders"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/licenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/NOTICE.md"
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/NOTICE.md"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENDBC_RUNTIME_MANIFEST}"
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/opendbc-runtime.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENDBC_ANDROID_EXPORT_HOST_DIRECTORY}/opendbc-host.json"
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/opendbc-host.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENDBC_REPOSITORY_LICENSES}
                ${OPENDBC_GENERATED_LICENSES}
                "${OPENDBC_ANDROID_TEMPLATE_OUTPUT}/app/src/main/assets/licenses"
            COMMENT "Embedding the qualified Android export host" VERBATIM)
    endif()
endif()

if(APPLE AND NOT IOS)
    add_custom_command(TARGET opendbc POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - --timestamp=none
            "$<TARGET_BUNDLE_DIR:opendbc>"
        COMMENT "Ad-hoc signing the complete OpenDBC bundle"
        VERBATIM)
endif()

if(NOT IOS AND NOT ANDROID)
    install(TARGETS opendbc
        BUNDLE DESTINATION . COMPONENT Runtime
        RUNTIME DESTINATION bin COMPONENT Runtime)
    if(NOT APPLE)
        if(TARGET openxr_loader)
            if(WIN32)
                install(FILES "$<TARGET_FILE:openxr_loader>" DESTINATION bin COMPONENT Runtime)
            else()
                install(FILES "$<TARGET_FILE:openxr_loader>" DESTINATION bin
                    RENAME "$<TARGET_SONAME_FILE_NAME:openxr_loader>" COMPONENT Runtime)
            endif()
        endif()
        install(FILES ${OPENDBC_REFERENCE_FILES} DESTINATION bin COMPONENT Runtime)
        install(FILES ${OPENDBC_CANONICAL_SHADERS} DESTINATION bin/shaders COMPONENT Runtime)
        install(FILES "${CMAKE_SOURCE_DIR}/docs/NOTICE.md" DESTINATION bin COMPONENT Runtime)
        install(FILES "${CMAKE_SOURCE_DIR}/resources/editor.png" DESTINATION bin COMPONENT Runtime)
        install(FILES "${OPENDBC_RUNTIME_MANIFEST}" DESTINATION bin COMPONENT Runtime)
        install(FILES ${OPENDBC_REPOSITORY_LICENSES} ${OPENDBC_GENERATED_LICENSES}
            DESTINATION bin/licenses COMPONENT Runtime)
        if(OPENDBC_ANDROID_EXPORT_HOST_LIBRARY)
            install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples/android/export-template/"
                DESTINATION bin/export-hosts/android/template COMPONENT Runtime
                PATTERN ".gradle" EXCLUDE)
            install(PROGRAMS "${CMAKE_SOURCE_DIR}/examples/android/gradlew"
                DESTINATION bin/export-hosts/android/template COMPONENT Runtime)
            install(FILES "${CMAKE_SOURCE_DIR}/examples/android/gradlew.bat"
                DESTINATION bin/export-hosts/android/template COMPONENT Runtime)
            install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples/android/gradle/wrapper"
                DESTINATION bin/export-hosts/android/template/gradle COMPONENT Runtime)
            install(DIRECTORY "${sdl3_SOURCE_DIR}/android-project/app/src/main/java/"
                DESTINATION bin/export-hosts/android/template/app/src/main/java COMPONENT Runtime)
            install(FILES "${OPENDBC_ANDROID_EXPORT_HOST_LIBRARY}"
                DESTINATION bin/export-hosts/android/template/app/src/main/jniLibs/arm64-v8a
                RENAME libmain.so COMPONENT Runtime)
            install(FILES ${OPENDBC_CANONICAL_SHADERS}
                DESTINATION bin/export-hosts/android/template/app/src/main/assets/shaders
                COMPONENT Runtime)
            install(FILES "${CMAKE_SOURCE_DIR}/docs/NOTICE.md"
                DESTINATION bin/export-hosts/android/template/app/src/main/assets COMPONENT Runtime)
            install(FILES "${OPENDBC_RUNTIME_MANIFEST}"
                DESTINATION bin/export-hosts/android/template/app/src/main/assets COMPONENT Runtime)
            install(FILES "${OPENDBC_ANDROID_EXPORT_HOST_DIRECTORY}/opendbc-host.json"
                DESTINATION bin/export-hosts/android/template/app/src/main/assets COMPONENT Runtime)
            install(FILES ${OPENDBC_REPOSITORY_LICENSES} ${OPENDBC_GENERATED_LICENSES}
                DESTINATION bin/export-hosts/android/template/app/src/main/assets/licenses COMPONENT Runtime)
        endif()
    endif()
endif()
