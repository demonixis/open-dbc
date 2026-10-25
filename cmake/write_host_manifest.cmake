if(NOT DEFINED OPENDBC_RUNTIME_MANIFEST OR NOT EXISTS "${OPENDBC_RUNTIME_MANIFEST}")
    message(FATAL_ERROR "OPENDBC_RUNTIME_MANIFEST is missing")
endif()
if(OPENDBC_XCODE_HOST)
    if(NOT DEFINED OPENDBC_XCODE_PRODUCT_ROOT OR "$ENV{CONFIGURATION}" STREQUAL "" OR
       "$ENV{EFFECTIVE_PLATFORM_NAME}" STREQUAL "")
        message(FATAL_ERROR "Xcode host qualification is missing its product root or build settings")
    endif()
    set(OPENDBC_XCODE_PRODUCT_DIR
        "${OPENDBC_XCODE_PRODUCT_ROOT}/$ENV{CONFIGURATION}$ENV{EFFECTIVE_PLATFORM_NAME}")
    set(OPENDBC_HOST_BINARY "${OPENDBC_XCODE_PRODUCT_DIR}/opendbc.app/opendbc")
    set(OPENDBC_HOST_MANIFEST "${OPENDBC_XCODE_PRODUCT_DIR}/opendbc.app.opendbc-host.json")
    set(OPENDBC_APPLE_HOST_BUNDLE "${OPENDBC_XCODE_PRODUCT_DIR}/opendbc.app")
endif()
if(NOT DEFINED OPENDBC_HOST_BINARY OR NOT EXISTS "${OPENDBC_HOST_BINARY}")
    message(FATAL_ERROR "OPENDBC_HOST_BINARY is missing")
endif()
if(NOT DEFINED OPENDBC_HOST_MANIFEST)
    message(FATAL_ERROR "OPENDBC_HOST_MANIFEST is missing")
endif()

if(OPENDBC_XCODE_HOST)
    execute_process(COMMAND /usr/bin/codesign --force --sign - --timestamp=none
        "${OPENDBC_APPLE_HOST_BUNDLE}"
        RESULT_VARIABLE OPENDBC_CODESIGN_STATUS ERROR_VARIABLE OPENDBC_CODESIGN_ERROR)
    if(NOT OPENDBC_CODESIGN_STATUS EQUAL 0)
        message(FATAL_ERROR "Cannot ad-hoc sign Apple export host: ${OPENDBC_CODESIGN_ERROR}")
    endif()
    execute_process(COMMAND /usr/bin/codesign --verify --deep --strict
        "${OPENDBC_APPLE_HOST_BUNDLE}"
        RESULT_VARIABLE OPENDBC_CODESIGN_VERIFY_STATUS ERROR_VARIABLE OPENDBC_CODESIGN_VERIFY_ERROR)
    if(NOT OPENDBC_CODESIGN_VERIFY_STATUS EQUAL 0)
        message(FATAL_ERROR "Apple export host signature is invalid: ${OPENDBC_CODESIGN_VERIFY_ERROR}")
    endif()
endif()

file(SHA256 "${OPENDBC_RUNTIME_MANIFEST}" OPENDBC_RUNTIME_MANIFEST_HASH)
file(SHA256 "${OPENDBC_HOST_BINARY}" OPENDBC_HOST_BINARY_HASH)
set(OPENDBC_NEEDED_JSON "[]")
if(OPENDBC_ANDROID_HOST)
    if(NOT DEFINED OPENDBC_OBJDUMP OR NOT EXISTS "${OPENDBC_OBJDUMP}")
        message(FATAL_ERROR "Android host qualification requires CMAKE_OBJDUMP")
    endif()
    execute_process(COMMAND "${OPENDBC_OBJDUMP}" -p "${OPENDBC_HOST_BINARY}"
        RESULT_VARIABLE OPENDBC_OBJDUMP_STATUS OUTPUT_VARIABLE OPENDBC_OBJDUMP_OUTPUT)
    if(NOT OPENDBC_OBJDUMP_STATUS EQUAL 0)
        message(FATAL_ERROR "Cannot inspect Android host dynamic dependencies")
    endif()
    string(REGEX MATCHALL "NEEDED[ \t]+[A-Za-z0-9_.+-]+" OPENDBC_NEEDED_LINES
        "${OPENDBC_OBJDUMP_OUTPUT}")
    set(OPENDBC_NEEDED_JSON "[")
    set(OPENDBC_NEEDED_SEPARATOR "")
    foreach(OPENDBC_NEEDED_LINE IN LISTS OPENDBC_NEEDED_LINES)
        string(REGEX REPLACE "^NEEDED[ \t]+" "" OPENDBC_NEEDED "${OPENDBC_NEEDED_LINE}")
        if(NOT OPENDBC_NEEDED MATCHES
           "^(libandroid|libc|libdl|liblog|libm|libOpenSLES|libz)\\.so$")
            message(FATAL_ERROR "Forbidden Android host dependency: ${OPENDBC_NEEDED}")
        endif()
        string(APPEND OPENDBC_NEEDED_JSON "${OPENDBC_NEEDED_SEPARATOR}\"${OPENDBC_NEEDED}\"")
        set(OPENDBC_NEEDED_SEPARATOR ", ")
    endforeach()
    string(APPEND OPENDBC_NEEDED_JSON "]")
endif()
file(WRITE "${OPENDBC_HOST_MANIFEST}"
    "{\n"
    "  \"format\": 1,\n"
    "  \"runtime_manifest_sha256\": \"${OPENDBC_RUNTIME_MANIFEST_HASH}\",\n"
    "  \"binary_sha256\": \"${OPENDBC_HOST_BINARY_HASH}\",\n"
    "  \"needed\": ${OPENDBC_NEEDED_JSON}\n"
    "}\n")
