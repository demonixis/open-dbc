/* A plugin for the CALL DLL tests. */
#include <opendbc_plugin.h>

#include <stdio.h>
#include <string.h>

OPENDBC_PLUGIN_DECLARE()

OPENDBC_PLUGIN_API int32_t add_integers(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                                        const OpenDbcHost* host)
{
    if (count != 2 || arguments[0].type != OPENDBC_VALUE_INTEGER || arguments[1].type != OPENDBC_VALUE_INTEGER)
    {
        host->fail(host->context, "add_integers takes two integers");
        return 1;
    }
    result->type = OPENDBC_VALUE_INTEGER;
    result->integer = arguments[0].integer + arguments[1].integer;
    return 0;
}

OPENDBC_PLUGIN_API int32_t half(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                                const OpenDbcHost* host)
{
    if (count != 1 || arguments[0].type != OPENDBC_VALUE_REAL)
    {
        host->fail(host->context, "half takes one real");
        return 1;
    }
    result->type = OPENDBC_VALUE_REAL;
    result->real = arguments[0].real / 2.0;
    return 0;
}

OPENDBC_PLUGIN_API int32_t greet(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                                 const OpenDbcHost* host)
{
    static char Buffer[256];
    if (count != 1 || arguments[0].type != OPENDBC_VALUE_STRING)
    {
        host->fail(host->context, "greet takes one string");
        return 1;
    }
    snprintf(Buffer, sizeof(Buffer), "hello %s", arguments[0].string);
    result->type = OPENDBC_VALUE_STRING;
    result->string = Buffer;
    return 0;
}

/* fill_memblock(address, size, value) writes value into size bytes of a memblock. */
OPENDBC_PLUGIN_API int32_t fill_memblock(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                                         const OpenDbcHost* host)
{
    (void)result;
    if (count != 3)
        return 1;
    void* Memory = host->resolve(host->context, (uint32_t)arguments[0].integer, (uint32_t)arguments[1].integer);
    if (Memory == NULL)
    {
        host->fail(host->context, "the address is outside every memblock");
        return 1;
    }
    memset(Memory, arguments[2].integer, (size_t)arguments[1].integer);
    return 0;
}

OPENDBC_PLUGIN_API int32_t fail_plain(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                                      const OpenDbcHost* host)
{
    (void)arguments;
    (void)count;
    (void)result;
    (void)host;
    return 7;
}
