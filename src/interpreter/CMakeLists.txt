add_library(dbc_interpreter STATIC
  command_line.cpp
  command_line.hpp
  execution_service.cpp
  execution_service.hpp
  program_image.cpp
  program_image.hpp
  project_resolver.cpp
  project_resolver.hpp
  runtime_startup.cpp
  runtime_startup.hpp
  engine_builtin_registry.cpp
  engine_builtin_handlers.inc
  engine_builtin_host.cpp
  engine_builtin_host.hpp
  engine_builtin_context.hpp
  engine_builtin_registry.hpp
)

target_include_directories(dbc_interpreter
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dbc_interpreter PUBLIC dbc_engine unordered_dense::unordered_dense dbc_language opendbc_vm
  nlohmann_json::nlohmann_json)
dbc_enable_warnings(dbc_interpreter)
# Only this source sees the fingerprint, so a new fingerprint recompiles one file.
set_source_files_properties(program_image.cpp PROPERTIES
    COMPILE_DEFINITIONS "OPENDBC_RUNTIME_FINGERPRINT=\"${OPENDBC_RUNTIME_FINGERPRINT}\"")
