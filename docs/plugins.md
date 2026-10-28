# Native plugins (Classic DLLs)

`dbc` only; DBPro DLL commands stay unsupported. DarkBASIC Classic called any
Windows DLL. Those DLLs are 32-bit x86 code, which no 64-bit OpenDBC process can
load. `LOAD DLL` therefore loads native plugins written for OpenDBC: a `.dll`,
`.dylib` or `.so` built against [`sdk/opendbc_plugin.h`](../sdk/opendbc_plugin.h).
A plugin runs with the program's full rights; load only plugins you trust.

## Writing a plugin

```c
#include <opendbc_plugin.h>

OPENDBC_PLUGIN_DECLARE()

/* value = CALL DLL(1, "add", 40, 2) */
OPENDBC_PLUGIN_API int32_t add(const OpenDbcValue* arguments, int32_t count, OpenDbcValue* result,
                               const OpenDbcHost* host)
{
    if (count != 2 || arguments[0].type != OPENDBC_VALUE_INTEGER || arguments[1].type != OPENDBC_VALUE_INTEGER)
    {
        host->fail(host->context, "add takes two integers");
        return 1;
    }
    result->type = OPENDBC_VALUE_INTEGER;
    result->integer = arguments[0].integer + arguments[1].integer;
    return 0;
}
```

`OPENDBC_PLUGIN_DECLARE()` exports the ABI version that `LOAD DLL` checks; other
libraries are refused. Every function `CALL DLL` reaches has the
`OpenDbcFunction` type. The plugin is C, or C++ that lets no exception escape.
Build it as a shared library for each platform that runs the program.

## Commands

| Command | Behavior |
| --- | --- |
| `LOAD DLL file$, n` | Loads the plugin, 1 ≤ n ≤ 256, from the asset root. A name ending in `.dll` also finds `name.dylib`/`libname.dylib` on macOS and `name.so`/`libname.so` on Linux. A missing file, a non-plugin or a used number is an error. |
| `DELETE DLL n` | Unloads it; an error when n is not loaded. |
| `DLL EXIST(n)`, `DLL CALL EXIST(n, name$)` | 1 when the plugin, or its exported function, exists. |
| `CALL DLL n, name$[, up to 9 values]` | Calls the function and ignores its result. |
| `CALL DLL(n, name$[, …])` | Returns the function's integer or real; a string result is an error. |

Arguments keep their BASIC types: integer variables and literals arrive as
`OPENDBC_VALUE_INTEGER` (32 bits, as in Classic), reals as `OPENDBC_VALUE_REAL`
and strings as UTF-8. A function that returns non-zero fails the command, with
the message passed to `host->fail` when there is one.

## Memblock addresses

`GET MEMBLOCK PTR(n)` returns `n × 16777216`: the memblock number in the top
byte, offset 0 below it. Programs add offsets as they did to Classic pointers,
and no native address ever reaches BASIC. A plugin turns such an address into
memory with `host->resolve(host->context, address, size)`. The call returns NULL
unless all `size` bytes lie inside one memblock, and the memory stays valid until
the function returns. Only the first 16 MB of a memblock can be addressed.

## Backbuffer

`LOCK BACKBUFFER` copies the screen, as `MAKE MEMBLOCK FROM BITMAP 0` reads it,
into a buffer that `GET BACKBUFFER PTR()` addresses at 4096, below every
memblock. While it is locked, `GET BACKBUFFER WIDTH/HEIGHT/DEPTH/PITCH()`
describe that buffer: rows of `PITCH` bytes, 32-bit pixels as little-endian
`ARGB` dwords or 16-bit `RGB565` words. A plugin resolves addresses into it as
into a memblock. `UNLOCK BACKBUFFER` draws the buffer back as bitmap 0, over
anything drawn while it was locked, and keeps the current bitmap. Locking twice,
unlocking an unlocked buffer and `GET BACKBUFFER PTR()` while unlocked are
errors, as is a screen whose pixels exceed 16 MB.
