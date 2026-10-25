add_library(opendbc_vm STATIC
    bytecode.cpp
    compiler.cpp
    execution_session.cpp
    value.cpp
    verifier.cpp
)

target_include_directories(opendbc_vm PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(opendbc_vm PUBLIC dbc_language)

dbc_enable_warnings(opendbc_vm)
