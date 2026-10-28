# Writes the Studio's built-in snippets into a C++ include, so the Studio needs no
# snippet files beside the executable on any platform.
# Called with: -DOUTPUT=<file.inc> -DSOURCE_DIR=<dir> -DFILES=<name.dba;name.dba>
set(OPENDBC_EMBED "// Generated from tools/studio/snippets by embed_snippets.cmake; do not edit.\n")
string(APPEND OPENDBC_EMBED "inline constexpr std::pair<std::string_view, std::string_view> KBuiltinSnippetFiles[] = {\n")
foreach(OPENDBC_FILE IN LISTS FILES)
    file(READ "${SOURCE_DIR}/${OPENDBC_FILE}" OPENDBC_HEX HEX)
    file(SIZE "${SOURCE_DIR}/${OPENDBC_FILE}" OPENDBC_SIZE)
    # Every byte is escaped, so no escape can swallow the character after it.
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" OPENDBC_BYTES "${OPENDBC_HEX}")
    string(APPEND OPENDBC_EMBED "    {\"${OPENDBC_FILE}\", std::string_view(\"${OPENDBC_BYTES}\", ${OPENDBC_SIZE})},\n")
endforeach()
string(APPEND OPENDBC_EMBED "};\n")
file(WRITE "${OUTPUT}.tmp" "${OPENDBC_EMBED}")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
