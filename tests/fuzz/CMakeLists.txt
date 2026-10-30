# The driver implements the edge callbacks, so it must not call them from its own code.
add_executable(fuzz_inputs fuzz_inputs.cpp)
target_link_libraries(fuzz_inputs PRIVATE dbc_interpreter)
dbc_enable_warnings(fuzz_inputs)
target_compile_options(fuzz_inputs PRIVATE -fno-sanitize-coverage=trace-pc-guard)
