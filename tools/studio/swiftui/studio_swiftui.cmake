# The SwiftUI Studio: the Dear ImGui Studio's sources without its interface, behind an
# Objective-C++ bridge (bridge/), under a SwiftUI interface (app/). Included from
# tools/studio/CMakeLists.txt, whose source file properties (bundle resources) it shares.

get_target_property(STUDIO_SWIFTUI_SOURCES opendbc SOURCES)
# The ImGui application and its panels stay behind. The Scene panels come along: they hold the
# level editor's model, and the viewport still draws through them.
list(FILTER STUDIO_SWIFTUI_SOURCES EXCLUDE REGEX "src/(main|app)\\.(cpp|hpp)$")
list(FILTER STUDIO_SWIFTUI_SOURCES EXCLUDE REGEX
    "src/panels/(asset_|build_|code_|completion_|editor_|media_|output_|problems_|project_|reference_|snippet_)")
list(FILTER STUDIO_SWIFTUI_SOURCES EXCLUDE REGEX "src/dba/language\\.(cpp|hpp)$")
list(FILTER STUDIO_SWIFTUI_SOURCES EXCLUDE REGEX "src/core/screen_keyboard_ios\\.mm$")

file(GLOB STUDIO_SWIFTUI_APP CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/app/*.swift")
file(GLOB STUDIO_SWIFTUI_BRIDGE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/bridge/*.mm"
    "${CMAKE_CURRENT_LIST_DIR}/bridge/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/bridge/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/bridge/*.hpp")
set(STUDIO_SWIFTUI_BRIDGE_OBJC ${STUDIO_SWIFTUI_BRIDGE})
list(FILTER STUDIO_SWIFTUI_BRIDGE_OBJC INCLUDE REGEX "\\.mm$")
set_source_files_properties(${STUDIO_SWIFTUI_BRIDGE_OBJC} PROPERTIES COMPILE_OPTIONS -fobjc-arc)

add_executable(opendbc_swiftui MACOSX_BUNDLE
    ${STUDIO_SWIFTUI_SOURCES} ${STUDIO_SWIFTUI_APP} ${STUDIO_SWIFTUI_BRIDGE})

foreach(STUDIO_SWIFTUI_PROPERTY IN ITEMS
        INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(STUDIO_SWIFTUI_VALUE opendbc ${STUDIO_SWIFTUI_PROPERTY})
    if(STUDIO_SWIFTUI_VALUE)
        set_target_properties(opendbc_swiftui PROPERTIES ${STUDIO_SWIFTUI_PROPERTY} "${STUDIO_SWIFTUI_VALUE}")
    endif()
endforeach()
# The C and C++ options mean nothing to the Swift compiler, which refuses them.
get_target_property(STUDIO_SWIFTUI_OPTIONS opendbc COMPILE_OPTIONS)
set(STUDIO_SWIFTUI_C_OPTIONS)
foreach(STUDIO_SWIFTUI_OPTION IN LISTS STUDIO_SWIFTUI_OPTIONS)
    list(APPEND STUDIO_SWIFTUI_C_OPTIONS "$<$<NOT:$<COMPILE_LANGUAGE:Swift>>:${STUDIO_SWIFTUI_OPTION}>")
endforeach()
set_target_properties(opendbc_swiftui PROPERTIES COMPILE_OPTIONS "${STUDIO_SWIFTUI_C_OPTIONS}")
target_include_directories(opendbc_swiftui PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bridge")
target_compile_definitions(opendbc_swiftui PRIVATE DBC_STUDIO_SWIFTUI=1)
target_link_libraries(opendbc_swiftui PRIVATE "-framework WebKit")

get_target_property(STUDIO_SWIFTUI_DEPENDENCIES opendbc MANUALLY_ADDED_DEPENDENCIES)
if(STUDIO_SWIFTUI_DEPENDENCIES)
    add_dependencies(opendbc_swiftui ${STUDIO_SWIFTUI_DEPENDENCIES})
endif()

# The same bundle description as the Dear ImGui Studio, under its own name.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../ios/Info.plist.in" STUDIO_SWIFTUI_PLIST)
string(REPLACE "<string>OpenDBC</string>" "<string>OpenDBC SwiftUI</string>" STUDIO_SWIFTUI_PLIST
    "${STUDIO_SWIFTUI_PLIST}")
string(CONFIGURE "${STUDIO_SWIFTUI_PLIST}" STUDIO_SWIFTUI_PLIST @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Info-swiftui.plist" "${STUDIO_SWIFTUI_PLIST}")

set_target_properties(opendbc_swiftui PROPERTIES
    OUTPUT_NAME "OpenDBCSwiftUI"
    BUNDLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info-swiftui.plist"
    # Its own identifier: both Studios install side by side.
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OPENDBC_SWIFTUI_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.3"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "YES"
    XCODE_ATTRIBUTE_DERIVE_MACCATALYST_PRODUCT_BUNDLE_IDENTIFIER "NO"
    # CMake names the Swift libraries of the SDK it was configured with; a Mac Catalyst build
    # links the Mac's, so the search path follows the SDK being built for.
    XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS "$(SDKROOT)/usr/lib/swift $(inherited)"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    "XCODE_ATTRIBUTE_SWIFT_ACTIVE_COMPILATION_CONDITIONS[variant=Debug]" "DEBUG"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_LIST_DIR}/bridge/OpenDBCStudio-Bridging-Header.h"
    XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "${OPENDBC_IOS_SIGNING_ALLOWED}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OPENDBC_IOS_TEAM_ID}"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_GENERATE_SCHEME ON
)
