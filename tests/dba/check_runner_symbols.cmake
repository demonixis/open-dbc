# CTest script: opendbc-runner must not link the BASIC frontend. Linking it would mean a runtime
# translation unit references lexer, parser or binder code again.
# Called with: -DNM=... -DRUNNER=...

execute_process(COMMAND ${NM} ${RUNNER} RESULT_VARIABLE exit_code OUTPUT_VARIABLE symbols ERROR_VARIABLE stderr)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "nm failed (exit ${exit_code}):\n${stderr}")
endif()
foreach(marker IN ITEMS LexAndParse ParseDbProProject DetectDialect 2vm5Lower SDL_ShaderCross ImGui)
    string(FIND "${symbols}" "${marker}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "opendbc-runner links Studio or frontend code (${marker})")
    endif()
endforeach()
message(STATUS "opendbc-runner links no frontend, Studio or shader compiler code")
