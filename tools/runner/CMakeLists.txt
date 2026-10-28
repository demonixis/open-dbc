if(ANDROID)
    # SDL's Android activity loads the game from libmain.so.
    add_library(opendbc-runner SHARED main.cpp)
    set_target_properties(opendbc-runner PROPERTIES OUTPUT_NAME main)
    target_link_options(opendbc-runner PRIVATE $<$<CONFIG:Release>:-Wl,--strip-all>)
    # Google Play requires 16 KB page alignment for apps that target Android 15 and ship native code.
    target_link_options(opendbc-runner PRIVATE -Wl,-z,max-page-size=16384)
else()
    add_executable(opendbc-runner MACOSX_BUNDLE main.cpp)
endif()
if(WIN32)
    # Exports replace this icon and version with the game's own.
    set(OPENDBC_RUNNER_ICON "${CMAKE_SOURCE_DIR}/resources/editor.ico")
    set(OPENDBC_RUNNER_VERSION_COMMAS
        "${OpenDBC_VERSION_MAJOR},${OpenDBC_VERSION_MINOR},0,${OPENDBC_BUILD_NUMBER}")
    configure_file(runner.rc.in "${CMAKE_CURRENT_BINARY_DIR}/runner.rc" @ONLY)
    target_sources(opendbc-runner PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/runner.rc")
endif()
target_link_libraries(opendbc-runner PRIVATE dbc_interpreter)
dbc_enable_warnings(opendbc-runner)
opendbc_deploy_resources(opendbc-runner)

if(IOS)
    configure_file(ios/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set(OPENDBC_RUNNER_IOS_SHADERS ${OPENDBC_CANONICAL_SHADERS})
    set_source_files_properties(${OPENDBC_RUNNER_IOS_SHADERS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/shaders")
    target_sources(opendbc-runner PRIVATE ${OPENDBC_RUNNER_IOS_SHADERS})
    set_target_properties(opendbc-runner PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.demonixis.opendbc.runner"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_GENERATE_SCHEME ON
    )
elseif(APPLE)
    set_target_properties(opendbc-runner PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "OpenDBC Runner"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.demonixis.opendbc.runner"
    )
endif()

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
    # A dependent target hashes the final Mach-O; artifact signing happens after every later change.
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        # Xcode leaves EFFECTIVE_PLATFORM_NAME as a build-setting expression in generator-expression
        # paths used by aggregate target scripts, so the script resolves the product directory itself.
        set(OPENDBC_APPLE_HOST_ARGS
            -DOPENDBC_XCODE_HOST=1
            -DOPENDBC_XCODE_PRODUCT_ROOT=${CMAKE_CURRENT_BINARY_DIR}
            -DOPENDBC_XCODE_APP=opendbc-runner)
    else()
        set(OPENDBC_APPLE_HOST_ARGS
            -DOPENDBC_HOST_BINARY=$<TARGET_FILE:opendbc-runner>
            -DOPENDBC_HOST_MANIFEST=$<TARGET_BUNDLE_DIR:opendbc-runner>.opendbc-host.json)
    endif()
    add_custom_target(opendbc_export_host_manifest ALL
        COMMAND ${CMAKE_COMMAND}
            -DOPENDBC_RUNTIME_MANIFEST=${OPENDBC_RUNTIME_MANIFEST}
            ${OPENDBC_APPLE_HOST_ARGS}
            -P ${CMAKE_SOURCE_DIR}/cmake/write_host_manifest.cmake
        DEPENDS opendbc-runner
        COMMENT "Qualifying the OpenDBC iOS export host"
        VERBATIM)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(opendbc_export_host_manifest PROPERTIES XCODE_GENERATE_SCHEME ON)
    endif()
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
