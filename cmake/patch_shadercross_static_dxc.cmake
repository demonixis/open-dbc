if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(DXC_CMAKE
    "${SOURCE_DIR}/external/DirectXShaderCompiler/tools/clang/tools/dxcompiler/CMakeLists.txt")
file(READ "${DXC_CMAKE}" DXC_CONTENTS)

if(DXC_CONTENTS MATCHES "add_clang_library\\(dxcompiler SHARED")
    string(REPLACE "add_clang_library(dxcompiler SHARED"
        "add_clang_library(dxcompiler STATIC" DXC_CONTENTS "${DXC_CONTENTS}")
    file(WRITE "${DXC_CMAKE}" "${DXC_CONTENTS}")
elseif(NOT DXC_CONTENTS MATCHES "add_clang_library\\(dxcompiler STATIC")
    message(FATAL_ERROR "Pinned DXC library declaration changed; update the static-link patch")
endif()

if(NOT DXC_CONTENTS MATCHES "libclang/dxcrewriteunused.cpp")
    string(REPLACE "  dxcshadersourceinfo.cpp\n)"
        "  dxcshadersourceinfo.cpp\n  ../libclang/dxcrewriteunused.cpp\n)"
        DXC_CONTENTS "${DXC_CONTENTS}")
endif()
string(REPLACE "  clangTooling\n" "" DXC_CONTENTS "${DXC_CONTENTS}")
# A static library must not carry the DLL's version resource: MSVC rejects it next to the
# executable's own manifest resource (LNK1241).
string(REPLACE "  DXCompiler.rc\n" "" DXC_CONTENTS "${DXC_CONTENTS}")
string(REPLACE "  libclang\n" "" DXC_CONTENTS "${DXC_CONTENTS}")
if(NOT DXC_CONTENTS MATCHES "target_compile_definitions\\(dxcompiler PRIVATE OPENDBC_DXC_STATIC\\)")
    string(REPLACE "add_clang_library(dxcompiler STATIC \${SOURCES})\n"
        "add_clang_library(dxcompiler STATIC \${SOURCES})\ntarget_compile_definitions(dxcompiler PRIVATE OPENDBC_DXC_STATIC)\n"
        DXC_CONTENTS "${DXC_CONTENTS}")
endif()
file(WRITE "${DXC_CMAKE}" "${DXC_CONTENTS}")

set(DXC_ENTRY
    "${SOURCE_DIR}/external/DirectXShaderCompiler/tools/clang/tools/dxcompiler/DXCompiler.cpp")
file(READ "${DXC_ENTRY}" DXC_ENTRY_CONTENTS)
if(NOT DXC_ENTRY_CONTENTS MATCHES "OPENDBC_DXC_STATIC")
    string(REPLACE "#if defined(LLVM_ON_UNIX)\n"
        [=[#if defined(OPENDBC_DXC_STATIC)
namespace {
class DxcStaticLifetime {
public:
  DxcStaticLifetime() { InitMaybeFail(); }
  ~DxcStaticLifetime() {
    DxcSetThreadMallocToDefault();
    ::hlsl::options::cleanupHlslOptTable();
    ::llvm::sys::fs::CleanupPerThreadFileSystem();
    ::llvm::llvm_shutdown();
#if defined(LLVM_ON_WIN32)
    DxilLibCleanup(DxilLibCleanUpType::UnloadLibrary);
#endif
    DxcClearThreadMalloc();
    DxcCleanupThreadMalloc();
  }
};

DxcStaticLifetime StaticLifetime;
} // namespace
#elif defined(LLVM_ON_UNIX)
]=]
        DXC_ENTRY_CONTENTS "${DXC_ENTRY_CONTENTS}")
    file(WRITE "${DXC_ENTRY}" "${DXC_ENTRY_CONTENTS}")
endif()

set(DXC_API
    "${SOURCE_DIR}/external/DirectXShaderCompiler/tools/clang/tools/dxcompiler/dxcapi.cpp")
file(READ "${DXC_API}" DXC_API_CONTENTS)
string(REPLACE "HRESULT CreateDxcIntelliSense(REFIID riid, _Out_ LPVOID *ppv);\n" ""
    DXC_API_CONTENTS "${DXC_API_CONTENTS}")
string(REPLACE
    "  } else if (IsEqualCLSID(rclsid, CLSID_DxcIntelliSense)) {\n    hr = CreateDxcIntelliSense(riid, ppv);\n"
    "" DXC_API_CONTENTS "${DXC_API_CONTENTS}")
file(WRITE "${DXC_API}" "${DXC_API_CONTENTS}")

set(SHADERCROSS_CMAKE "${SOURCE_DIR}/CMakeLists.txt")
file(READ "${SHADERCROSS_CMAKE}" SHADERCROSS_CONTENTS)

if(SHADERCROSS_CONTENTS MATCHES "list\\(APPEND vendored_targets dxcompiler dxildll\\)")
    string(REPLACE "\t\tlist(APPEND vendored_targets dxcompiler dxildll)\n" ""
        SHADERCROSS_CONTENTS "${SHADERCROSS_CONTENTS}")
elseif(SHADERCROSS_CONTENTS MATCHES "vendored_targets dxcompiler")
    message(FATAL_ERROR "Pinned SDL_shadercross DXC export changed; update the static-link patch")
endif()
string(REPLACE "\t\tadd_dependencies(dxcompiler dxildll)\n" ""
    SHADERCROSS_CONTENTS "${SHADERCROSS_CONTENTS}")
string(REPLACE
    "\t\ttarget_link_libraries(\${target} PRIVATE DirectXShaderCompiler::dxcompiler)\n"
    "\t\ttarget_link_libraries(\${target} PRIVATE \"$<LINK_LIBRARY:WHOLE_ARCHIVE,DirectXShaderCompiler::dxcompiler>\")\n"
    SHADERCROSS_CONTENTS "${SHADERCROSS_CONTENTS}")

string(REPLACE
    "\texport(TARGETS \${vendored_targets} NAMESPACE \"SDL3_shadercross::\" FILE \"SDL3_shadercross-vendored-targets.cmake\")\n"
    "" SHADERCROSS_CONTENTS "${SHADERCROSS_CONTENTS}")
string(REPLACE
    "\texport(TARGETS SDL3_shadercross-static NAMESPACE \"SDL3_shadercross::\" FILE \"SDL3_shadercross-static-targets.cmake\")\n"
    "" SHADERCROSS_CONTENTS "${SHADERCROSS_CONTENTS}")
file(WRITE "${SHADERCROSS_CMAKE}" "${SHADERCROSS_CONTENTS}")
