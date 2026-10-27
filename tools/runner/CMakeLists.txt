add_executable(opendbc-runner MACOSX_BUNDLE main.cpp)
set_target_properties(opendbc-runner PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "OpenDBC Runner"
    MACOSX_BUNDLE_GUI_IDENTIFIER "net.demonixis.opendbc.runner"
)
target_link_libraries(opendbc-runner PRIVATE dbc_interpreter)
dbc_enable_warnings(opendbc-runner)
opendbc_deploy_resources(opendbc-runner)

# The Studio exports games from this runner; the manifest ties the binary to the runtime sources.
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
