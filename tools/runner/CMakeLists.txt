add_executable(opendbc-runner MACOSX_BUNDLE main.cpp)
set_target_properties(opendbc-runner PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "OpenDBC Runner"
    MACOSX_BUNDLE_GUI_IDENTIFIER "net.demonixis.opendbc.runner"
)
target_link_libraries(opendbc-runner PRIVATE dbc_interpreter)
dbc_enable_warnings(opendbc-runner)
opendbc_deploy_resources(opendbc-runner)
