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

# Exported games inherit the runner's minimum macOS, so it is fixed instead of following the build Mac.
# 13.3 is the first release whose libc++ formats floating-point numbers with std::to_chars.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
   AND (NOT DEFINED CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
   AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Oldest macOS release OpenDBC and its exports run on" FORCE)
endif()

project(OpenDBC VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL
    "Suppress deprecation warnings from pinned third-party CMake projects" FORCE)

# --- Versioning ------------------------------------------------------------
# Marketing version lives here and nowhere else. The build number comes from
# the commit count: monotonic, automatic, and never regresses, which is what
# TestFlight requires of every upload. It is refreshed when CMake re-runs.
# Not a cache entry, so an existing build directory picks up a new version.
set(OPENDBC_VERSION "${PROJECT_VERSION}")

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
if(WIN32)
    enable_language(RC)
    if(OPENDBC_BUILD_NUMBER GREATER 65535)
        message(FATAL_ERROR "Windows version resources require a build number no greater than 65535")
    endif()
    set(OPENDBC_WINDOWS_VERSION_COMMAS
        "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${OPENDBC_BUILD_NUMBER}")
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
    "shaders/*.hlsl" "shaders/*.spv" "shaders/*.msl"
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
# Shipped by the Studio; export hosts record its hash to prove they run the same runtime.
set(OPENDBC_RUNTIME_MANIFEST "${CMAKE_BINARY_DIR}/opendbc-runtime.json")
configure_file(cmake/opendbc-runtime.json.in "${OPENDBC_RUNTIME_MANIFEST}" @ONLY)
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
        /experimental:deterministic
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
        # CMake repeats DXC's cyclic archives for order-sensitive linkers; ld-prime
        # resolves them in one pass and warns about the repetition. Older ld64 lacks the flag.
        include(CheckLinkerFlag)
        check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" OPENDBC_HAS_NO_WARN_DUPLICATE_LIBRARIES)
        if(OPENDBC_HAS_NO_WARN_DUPLICATE_LIBRARIES)
            add_link_options("LINKER:-no_warn_duplicate_libraries")
        endif()
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
include(cmake/fetch_dependency.cmake)

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

opendbc_fetch_dependency(SDL3
  https://github.com/libsdl-org/SDL.git
  c1ef7793c75c2bde6321ec0c9894ea0b2a7694fe
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
    opendbc_fetch_dependency(opendbc_openxr
        https://github.com/KhronosGroup/OpenXR-SDK.git
        105a2a674cd7aae85de8d6a43f2d2cbdaa80af60)
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

opendbc_fetch_dependency(SDL3_image
  https://github.com/libsdl-org/SDL_image.git
  ad58ecfc27a1bd09e510ceff8bbbdb1094806476
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

opendbc_fetch_dependency(SDL3_mixer
  https://github.com/libsdl-org/SDL_mixer.git
  da02588817a565fccfa60fa2f0bb661a3c77b8f0
  PATCH_COMMAND ${CMAKE_COMMAND}
                -DOPENDBC_GIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DOPENDBC_PATCH_SOURCE_DIR=<SOURCE_DIR>
                -DOPENDBC_PATCH_FILE=${CMAKE_SOURCE_DIR}/cmake/patches/sdl-mixer-wav-xlaw.patch
                -P ${CMAKE_SOURCE_DIR}/cmake/apply_idempotent_patch.cmake
)
FetchContent_MakeAvailable(SDL3_mixer)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_ZLIB ON CACHE BOOL "" FORCE)

opendbc_fetch_dependency(SDL3_ttf
  https://github.com/libsdl-org/SDL_ttf.git
  a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
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

opendbc_fetch_dependency(assimp
  https://github.com/assimp/assimp.git
  c35200e38ea8f058812b83de2ef32c6093b0ece2
)
FetchContent_MakeAvailable(assimp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET assimp)
    target_compile_options(assimp PRIVATE -Wno-nontrivial-memcall)
endif()

opendbc_fetch_dependency(glm
  https://github.com/g-truc/glm.git
  0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
set(GLM_ENABLE_CXX_17 ON CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "Use native Windows paths for log files" FORCE)
endif()
opendbc_fetch_dependency(spdlog
  https://github.com/gabime/spdlog.git
  6fa36017cfd5731d617e1a934f0e5ea9c4445b13
)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

opendbc_fetch_dependency(unordered_dense
  https://github.com/martinus/unordered_dense.git
  73f3cbb237e84d483afafc743f1f14ec53e12314
)
FetchContent_MakeAvailable(unordered_dense)

# Classic multiplayer runs over ENet, a small UDP library with reliable channels and fragmentation.
opendbc_fetch_dependency(enet
  https://github.com/lsalzman/enet.git
  2662c0de09e36f2a2030ccc2c528a3e4c9e8138a # v1.3.18
)
FetchContent_MakeAvailable(enet)
target_include_directories(enet SYSTEM INTERFACE ${enet_SOURCE_DIR}/include)
# ENet links the Windows socket libraries for MinGW only.
if(WIN32)
  target_link_libraries(enet INTERFACE ws2_32 winmm)
endif()

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
opendbc_fetch_dependency(JoltPhysics
  https://github.com/jrouwe/JoltPhysics.git
  e77f175595e64cb44218cc9d9d56fc365ad0e36a
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
    if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950 AND SDLSHADERCROSS_DXC)
        # Pinned DXC specializes is_nothrow_constructible for StringRef, also handled
        # below for Apple Clang. VS 2026 diagnoses those legacy specializations.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd5285")
    endif()
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
    opendbc_fetch_dependency(SDL3_shadercross
    https://github.com/libsdl-org/SDL_shadercross.git
    3a314481ede01f5d966f0e834019942e34c02b82
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_shadercross_static_dxc.cmake
    )
    # DXC caches paths into its own sources; drop those left by an earlier checkout.
    foreach(OPENDBC_DXC_PATH DXC_EXTERNAL_ROOT_DIR DXC_SPIRV_HEADERS_DIR DXC_SPIRV_TOOLS_DIR)
        string(FIND "${${OPENDBC_DXC_PATH}}" "${SDL3_shadercross_CACHE_DIR}/" OPENDBC_DXC_PATH_POSITION)
        if(DEFINED CACHE{${OPENDBC_DXC_PATH}} AND NOT OPENDBC_DXC_PATH_POSITION EQUAL 0)
            unset(${OPENDBC_DXC_PATH} CACHE)
        endif()
    endforeach()
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
            spirv-cross-msl spirv-cross-hlsl spirv-cross-c
            SPIRV-Tools-static SPIRV-Tools-opt SPIRV-Tools-link SPIRV-Tools-reduce
            SPIRV-Tools-diff SPIRV-Tools-lint)
        if(TARGET ${OPENDBC_SPIRV_CROSS_TARGET})
            set_target_properties(${OPENDBC_SPIRV_CROSS_TARGET} PROPERTIES CXX_STANDARD 17)
        endif()
    endforeach()
endif()

set(OPENDBC_SHADER_SOURCE_DIR "${PROJECT_SOURCE_DIR}/shaders")
set(OPENDBC_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
set(OPENDBC_SHADER_FILES
    dbclassic_tri.vert.spv dbclassic_tri.frag.spv dbclassic_tri.vert.msl dbclassic_tri.frag.msl
    dbclassic_mesh.vert.spv dbclassic_mesh.vert.msl
    shadow_volume.vert.spv shadow_volume.frag.spv shadow_volume.vert.msl shadow_volume.frag.msl
    vr_transfer.vert.spv vr_transfer.frag.spv vr_transfer.vert.msl vr_transfer.frag.msl)
list(TRANSFORM OPENDBC_SHADER_FILES PREPEND "${OPENDBC_SHADER_OUTPUT_DIR}/" OUTPUT_VARIABLE OPENDBC_SHADER_OUTPUTS)
list(TRANSFORM OPENDBC_SHADER_FILES PREPEND "${OPENDBC_SHADER_SOURCE_DIR}/" OUTPUT_VARIABLE OPENDBC_CANONICAL_SHADERS)

add_custom_command(
    OUTPUT ${OPENDBC_SHADER_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OPENDBC_SHADER_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENDBC_CANONICAL_SHADERS} "${OPENDBC_SHADER_OUTPUT_DIR}"
    DEPENDS ${OPENDBC_CANONICAL_SHADERS}
    COMMENT "Copy canonical OpenDBC shaders"
    VERBATIM
)
add_custom_target(opendbc_shaders ALL DEPENDS ${OPENDBC_SHADER_OUTPUTS})

option(OPENDBC_VERIFY_CANONICAL_SHADERS "Build opendbc_shaderc and verify the canonical shaders" OFF)
if(OPENDBC_VERIFY_CANONICAL_SHADERS AND NOT SDLSHADERCROSS_DXC)
    message(FATAL_ERROR "Canonical shader verification needs SDL_shadercross with DXC")
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

# Packagers building with a newer compiler than the maintainers can turn it off.
option(OPENDBC_WERROR "Treat warnings in OpenDBC's own sources as errors" ON)

function(dbc_enable_warnings target)
    if(MSVC)
        # C4702 (unreachable code) fires spuriously after inlining.
        target_compile_options(${target} PRIVATE /W4 /wd4702)
        if(OPENDBC_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(OPENDBC_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
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
            # Guides tests/fuzz. Without its callbacks the sanitizer runtime's own do next to nothing.
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                target_compile_options(${target} PRIVATE -fsanitize-coverage=trace-pc-guard)
            endif()
        endif()
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endfunction()

# Asset files reach these decoders first. Memory errors only: their undefined behaviour is not ours to fix.
if(OPENDBC_SANITIZE AND NOT MSVC)
    foreach(decoder IN ITEMS SDL3_mixer-static SDL3_image-static)
        target_compile_options(${decoder} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${decoder} PRIVATE -fsanitize-coverage=trace-pc-guard)
        endif()
    endforeach()
endif()

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
add_subdirectory(tools/runner)
add_subdirectory(tools/studio)
if(OPENDBC_VERIFY_CANONICAL_SHADERS)
    add_subdirectory(tools/shaderc)
endif()

# --- Regression Tests ---
option(BUILD_TESTS "Build DBA regression tests" ON)
if(BUILD_TESTS)
    enable_testing()
    # Avoid doctest's forward declarations of standard-library types on recent MSVC.
    set(DOCTEST_USE_STD_HEADERS ON CACHE BOOL "Use standard headers in doctest" FORCE)

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
    # Batch gates that run every witness or example take longer than the 120 s default.
    set_tests_properties(behavior_manifest_regeneration PROPERTIES
        TIMEOUT 300
        LABELS "unit;language;qualification;portable"
        RESOURCE_LOCK "dba_written_files"
    )

    add_test(
        NAME corpus_expectations_regeneration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_corpus_expectations.py
            --opendbc $<TARGET_FILE:opendbc> --check
    )
    set_tests_properties(corpus_expectations_regeneration PROPERTIES
        LABELS "unit;language;qualification;portable"
    )

    add_test(
        NAME example_frames
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_example_frames.py
            --opendbc $<TARGET_FILE:opendbc>
    )
    set_tests_properties(example_frames PROPERTIES
        TIMEOUT 300
        LABELS "runtime;qualification"
        ENVIRONMENT "OPENDBC_HEADLESS=1"
    )

    # Keeps the renderer stress scenes running; timing them is scripts/benchmark_renderer.py's job.
    add_test(
        NAME benchmark_scenes
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/benchmark_renderer.py run
            --opendbc $<TARGET_FILE:opendbc> --output ${CMAKE_CURRENT_BINARY_DIR}/benchmark-scenes
            --runs 1 --frames 3 --only shared_mesh distinct_meshes transparency skinned two_views shadow_casters
    )
    set_tests_properties(benchmark_scenes PROPERTIES
        TIMEOUT 300
        LABELS "runtime;qualification"
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
    # These programs write files beside their source. Their source run, their image run and the
    # behavior manifest generator share a lock so two runs of one program never write the same
    # file at once; every other test runs in parallel.
    set(DBA_TESTS_WRITING_FILES
        test_array_namespace test_dbpro_display_image_text test_dbpro_file_memblock test_file_io test_images)
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
            )
            if(test_name IN_LIST DBA_TESTS_WRITING_FILES)
                set_tests_properties("dba_${test_name}" PROPERTIES RESOURCE_LOCK "dba_written_files")
            endif()
            if(TARGET opendbc-runner)
                add_test(
                    NAME "dba_image_${test_name}"
                    COMMAND ${CMAKE_COMMAND}
                        -DOPENDBC=$<TARGET_FILE:opendbc>
                        -DRUNNER=$<TARGET_FILE:opendbc-runner>
                        -DTEST_SCRIPT=${test_script}
                        -DDIALECT=${test_dialect}
                        -DEXPECTED_FILE=${expected_file}
                        -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/tests/images/${test_name}.out
                        -P ${CMAKE_SOURCE_DIR}/tests/dba/run_image_test.cmake
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                )
                set_tests_properties("dba_image_${test_name}" PROPERTIES
                    TIMEOUT 60
                    LABELS "dba;runtime;runner"
                )
                if(test_name IN_LIST DBA_TESTS_WRITING_FILES)
                    set_tests_properties("dba_image_${test_name}" PROPERTIES RESOURCE_LOCK "dba_written_files")
                endif()
            endif()
        endif()
    endforeach()
    if(TARGET opendbc-runner AND CMAKE_NM)
        add_test(
            NAME runner_links_no_frontend
            COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DRUNNER=$<TARGET_FILE:opendbc-runner>
                -P ${CMAKE_SOURCE_DIR}/tests/dba/check_runner_symbols.cmake
        )
        set_tests_properties(runner_links_no_frontend PROPERTIES LABELS "unit;runner")
    endif()
    if(APPLE AND TARGET opendbc-runner)
        # Exported macOS games run wherever the runner does, not only on the build Mac's release.
        string(REPLACE "." "\\." OPENDBC_MACOS_MINIMUM_PATTERN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        add_test(NAME runner_macos_minimum
            COMMAND /usr/bin/xcrun vtool -show-build $<TARGET_FILE:opendbc-runner>)
        set_tests_properties(runner_macos_minimum PROPERTIES
            LABELS "unit;runner"
            PASS_REGULAR_EXPRESSION "minos ${OPENDBC_MACOS_MINIMUM_PATTERN}\n")
    endif()

    # C++ unit tests (doctest)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    opendbc_fetch_dependency(doctest
        https://github.com/doctest/doctest.git
        ae7a13539fb71f270b87eb2e874fbac80bc8dda2
    )
    FetchContent_MakeAvailable(doctest)
    list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
    add_subdirectory(tests/unit)
    if(OPENDBC_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT WIN32 AND NOT ANDROID AND NOT IOS)
        add_subdirectory(tests/fuzz)
    endif()
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
