/* OpenDBC native plugins, loaded by the Classic LOAD DLL command and called with CALL DLL.
 * See docs/plugins.md. Code is part of OpenDBC (MIT). */
#ifndef OPENDBC_PLUGIN_H
#define OPENDBC_PLUGIN_H

#include <stdint.h>

#define OPENDBC_PLUGIN_ABI_VERSION 1u

#if defined(_WIN32)
#define OPENDBC_PLUGIN_EXPORT __declspec(dllexport)
#else
#define OPENDBC_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Marks a function CALL DLL can find by name. */
#ifdef __cplusplus
#define OPENDBC_PLUGIN_API extern "C" OPENDBC_PLUGIN_EXPORT
#else
#define OPENDBC_PLUGIN_API OPENDBC_PLUGIN_EXPORT
#endif

/* Every plugin uses this once, so LOAD DLL can tell it from other libraries. */
#define OPENDBC_PLUGIN_DECLARE()                                                                                       \
    OPENDBC_PLUGIN_API uint32_t opendbc_plugin_abi_version(void)                                                       \
    {                                                                                                                  \
        return OPENDBC_PLUGIN_ABI_VERSION;                                                                             \
    }

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OpenDbcValueType
{
    OPENDBC_VALUE_NONE = 0,
    OPENDBC_VALUE_INTEGER = 1,
    OPENDBC_VALUE_REAL = 2,
    OPENDBC_VALUE_STRING = 3
} OpenDbcValueType;

/* A BASIC value. Strings are UTF-8 and end with a zero byte. An argument string lives until the function
 * returns; a result string must stay valid until then too, for example in a static buffer. */
typedef struct OpenDbcValue
{
    int32_t type;
    int32_t integer;
    double real;
    const char* string;
} OpenDbcValue;

typedef struct OpenDbcHost
{
    uint32_t abi_version;
    void* context;
    /* Turns an address from GET MEMBLOCK PTR(), plus an offset, into memory the plugin may read and write.
     * Returns NULL unless all size bytes lie in one memblock. The memory stays valid until the function
     * returns. */
    void* (*resolve)(void* context, uint32_t address, uint32_t size);
    /* Reports why the function failed; the message is copied. Return a non-zero status afterwards. */
    void (*fail)(void* context, const char* message);
} OpenDbcHost;

/* Every exported function has this type. It returns 0 on success and sets result, or leaves it at
 * OPENDBC_VALUE_NONE when it returns nothing. Integers arrive as 32-bit values, as in DarkBASIC Classic. */
typedef int32_t (*OpenDbcFunction)(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                                   const OpenDbcHost* host);

#ifdef __cplusplus
}
#endif

#endif
