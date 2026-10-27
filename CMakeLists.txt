cmake_minimum_required(VERSION 3.24)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
   AND (NOT DEFINED CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
   AND "$ENV{SDKROOT}" STREQUAL ""
   AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE OPENDBC_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE OPENDBC_XCRUN_RESULT
    )
    if(OPENDBC_XCRUN_RESULT EQUAL 0 AND IS_DIRECTORY "${OPENDBC_MACOS_SDK}")
        set(CMAKE_OSX_SYSROOT "${OPENDBC_MACOS_SDK}" CACHE PATH "macOS SDK to use." FORCE)
    endif()
endif()

project(OpenDBC VERSION 0.3 LANGUAGES C CXX)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL
    "Suppress deprecation warnings from pinned third-party CMake projects" FORCE)

# --- Versioning ------------------------------------------------------------
# Marketing version lives here and nowhere else. The build number comes from
# the commit count: monotonic, automatic, and never regresses, which is what
# TestFlight requires of every upload. It is refreshed when CMake re-runs.
set(OPENDBC_VERSION "${PROJECT_VERSION}" CACHE STRING "Marketing version (CFBundleShortVersionString)")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE OPENDBC_BUILD_NUMBER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _opendbc_git_result
    )
endif()
if(NOT OPENDBC_BUILD_NUMBER MATCHES "^[0-9]+$")
    set(OPENDBC_BUILD_NUMBER "1")
endif()
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE OPENDBC_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT OPENDBC_REVISION MATCHES "^[0-9a-f]+$")
    set(OPENDBC_REVISION "source-archive")
endif()
file(GLOB_RECURSE OPENDBC_RUNTIME_FINGERPRINT_INPUTS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "src/*.cpp" "src/*.hpp" "src/*.inc"
    "tools/studio/src/*.cpp" "tools/studio/src/*.hpp"
    "shaders/*.glsl" "shaders/*.vert" "shaders/*.frag" "shaders/*.spv"
    "cmake/*.cmake" "cmake/*.patch")
list(APPEND OPENDBC_RUNTIME_FINGERPRINT_INPUTS
    "CMakeLists.txt"
    "src/engine/CMakeLists.txt"
    "src/interpreter/CMakeLists.txt"
    "src/language/CMakeLists.txt"
    "src/vm/CMakeLists.txt"
    "tools/studio/CMakeLists.txt")
list(SORT OPENDBC_RUNTIME_FINGERPRINT_INPUTS)
set(OPENDBC_RUNTIME_FINGERPRINT_MATERIAL "OpenDBC-runtime-v1\n")
foreach(OPENDBC_RUNTIME_INPUT IN LISTS OPENDBC_RUNTIME_FINGERPRINT_INPUTS)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${OPENDBC_RUNTIME_INPUT}" OPENDBC_RUNTIME_INPUT_HASH)
    string(APPEND OPENDBC_RUNTIME_FINGERPRINT_MATERIAL
        "${OPENDBC_RUNTIME_INPUT}:${OPENDBC_RUNTIME_INPUT_HASH}\n")
endforeach()
string(SHA256 OPENDBC_RUNTIME_FINGERPRINT "${OPENDBC_RUNTIME_FINGERPRINT_MATERIAL}")
message(STATUS "open-dbc version ${OPENDBC_VERSION} build ${OPENDBC_BUILD_NUMBER}")

# --- Bundle identity -------------------------------------------------------
# The App Store product (iOS / iPadOS / visionOS / Mac Catalyst).
set(OPENDBC_BUNDLE_ID "net.demonixis.opendbc" CACHE STRING "Bundle identifier of the sandboxed app")
# The full desktop build is a different product with different capabilities, so
# it gets its own id and can sit next to the Catalyst one on the same Mac.
set(OPENDBC_DESKTOP_BUNDLE_ID "net.demonixis.opendbc.desktop" CACHE STRING "Bundle identifier of the full desktop app")
# Left empty on purpose: set it per machine, e.g. in CMakeUserPresets.json.
set(OPENDBC_IOS_TEAM_ID "" CACHE STRING "Apple Developer team ID used to sign")

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No C++ modules are used, and the pinned Android NDK has no clang-scan-deps.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release artifacts must not expose checkout or build paths through diagnostics
# compiled into OpenDBC or its statically linked dependencies.
if(MSVC)
    add_compile_options(
        "/pathmap:${CMAKE_SOURCE_DIR}=."
        "/pathmap:${CMAKE_BINARY_DIR}=."
    )
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=."
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # DXC's static libraries reference each other in a cycle that GNU ld cannot resolve in
        # one pass; lld, like the Apple and Microsoft linkers, does not depend on archive order.
        include(CheckLinkerFlag)
        check_linker_flag(CXX "-fuse-ld=lld" OPENDBC_HAS_LLD)
        if(OPENDBC_HAS_LLD)
            add_link_options(-fuse-ld=lld)
        else()
            message(WARNING "lld was not found; linking DXC with GNU ld may fail")
        endif()
    endif()
    if(APPLE)
        # The Apple linker records each object file's absolute path in the debug map.
        add_link_options("LINKER:-oso_prefix,${CMAKE_BINARY_DIR}/")
    endif()
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# --- Mobile cross-compilation overrides ---
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(IOS TRUE)
endif()

if(ANDROID OR IOS)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)
    add_compile_definitions(DBC_MOBILE=1)
endif()

# The Xcode generator makes a scheme per target, and FetchContent brings in
# dozens. Only our own targets opt back in.
set(CMAKE_XCODE_GENERATE_SCHEME OFF)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
if(ANDROID OR IOS)
    set(SDL_SENSOR ON CACHE BOOL "" FORCE)
else()
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
endif()
set(SDL_POWER OFF CACHE BOOL "" FORCE)
# OpenDBC uses SDL's native file dialogs.
if(ANDROID OR IOS)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
else()
    set(SDL_DIALOG ON CACHE BOOL "" FORCE)
endif()
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST       OFF CACHE BOOL "" FORCE)
set(SDL_DISKAUDIO    OFF CACHE BOOL "" FORCE)
# The dummy audio driver is only used on demand by headless runs (OPENDBC_HEADLESS).
set(SDL_DUMMYAUDIO   ON  CACHE BOOL "" FORCE)
set(SDL_DUMMYVIDEO   OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN    OFF CACHE BOOL "" FORCE)
set(SDL_VIRTUAL_JOYSTICK ON CACHE BOOL "" FORCE)
if(NOT ANDROID AND NOT IOS)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)
    set(SDL_GPU_OPENXR ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG c1ef7793c75c2bde6321ec0c9894ea0b2a7694fe
  PATCH_COMMAND ${CMAKE_COMMAND}
                -DOPENDBC_GIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DOPENDBC_PATCH_SOURCE_DIR=<SOURCE_DIR>
                -P ${CMAKE_SOURCE_DIR}/cmake/apply_sdl_patches.cmake
)
FetchContent_MakeAvailable(SDL3)

# Keep SDK options local: its BUILD_TESTS must not disable OpenDBC's tests.
function(opendbc_configure_openxr)
    set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
    set(OPENXR_DEBUG_POSTFIX "" CACHE STRING "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF)
    set(BUILD_LOADER ON)
    set(BUILD_CONFORMANCE_TESTS OFF)
    FetchContent_Declare(opendbc_openxr
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG 105a2a674cd7aae85de8d6a43f2d2cbdaa80af60)
    FetchContent_MakeAvailable(opendbc_openxr)
    set(OPENDBC_OPENXR_SOURCE_DIR "${opendbc_openxr_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
if(NOT ANDROID AND NOT IOS)
    opendbc_configure_openxr()
endif()

if(WIN32)
    # SDL3_image (and other SDL3 extensions) use find_package(SDL3) internally.
    # When SDL3 comes from FetchContent its SDL3Config.cmake lives in the build
    # directory, which is not on the default search path.  Pointing SDL3_DIR at
    # that directory makes find_package succeed on every platform.
    set(SDL3_DIR "${sdl3_BINARY_DIR}" CACHE PATH "" FORCE)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_IMAGEIO OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_ANI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_APNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PCX OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_QOI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SVG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TGA ON CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XCF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XV OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG_SAVE OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP_SAVE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3_image
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
  GIT_TAG ad58ecfc27a1bd09e510ceff8bbbdb1094806476
)
FetchContent_MakeAvailable(SDL3_image)

set(SDLMIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDLMIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI ON CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI_TIMIDITY ON CACHE BOOL "" FORCE)
set(SDLMIXER_AIFF OFF CACHE BOOL "" FORCE)
set(SDLMIXER_AU OFF CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_DRMP3 ON CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VOC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_TREMOR OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3_mixer
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
  GIT_TAG da02588817a565fccfa60fa2f0bb661a3c77b8f0
)
FetchContent_MakeAvailable(SDL3_mixer)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_ZLIB ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
)
FetchContent_MakeAvailable(SDL3_ttf)
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND TARGET freetype)
    target_compile_options(freetype PRIVATE -Wno-macro-redefined)
endif()

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_X_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_3DS_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD2_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_MD3_IMPORTER ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG c35200e38ea8f058812b83de2ef32c6093b0ece2
)
FetchContent_MakeAvailable(assimp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET assimp)
    target_compile_options(assimp PRIVATE -Wno-nontrivial-memcall)
endif()

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
set(GLM_ENABLE_CXX_17 ON CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG 6fa36017cfd5731d617e1a934f0e5ea9c4445b13
)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
  unordered_dense
  GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
  GIT_TAG 73f3cbb237e84d483afafc743f1f14ec53e12314
)
FetchContent_MakeAvailable(unordered_dense)

set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a
  SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(JoltPhysics)

if(APPLE OR (NOT ANDROID AND NOT IOS))
    if(ANDROID OR IOS)
        set(SDLSHADERCROSS_DXC OFF CACHE BOOL "" FORCE)
    else()
        set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
    endif()
    set(OPENDBC_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    if(APPLE AND SDLSHADERCROSS_DXC)
        # Recent Apple Clang flags DXC's specializations; older releases reject the option.
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-Wno-invalid-specialization OPENDBC_HAS_WNO_INVALID_SPECIALIZATION)
        if(OPENDBC_HAS_WNO_INVALID_SPECIALIZATION)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-specialization")
        endif()
    endif()
    set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
    SDL3_shadercross
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
    GIT_TAG 3a314481ede01f5d966f0e834019942e34c02b82
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_shadercross_static_dxc.cmake
    )
    FetchContent_MakeAvailable(SDL3_shadercross)
    if(SDLSHADERCROSS_DXC)
        # DxcOptimizer references both archives. Declaring the ScalarOpts cycle lets
        # CMake repeat the static libraries for linkers that scan archives in order.
        target_link_libraries(LLVMHLSL PUBLIC LLVMScalarOpts LLVMPassPrinters)
    endif()
    set(CMAKE_CXX_FLAGS "${OPENDBC_SAVED_CMAKE_CXX_FLAGS}")
    unset(OPENDBC_SAVED_CMAKE_CXX_FLAGS)
    foreach(OPENDBC_SPIRV_CROSS_TARGET
            spirv-cross-core spirv-cross-glsl spirv-cross-cpp spirv-cross-reflect
            spirv-cross-msl spirv-cross-hlsl spirv-cross-c)
        if(TARGET ${OPENDBC_SPIRV_CROSS_TARGET})
            set_target_properties(${OPENDBC_SPIRV_CROSS_TARGET} PROPERTIES CXX_STANDARD 17)
        endif()
    endforeach()
endif()

set(OPENDBC_SHADER_SOURCE_DIR "${PROJECT_SOURCE_DIR}/shaders")
set(OPENDBC_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
set(OPENDBC_SHADER_OUTPUTS
    "${OPENDBC_SHADER_OUTPUT_DIR}/dbclassic_tri.vert.spv"
    "${OPENDBC_SHADER_OUTPUT_DIR}/dbclassic_tri.frag.spv"
    "${OPENDBC_SHADER_OUTPUT_DIR}/vr_transfer.vert.spv"
    "${OPENDBC_SHADER_OUTPUT_DIR}/vr_transfer.frag.spv")
set(OPENDBC_CANONICAL_SHADERS
    "${OPENDBC_SHADER_SOURCE_DIR}/dbclassic_tri.vert.spv"
    "${OPENDBC_SHADER_SOURCE_DIR}/dbclassic_tri.frag.spv"
    "${OPENDBC_SHADER_SOURCE_DIR}/vr_transfer.vert.spv"
    "${OPENDBC_SHADER_SOURCE_DIR}/vr_transfer.frag.spv")

add_custom_command(
    OUTPUT ${OPENDBC_SHADER_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OPENDBC_SHADER_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENDBC_SHADER_SOURCE_DIR}/dbclassic_tri.vert.spv"
            "${OPENDBC_SHADER_OUTPUT_DIR}/dbclassic_tri.vert.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENDBC_SHADER_SOURCE_DIR}/dbclassic_tri.frag.spv"
            "${OPENDBC_SHADER_OUTPUT_DIR}/dbclassic_tri.frag.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENDBC_SHADER_SOURCE_DIR}/vr_transfer.vert.spv"
            "${OPENDBC_SHADER_OUTPUT_DIR}/vr_transfer.vert.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENDBC_SHADER_SOURCE_DIR}/vr_transfer.frag.spv"
            "${OPENDBC_SHADER_OUTPUT_DIR}/vr_transfer.frag.spv"
    DEPENDS ${OPENDBC_CANONICAL_SHADERS}
    COMMENT "Copy canonical OpenDBC SPIR-V shaders"
    VERBATIM
)
add_custom_target(opendbc_shaders ALL DEPENDS ${OPENDBC_SHADER_OUTPUTS})

option(OPENDBC_VERIFY_CANONICAL_SHADERS "Build pinned glslang and verify canonical SPIR-V" OFF)
if(OPENDBC_VERIFY_CANONICAL_SHADERS)
    set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG 275822a6261ee689aadb1da5f09a0ec2f058685c)
    FetchContent_MakeAvailable(glslang)
    add_custom_target(opendbc_verify_shaders
        COMMAND ${CMAKE_COMMAND}
            -DOPENDBC_GLSLANG=$<TARGET_FILE:glslang-standalone>
            -DOPENDBC_SHADER_SOURCE_DIR=${OPENDBC_SHADER_SOURCE_DIR}
            -DOPENDBC_SHADER_VERIFY_DIR=${CMAKE_BINARY_DIR}/verified-shaders
            -P ${CMAKE_SOURCE_DIR}/cmake/verify_shaders.cmake
        DEPENDS glslang-standalone
        COMMENT "Verify canonical SPIR-V with pinned glslang"
        VERBATIM)
endif()

# Deploy canonical shaders as regular files next to each executable.
# Usage: opendbc_deploy_resources(<target>)
function(opendbc_deploy_resources target)
    add_dependencies(${target} opendbc_shaders)
    if(IOS)
        set(resource_dir "$<TARGET_BUNDLE_DIR:${target}>")
    elseif(APPLE)
        set(resource_dir "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources")
    else()
        set(resource_dir "$<TARGET_FILE_DIR:${target}>")
    endif()
    if(TARGET openxr_loader)
        add_dependencies(${target} openxr_loader)
        if(WIN32 OR APPLE)
            set(opendbc_loader_name "$<TARGET_FILE_NAME:openxr_loader>")
        else()
            set(opendbc_loader_name "$<TARGET_SONAME_FILE_NAME:openxr_loader>")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${resource_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:openxr_loader>"
                    "${resource_dir}/${opendbc_loader_name}" VERBATIM)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${resource_dir}/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OPENDBC_SHADER_OUTPUTS} "${resource_dir}/shaders"
        COMMENT "Deploy OpenDBC resources for ${target}"
        VERBATIM
    )
endfunction()

# Every OpenDBC target is warning-clean under the same flags; third-party code
# reaches us through SYSTEM include paths so only our own sources are held to it.
option(OPENDBC_SANITIZE "Build OpenDBC targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(dbc_enable_warnings target)
    if(MSVC)
        # C4702 (unreachable code) fires spuriously after inlining.
        target_compile_options(${target} PRIVATE /W4 /WX /wd4702)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # GCC's flow analysis reports false positives inside libstdc++ (<regex>, std::function),
            # more so under the sanitizers.
            target_compile_options(${target} PRIVATE -Wno-maybe-uninitialized -Wno-array-bounds
                                   -Wno-stringop-overread -Wno-stringop-overflow)
        endif()
        if(OPENDBC_SANITIZE)
            # Jolt is built without RTTI, which UBSan's vptr check needs.
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr
                                   -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)
        endif()
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endfunction()

# Imported dependency headers are not ours to fix: mark them as system includes.
function(dbc_mark_system_includes)
    foreach(dep IN LISTS ARGN)
        if(TARGET ${dep})
            get_target_property(real ${dep} ALIASED_TARGET)
            if(NOT real)
                set(real ${dep})
            endif()
            get_target_property(dirs ${real} INTERFACE_INCLUDE_DIRECTORIES)
            if(dirs)
                set_target_properties(${real} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
            endif()
        endif()
    endforeach()
endfunction()
dbc_mark_system_includes(SDL3::SDL3 SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer SDL3_ttf::SDL3_ttf
    assimp::assimp glm::glm spdlog::spdlog_header_only unordered_dense::unordered_dense
    SDL3_shadercross::SDL3_shadercross nlohmann_json::nlohmann_json)

# Declared after the dependencies: DXC defines _USE_MATH_DEFINES itself and builds with /WX.
add_compile_definitions(
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_LEFT_HANDED
    $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>
    # The portable C library calls (fopen, getenv, sscanf) are intended.
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

add_subdirectory(src/language)
add_subdirectory(src/vm)
add_subdirectory(src/engine)
add_subdirectory(src/interpreter)

# The Studio is the product on every platform; mobile builds use its sandboxed
# configuration selected in tools/studio/src/core/platform.hpp.
add_subdirectory(tools/studio)

# --- Regression Tests ---
option(BUILD_TESTS "Build DBA regression tests" ON)
if(BUILD_TESTS)
    enable_testing()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # "portable" marks checks whose result does not depend on the host platform (committed data,
    # generated references, frontend output), so one host can run them; the dba_* tests still
    # execute every behavior witness on each platform.
    add_test(
        NAME language_catalogue_audit
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/audit_language_catalogue.py
    )
    set_tests_properties(language_catalogue_audit PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME dbpro_project_evidence_audit
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/audit_dbpro_projects.py
    )
    set_tests_properties(dbpro_project_evidence_audit PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME dbpro_demo_qualification_inventory
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/qualify_dbpro_examples.py
            --inventory-only
    )
    set_tests_properties(dbpro_demo_qualification_inventory PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME language_signature_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_language_signatures.py --check
    )
    set_tests_properties(language_signature_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME dbpro_extension_evidence_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_dbpro_extension_evidence.py --check
    )
    set_tests_properties(dbpro_extension_evidence_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME language_reference_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_language_references.py --check
    )
    set_tests_properties(language_reference_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME api_documentation_generation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_api_docs.py --check
    )
    set_tests_properties(api_documentation_generation PROPERTIES
        LABELS "unit;documentation;qualification;portable"
    )

    add_test(
        NAME personal_path_audit
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/audit_personal_paths.py
    )
    set_tests_properties(personal_path_audit PROPERTIES
        LABELS "unit;qualification;portable"
    )

    add_test(
        NAME classic_fixture_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_classic_fixtures.py --check
    )
    set_tests_properties(classic_fixture_regeneration PROPERTIES
        LABELS "unit;media;qualification;portable"
    )

    add_test(
        NAME example_media_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_example_media.py --check
    )
    set_tests_properties(example_media_regeneration PROPERTIES
        LABELS "unit;media;qualification;portable"
    )

    add_test(
        NAME example_asset_provenance
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/audit_example_assets.py
    )
    set_tests_properties(example_asset_provenance PROPERTIES
        LABELS "unit;media;qualification;portable"
    )

    add_test(
        NAME dbc_midi_fallback_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_dbc_midi_fallbacks.py --check
    )
    set_tests_properties(dbc_midi_fallback_regeneration PROPERTIES
        LABELS "unit;media;qualification;portable"
    )

    add_test(
        NAME unsupported_compatibility_manifest_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_unsupported_compatibility_manifest.py
            --check
    )
    set_tests_properties(unsupported_compatibility_manifest_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME compatibility_stub_manifest_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_compatibility_stub_manifest.py
            --check --require-no-temporary
    )
    set_tests_properties(compatibility_stub_manifest_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME install_resource_validation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test_qualify_install.py
    )
    set_tests_properties(install_resource_validation PROPERTIES
        LABELS "unit;qualification"
    )

    add_test(
        NAME behavior_manifest_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_behavior_manifest.py
            --opendbc $<TARGET_FILE:opendbc> --check
    )
    set_tests_properties(behavior_manifest_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
        RESOURCE_LOCK "opendbc_runtime"
    )

    add_test(
        NAME corpus_expectations_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_corpus_expectations.py
            --opendbc $<TARGET_FILE:opendbc> --check
    )
    set_tests_properties(corpus_expectations_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
        RESOURCE_LOCK "opendbc_runtime"
    )

    add_test(
        NAME example_frames
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_example_frames.py
            --opendbc $<TARGET_FILE:opendbc>
    )
    set_tests_properties(example_frames PROPERTIES
        LABELS "runtime;qualification"
        RESOURCE_LOCK "opendbc_runtime"
        ENVIRONMENT "OPENDBC_HEADLESS=1"
    )

    add_test(
        NAME readme_metrics_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_readme_metrics.py --check
    )
    set_tests_properties(readme_metrics_regeneration PROPERTIES
        LABELS "unit;qualification;portable"
    )

    # DBA script regression tests
    set(DBA_TEST_DIR "${CMAKE_SOURCE_DIR}/tests/dba")
    file(STRINGS "${DBA_TEST_DIR}/test_profiles.tsv" DBA_TEST_PROFILES)
    foreach(profile_row ${DBA_TEST_PROFILES})
        if(profile_row MATCHES "^#" OR profile_row STREQUAL "")
            continue()
        endif()
        string(REPLACE "\t" ";" profile_fields "${profile_row}")
        list(LENGTH profile_fields profile_field_count)
        if(NOT profile_field_count EQUAL 2)
            message(FATAL_ERROR "Invalid BASIC test profile row: ${profile_row}")
        endif()
        list(GET profile_fields 0 test_source_name)
        list(GET profile_fields 1 test_dialect)
        set(test_script "${DBA_TEST_DIR}/${test_source_name}")
        if(NOT EXISTS "${test_script}")
            message(FATAL_ERROR "Missing BASIC test source declared by test_profiles.tsv: ${test_script}")
        endif()
        get_filename_component(test_name ${test_script} NAME_WE)
        set(expected_file "${DBA_TEST_DIR}/${test_name}.expected")
        if(EXISTS ${expected_file})
            add_test(
                NAME "dba_${test_name}"
                COMMAND ${CMAKE_COMMAND}
                    -DOPENDBC=$<TARGET_FILE:opendbc>
                    -DTEST_SCRIPT=${test_script}
                    -DDIALECT=${test_dialect}
                    -DEXPECTED_FILE=${expected_file}
                    -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/tests/${test_name}.out
                    -P ${CMAKE_SOURCE_DIR}/tests/dba/run_test.cmake
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            )
            set_tests_properties("dba_${test_name}" PROPERTIES
                TIMEOUT 60
                LABELS "dba;runtime"
                RESOURCE_LOCK "opendbc_runtime"
            )
        endif()
    endforeach()

    # C++ unit tests (doctest)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG ae7a13539fb71f270b87eb2e874fbac80bc8dda2
    )
    FetchContent_MakeAvailable(doctest)
    list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
    add_subdirectory(tests/unit)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(OPENDBC_COPY_COMPILE_COMMANDS_SCRIPT
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_compile_commands_if_exists.cmake)
    add_custom_target(opendbc_compile_commands ALL
    COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${CMAKE_BINARY_DIR}/compile_commands.json
            -DOUTPUT_FILE=${CMAKE_SOURCE_DIR}/compile_commands.json
            -P ${OPENDBC_COPY_COMPILE_COMMANDS_SCRIPT}
    COMMENT "Copy compile_commands.json to source root for editors (Zed/LSP)."
    VERBATIM
  )
endif()

set(CPACK_PACKAGE_NAME "OpenDBC")
set(CPACK_PACKAGE_VENDOR "OpenDBC")
set(CPACK_PACKAGE_VERSION "${OPENDBC_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "OpenDBC-${OPENDBC_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${CMAKE_PROJECT_NAME};Runtime;/")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/docs/NOTICE.md")
include(CPack)
