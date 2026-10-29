if(ANDROID)
    # SDL's Android activity loads the game from libmain.so.
    add_library(opendbc-runner SHARED main.cpp)
    set_target_properties(opendbc-runner PROPERTIES OUTPUT_NAME main)
    target_link_options(opendbc-runner PRIVATE $<$<CONFIG:Release>:-Wl,--strip-all>)
    # Google Play requires 16 KB page alignment for apps that target Android 15 and ship native code.
    target_link_options(opendbc-runner PRIVATE -Wl,-z,max-page-size=16384)
elseif(IOS)
    # iOS games are exported as Xcode projects that link the runner from one static archive, so Xcode
    # signs, archives and uploads them like any app (examples/ios/export-template).
    add_library(opendbc-runner OBJECT main.cpp)
else()
    add_executable(opendbc-runner MACOSX_BUNDLE main.cpp)
endif()
if(WIN32)
    # Exports replace this icon and version with the game's own.
    set(OPENDBC_RUNNER_ICON "${CMAKE_SOURCE_DIR}/resources/editor.ico")
    configure_file(runner.rc.in "${CMAKE_CURRENT_BINARY_DIR}/runner.rc" @ONLY)
    target_sources(opendbc-runner PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/runner.rc")
endif()
target_link_libraries(opendbc-runner PRIVATE dbc_interpreter)
dbc_enable_warnings(opendbc-runner)
if(NOT IOS)
    opendbc_deploy_resources(opendbc-runner)
endif()

if(APPLE AND NOT IOS)
    set_target_properties(opendbc-runner PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "OpenDBC Runner"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.demonixis.opendbc.runner"
    )
endif()

# Walks the link interface of TARGET and returns the static library targets it pulls in, and the other
# items (system frameworks and libraries) an executable linking them needs.
function(opendbc_collect_link_closure TARGET OUT_ARCHIVES OUT_FLAGS)
    set(Pending ${TARGET})
    set(Visited)
    set(Archives)
    set(Flags)
    while(Pending)
        list(POP_FRONT Pending Current)
        if(Current IN_LIST Visited)
            continue()
        endif()
        list(APPEND Visited ${Current})
        get_target_property(Aliased ${Current} ALIASED_TARGET)
        if(Aliased)
            set(Current ${Aliased})
        endif()
        get_target_property(Type ${Current} TYPE)
        get_target_property(Imported ${Current} IMPORTED)
        set(Links)
        if(Type STREQUAL "STATIC_LIBRARY" AND NOT Imported)
            list(APPEND Archives ${Current})
            get_target_property(Direct ${Current} LINK_LIBRARIES)
            if(Direct)
                list(APPEND Links ${Direct})
            endif()
        elseif(Imported AND Type MATCHES "SHARED_LIBRARY|UNKNOWN_LIBRARY")
            # System libraries found by find_package, such as ZLIB::ZLIB and its libz.tbd.
            get_target_property(Location ${Current} IMPORTED_LOCATION)
            if(NOT Location)
                get_target_property(Location ${Current} IMPORTED_LOCATION_RELEASE)
            endif()
            get_filename_component(LocationName "${Location}" NAME)
            if(LocationName MATCHES "^lib(.+)\\.(tbd|dylib)$")
                list(APPEND Flags "-l${CMAKE_MATCH_1}")
            elseif(LocationName MATCHES "^(.+)\\.framework$")
                list(APPEND Flags "-framework ${CMAKE_MATCH_1}")
            endif()
        endif()
        get_target_property(Interface ${Current} INTERFACE_LINK_LIBRARIES)
        if(Interface)
            list(APPEND Links ${Interface})
        endif()
        foreach(Link IN LISTS Links)
            # Static libraries publish their private dependencies as $<LINK_ONLY:...>; SDL names its
            # frameworks with $<LINK_LIBRARY:FRAMEWORK,...>.
            while(Link MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE|TARGET_NAME):(.*)>$")
                set(Link "${CMAKE_MATCH_2}")
            endwhile()
            if(Link MATCHES "^\\$<LINK_LIBRARY:FRAMEWORK,(.+)>$")
                list(APPEND Flags "-framework ${CMAKE_MATCH_1}")
            elseif(Link MATCHES "^\\$<LINK_LIBRARY:WEAK_FRAMEWORK,(.+)>$")
                list(APPEND Flags "-weak_framework ${CMAKE_MATCH_1}")
            elseif(TARGET ${Link})
                list(APPEND Pending ${Link})
            elseif(Link MATCHES "^-framework |^-weak_framework |^-l")
                list(APPEND Flags "${Link}")
            elseif(Link MATCHES "/lib([^/>]+)\\.(tbd|dylib)>?$")
                # A system library path, possibly inside a configuration expression such as assimp's zlib.
                list(APPEND Flags "-l${CMAKE_MATCH_1}")
            elseif(Link MATCHES "^[A-Za-z0-9_+]+$")
                list(APPEND Flags "-l${Link}")
            endif()
        endforeach()
    endwhile()
    list(REMOVE_DUPLICATES Flags)
    set(${OUT_ARCHIVES} ${Archives} PARENT_SCOPE)
    set(${OUT_FLAGS} ${Flags} PARENT_SCOPE)
endfunction()

# Exports start from this runner; its host manifest ties the binary to the runtime sources.
if(ANDROID)
    add_custom_target(opendbc_export_host_manifest ALL
        COMMAND ${CMAKE_COMMAND}
            -DOPENDBC_RUNTIME_MANIFEST=${OPENDBC_RUNTIME_MANIFEST}
            -DOPENDBC_HOST_BINARY=$<TARGET_FILE:opendbc-runner>
            -DOPENDBC_HOST_MANIFEST=$<TARGET_FILE_DIR:opendbc-runner>/opendbc-host.json
            -DOPENDBC_ANDROID_HOST=1
            -DOPENDBC_OBJDUMP=${CMAKE_OBJDUMP}
            -P ${CMAKE_SOURCE_DIR}/cmake/write_host_manifest.cmake
        DEPENDS opendbc-runner
        COMMENT "Binding the OpenDBC runtime fingerprint to the Android export host"
        VERBATIM)
elseif(IOS)
    opendbc_collect_link_closure(dbc_interpreter OPENDBC_IOS_ARCHIVE_TARGETS OPENDBC_IOS_LINK_FLAGS)
    list(TRANSFORM OPENDBC_IOS_ARCHIVE_TARGETS REPLACE "^(.+)$" "$<TARGET_FILE:\\1>"
        OUTPUT_VARIABLE OPENDBC_IOS_ARCHIVES)
    set(OPENDBC_IOS_RUNTIME "${CMAKE_CURRENT_BINARY_DIR}/libOpenDBCRuntime.a")
    add_custom_command(OUTPUT "${OPENDBC_IOS_RUNTIME}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${OPENDBC_IOS_RUNTIME}"
        COMMAND /usr/bin/libtool -static -no_warning_for_no_symbols -o "${OPENDBC_IOS_RUNTIME}"
            "$<TARGET_OBJECTS:opendbc-runner>" ${OPENDBC_IOS_ARCHIVES}
        DEPENDS opendbc-runner ${OPENDBC_IOS_ARCHIVE_TARGETS}
        COMMAND_EXPAND_LISTS
        COMMENT "Merging the OpenDBC iOS runtime archive"
        VERBATIM)
    # The exported Xcode project links the system frameworks the runtime needs. -ObjC keeps SDL's
    # Objective-C classes, which UIKit only finds by name.
    string(REPLACE ";" " " OPENDBC_IOS_LDFLAGS "-ObjC;${OPENDBC_IOS_LINK_FLAGS}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/OpenDBCRuntime.xcconfig"
        "// Generated by the OpenDBC build: the libraries libOpenDBCRuntime.a needs.\n"
        "OPENDBC_RUNTIME_LDFLAGS = ${OPENDBC_IOS_LDFLAGS}\n"
        "// The runtime is built for arm64 only, on devices and in the Simulator.\n"
        "EXCLUDED_ARCHS[sdk=iphonesimulator*] = x86_64\n")
    # Built on request by the ios-runtime presets only, so Xcode builds of the iOS Studio skip it.
    add_custom_target(opendbc_ios_runtime
        COMMAND ${CMAKE_COMMAND}
            -DOPENDBC_RUNTIME_MANIFEST=${OPENDBC_RUNTIME_MANIFEST}
            -DOPENDBC_HOST_BINARY=${OPENDBC_IOS_RUNTIME}
            -DOPENDBC_HOST_MANIFEST=${OPENDBC_IOS_RUNTIME}.opendbc-host.json
            -P ${CMAKE_SOURCE_DIR}/cmake/write_host_manifest.cmake
        DEPENDS "${OPENDBC_IOS_RUNTIME}"
        COMMENT "Binding the OpenDBC runtime fingerprint to the iOS runtime archive"
        VERBATIM)
else()
    if(APPLE)
        set(OPENDBC_RUNNER_HOST_MANIFEST "$<TARGET_BUNDLE_DIR:opendbc-runner>/Contents/Resources/opendbc-host.json")
    else()
        set(OPENDBC_RUNNER_HOST_MANIFEST "$<TARGET_FILE_DIR:opendbc-runner>/opendbc-runner-host.json")
    endif()
    add_custom_command(TARGET opendbc-runner POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DOPENDBC_RUNTIME_MANIFEST=${OPENDBC_RUNTIME_MANIFEST}
            -DOPENDBC_HOST_BINARY=$<TARGET_FILE:opendbc-runner>
            -DOPENDBC_HOST_MANIFEST=${OPENDBC_RUNNER_HOST_MANIFEST}
            -P ${CMAKE_SOURCE_DIR}/cmake/write_host_manifest.cmake
        COMMENT "Binding opendbc-runner to the OpenDBC runtime fingerprint"
        VERBATIM)
endif()
