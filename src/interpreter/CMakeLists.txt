add_library(dbc_interpreter STATIC
  command_line.cpp
  command_line.hpp
  command_line_detail.hpp
  command_line_run.cpp
  embedded_artifact.cpp
  embedded_artifact.hpp
  execution_service.cpp
  execution_service.hpp
  execution_diagnostics.hpp
  frame_meter.cpp
  frame_meter.hpp
  program_build.cpp
  program_image.cpp
  program_image.hpp
  project_resolver.cpp
  project_resolver.hpp
  registry_file.cpp
  registry_file.hpp
  runtime_startup.cpp
  runtime_startup.hpp
  scene_capture.cpp
  scene_capture.hpp
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
# Only this source includes the fingerprint, so a new fingerprint recompiles one file.
add_dependencies(dbc_interpreter opendbc_runtime_fingerprint)
target_include_directories(dbc_interpreter PRIVATE "${OPENDBC_RUNTIME_FINGERPRINT_DIR}")
set_source_files_properties(program_image.cpp PROPERTIES OBJECT_DEPENDS "${OPENDBC_RUNTIME_FINGERPRINT_HEADER}")
set_source_files_properties(command_line_run.cpp PROPERTIES
    COMPILE_DEFINITIONS "OPENDBC_VERSION_STRING=\"${OPENDBC_VERSION}\"")
