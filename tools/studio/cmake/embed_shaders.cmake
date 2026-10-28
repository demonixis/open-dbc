# Writes the Studio's canonical shader files into a C++ include as byte arrays, so the
# Scene view needs no shader files beside the executable.
# Called with: -DOUTPUT=<file.inc> -DSOURCE_DIR=<dir> -DSTEMS=<stem;stem>
set(OPENDBC_EMBED "// Generated from tools/studio/shaders by embed_shaders.cmake; do not edit.\n")
foreach(OPENDBC_STEM IN LISTS STEMS)
    foreach(OPENDBC_STAGE IN ITEMS vert frag)
        foreach(OPENDBC_FORMAT IN ITEMS spv msl)
            file(READ "${SOURCE_DIR}/${OPENDBC_STEM}.${OPENDBC_STAGE}.${OPENDBC_FORMAT}" OPENDBC_HEX HEX)
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," OPENDBC_BYTES "${OPENDBC_HEX}")
            string(MAKE_C_IDENTIFIER "${OPENDBC_STEM}_${OPENDBC_STAGE}_${OPENDBC_FORMAT}" OPENDBC_NAME)
            string(APPEND OPENDBC_EMBED
                   "inline constexpr unsigned char K_${OPENDBC_NAME}[] = {${OPENDBC_BYTES}};\n")
        endforeach()
    endforeach()
endforeach()
file(WRITE "${OUTPUT}.tmp" "${OPENDBC_EMBED}")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
