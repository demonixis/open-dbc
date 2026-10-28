#include "engine/runtime_compatibility.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/memblock_system.hpp"
#include "interpreter/engine_builtin_context.hpp"
#include "interpreter/engine_builtin_registry.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using dbc::interpreter::detail::BuiltinContext;
    using dbc::interpreter::detail::BuiltinValue;
    using dbc::language::SymbolId;

    BuiltinValue Text(std::string Value)
    {
        return BuiltinValue::FromString(std::move(Value));
    }

    BuiltinValue Real(double Value)
    {
        return BuiltinValue::FromNumber(Value);
    }

    // A BASIC integer, as the VM passes it.
    BuiltinValue Integer(double Value)
    {
        BuiltinValue Result = BuiltinValue::FromNumber(Value);
        Result.IsInteger = true;
        return Result;
    }

    class Program
    {
      public:
        Program()
        {
            REQUIRE(Kernel_.Initialize("classic-dll", 64, 64));
            Kernel_.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
            Context_.Kernel = &Kernel_;
            Context_.Profile = dbc::language::LanguageProfile::Dbc;
            Context_.AssetRoot = TEST_PLUGIN_DIR;
        }

        bool Command(SymbolId Symbol, std::vector<BuiltinValue> Arguments)
        {
            return dbc::interpreter::detail::InvokeCommand(Context_, Symbol, Arguments, Error);
        }

        void Require(SymbolId Symbol, std::vector<BuiltinValue> Arguments)
        {
            REQUIRE_MESSAGE(Command(Symbol, std::move(Arguments)), Error);
        }

        bool Function(SymbolId Symbol, std::vector<BuiltinValue> Arguments, double& Output)
        {
            BuiltinValue Result;
            if (!dbc::interpreter::detail::InvokeFunction(Context_, Symbol, Arguments, Result, Error))
                return false;
            Output = Result.AsNumber();
            return true;
        }

        double Value(SymbolId Symbol, std::vector<BuiltinValue> Arguments)
        {
            double Output = 0.0;
            REQUIRE_MESSAGE(Function(Symbol, std::move(Arguments), Output), Error);
            return Output;
        }

        dbc::EngineKernel& Kernel() { return Kernel_; }
        BuiltinContext& Context() { return Context_; }

        std::string Error;

      private:
        dbc::EngineKernel Kernel_;
        BuiltinContext Context_;
    };
} // namespace

TEST_CASE("Classic DLL commands load OpenDBC plugins and call them with typed values")
{
    Program Basic;
    CHECK_FALSE(Basic.Command(SymbolId::Command_LoadDll, {Text("missing.dll"), Integer(1)}));
    CHECK(Basic.Error.find("LOAD DLL cannot find") != std::string::npos);
    CHECK_FALSE(Basic.Command(SymbolId::Command_LoadDll, {Text("test_not_plugin.dll"), Integer(2)}));
    CHECK(Basic.Error.find("is not an OpenDBC plugin of ABI version 1") != std::string::npos);
    CHECK(Basic.Value(SymbolId::Function_DllExist, {Integer(2)}) == 0.0);
    CHECK_FALSE(Basic.Command(SymbolId::Command_LoadDll, {Text("test_dll_plugin.dll"), Integer(0)}));
    CHECK(Basic.Error == "DLL number must be between 1 and 256");

    // A Windows name also finds the plugin built for this platform.
    Basic.Require(SymbolId::Command_LoadDll, {Text("test_dll_plugin.dll"), Integer(1)});
    CHECK_FALSE(Basic.Command(SymbolId::Command_LoadDll, {Text("test_dll_plugin.dll"), Integer(1)}));
    CHECK(Basic.Error == "LOAD DLL cannot replace existing DLL 1");
    CHECK(Basic.Value(SymbolId::Function_DllExist, {Integer(1)}) == 1.0);
    CHECK(Basic.Value(SymbolId::Function_DllCallExist, {Integer(1), Text("add_integers")}) == 1.0);
    CHECK(Basic.Value(SymbolId::Function_DllCallExist, {Integer(1), Text("missing")}) == 0.0);
    CHECK(Basic.Value(SymbolId::Function_DllCallExist, {Integer(3), Text("add_integers")}) == 0.0);

    CHECK(Basic.Value(SymbolId::Function_CallDll, {Integer(1), Text("add_integers"), Integer(40), Integer(2)}) ==
          42.0);
    double Unused = 0.0;
    CHECK_FALSE(Basic.Function(SymbolId::Function_CallDll,
                               {Integer(1), Text("add_integers"), Real(40.0), Integer(2)}, Unused));
    CHECK(Basic.Error == "CALL DLL 'add_integers' failed: add_integers takes two integers");
    CHECK(Basic.Value(SymbolId::Function_CallDll, {Integer(1), Text("half"), Real(5.0)}) == 2.5);
    Basic.Require(SymbolId::Command_CallDll, {Integer(1), Text("greet"), Text("Bill")});
    CHECK_FALSE(Basic.Function(SymbolId::Function_CallDll, {Integer(1), Text("greet"), Text("Bill")}, Unused));
    CHECK(Basic.Error == "CALL DLL() returns numbers; 'greet' returned a string");
    CHECK_FALSE(Basic.Command(SymbolId::Command_CallDll, {Integer(1), Text("fail_plain")}));
    CHECK(Basic.Error == "CALL DLL 'fail_plain' failed with status 7");
    CHECK_FALSE(Basic.Command(SymbolId::Command_CallDll, {Integer(1), Text("missing")}));
    CHECK(Basic.Error == "CALL DLL cannot find function 'missing' in DLL 1");
    CHECK_FALSE(Basic.Command(SymbolId::Command_CallDll, {Integer(4), Text("half")}));
    CHECK(Basic.Error == "CALL DLL requires existing DLL 4");

    // Memblock addresses reach plugins through the host, bounds checked.
    std::string Error;
    REQUIRE(Basic.Kernel().Memblocks().MakeMemblock(1, 32, &Error));
    REQUIRE(Basic.Kernel().Memblocks().MakeMemblock(200, 4, &Error));
    const double First = Basic.Value(SymbolId::Function_GetMemblockPtr, {Integer(1)});
    CHECK(First == 16777216.0);
    Basic.Require(SymbolId::Command_CallDll,
                  {Integer(1), Text("fill_memblock"), Integer(First + 8), Integer(4), Integer(7)});
    const auto Byte = [&](int Memblock, int Offset)
    {
        int Value = 0;
        REQUIRE(Basic.Kernel().Memblocks().ReadMemblockByte(Value, Memblock, Offset, &Error));
        return Value;
    };
    CHECK(Byte(1, 7) == 0);
    CHECK(Byte(1, 8) == 7);
    CHECK(Byte(1, 11) == 7);
    CHECK(Byte(1, 12) == 0);
    CHECK_FALSE(Basic.Command(SymbolId::Command_CallDll,
                              {Integer(1), Text("fill_memblock"), Integer(First + 30), Integer(4), Integer(7)}));
    CHECK(Basic.Error == "CALL DLL 'fill_memblock' failed: the address is outside every memblock");
    // Addresses above 2^31 keep their bits on the way to the plugin.
    const double High = Basic.Value(SymbolId::Function_GetMemblockPtr, {Integer(200)});
    CHECK(High == 200.0 * 16777216.0);
    Basic.Require(SymbolId::Command_CallDll, {Integer(1), Text("fill_memblock"), Integer(High), Integer(4), Integer(9)});
    CHECK(Byte(200, 3) == 9);
    CHECK_FALSE(Basic.Function(SymbolId::Function_GetMemblockPtr, {Integer(5)}, Unused));
    CHECK(Basic.Error == "Memblock 5 does not exist");

    Basic.Require(SymbolId::Command_DeleteDll, {Integer(1)});
    CHECK(Basic.Value(SymbolId::Function_DllExist, {Integer(1)}) == 0.0);
    CHECK_FALSE(Basic.Command(SymbolId::Command_DeleteDll, {Integer(1)}));
    CHECK(Basic.Error == "DELETE DLL requires existing DLL 1");

    Basic.Context().Profile = dbc::language::LanguageProfile::DbPro;
    CHECK_FALSE(Basic.Command(SymbolId::Command_LoadDll, {Integer(1), Integer(1)}));
}

TEST_CASE("LOCK BACKBUFFER lends the screen to plugins and UNLOCK BACKBUFFER draws it back")
{
    Program Basic;
    double Unused = 0.0;
    CHECK_FALSE(Basic.Function(SymbolId::Function_GetBackbufferPtr, {}, Unused));
    CHECK(Basic.Error == "GET BACKBUFFER PTR requires LOCK BACKBUFFER");
    CHECK_FALSE(Basic.Command(SymbolId::Command_UnlockBackbuffer, {}));
    CHECK(Basic.Error == "UNLOCK BACKBUFFER requires a locked backbuffer");

    // A 4x2 screen of grey pixels, drawn while bitmap 1 is current.
    dbc::MemblockSystem& Memblocks = Basic.Kernel().Memblocks();
    std::string Error;
    std::vector<std::uint8_t> Screen(12u + 4u * 2u * 4u, 0x40u);
    const std::uint32_t Header[3] = {4u, 2u, 32u};
    std::memcpy(Screen.data(), Header, sizeof(Header));
    REQUIRE(Memblocks.DecodeBitmap(0, Screen, "test", &Error));
    REQUIRE(Memblocks.DecodeBitmap(1, Screen, "test", &Error));
    REQUIRE(Basic.Kernel().ImageStoreRef().CurrentBitmap() == 1);

    Basic.Require(SymbolId::Command_LoadDll, {Text("test_dll_plugin.dll"), Integer(1)});
    Basic.Require(SymbolId::Command_LockBackbuffer, {});
    CHECK_FALSE(Basic.Command(SymbolId::Command_LockBackbuffer, {}));
    CHECK(Basic.Error == "LOCK BACKBUFFER requires an unlocked backbuffer");
    const double Pointer = Basic.Value(SymbolId::Function_GetBackbufferPtr, {});
    CHECK(Pointer == 4096.0);
    CHECK(Basic.Value(SymbolId::Function_GetBackbufferWidth, {}) == 4.0);
    CHECK(Basic.Value(SymbolId::Function_GetBackbufferHeight, {}) == 2.0);
    CHECK(Basic.Value(SymbolId::Function_GetBackbufferDepth, {}) == 32.0);
    const double Pitch = Basic.Value(SymbolId::Function_GetBackbufferPitch, {});
    CHECK(Pitch == 16.0);

    // The plugin whitens pixel (1, 1); writes outside the 32 locked bytes are refused.
    Basic.Require(SymbolId::Command_CallDll,
                  {Integer(1), Text("fill_memblock"), Integer(Pointer + Pitch + 4.0), Integer(4), Integer(255)});
    CHECK_FALSE(Basic.Command(SymbolId::Command_CallDll,
                              {Integer(1), Text("fill_memblock"), Integer(Pointer + 30.0), Integer(4), Integer(0)}));
    CHECK_FALSE(Basic.Command(SymbolId::Command_CallDll,
                              {Integer(1), Text("fill_memblock"), Integer(Pointer - 1.0), Integer(1), Integer(0)}));

    Basic.Require(SymbolId::Command_UnlockBackbuffer, {});
    CHECK(Basic.Kernel().ImageStoreRef().CurrentBitmap() == 1);
    CHECK(Memblocks.ResolveAddress(static_cast<std::uint32_t>(Pointer), 1) == nullptr);
    CHECK_FALSE(Basic.Function(SymbolId::Function_GetBackbufferPtr, {}, Unused));
    std::vector<std::uint8_t> Drawn;
    REQUIRE(Memblocks.EncodeBitmap(Drawn, 0, "test", &Error));
    REQUIRE(Drawn.size() == Screen.size());
    const auto Pixel = [&](int X, int Y) { return Drawn.data() + 12 + Y * 16 + X * 4; };
    CHECK(Pixel(1, 1)[0] == 255);
    CHECK(Pixel(1, 1)[2] == 255);
    CHECK(Pixel(0, 1)[0] == 0x40);
    CHECK(Pixel(1, 0)[1] == 0x40);
}
