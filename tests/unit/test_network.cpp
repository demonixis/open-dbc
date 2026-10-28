#include "engine/runtime_compatibility.hpp"
#include "engine/systems/audio_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/memblock_system.hpp"
#include "engine/systems/network_system.hpp"
#include "engine/systems/object_manager.hpp"
#include "interpreter/engine_builtin_context.hpp"
#include "interpreter/engine_builtin_registry.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using dbc::interpreter::detail::BuiltinContext;
    using dbc::interpreter::detail::BuiltinValue;
    using dbc::language::SymbolId;

    // A game port of its own keeps parallel test runs and other applications apart.
    std::string LoopbackAddress()
    {
        static const int Port = std::random_device{}() % 20000 + 40000;
        return "127.0.0.1:" + std::to_string(Port);
    }

    BuiltinValue Text(std::string Value)
    {
        return BuiltinValue::FromString(std::move(Value));
    }

    BuiltinValue Number(double Value)
    {
        return BuiltinValue::FromNumber(Value);
    }

    // One application taking part in a net game.
    class Application
    {
      public:
        explicit Application(const char* Title)
        {
            REQUIRE(Kernel_.Initialize(Title, 64, 64));
            Kernel_.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
            Context_.Kernel = &Kernel_;
            Context_.Profile = dbc::language::LanguageProfile::Dbc;
        }

        bool Command(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return dbc::interpreter::detail::InvokeCommand(Context_, Symbol, Arguments, Error);
        }

        void Require(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            REQUIRE_MESSAGE(Command(Symbol, std::move(Arguments)), Error);
        }

        bool Function(SymbolId Symbol, BuiltinValue& Result, std::vector<BuiltinValue> Arguments = {})
        {
            return dbc::interpreter::detail::InvokeFunction(Context_, Symbol, Arguments, Result, Error);
        }

        double Value(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            BuiltinValue Result;
            REQUIRE_MESSAGE(Function(Symbol, Result, std::move(Arguments)), Error);
            return Result.AsNumber();
        }

        std::string String(SymbolId Symbol)
        {
            BuiltinValue Result;
            REQUIRE_MESSAGE(Function(Symbol, Result), Error);
            return Result.AsString();
        }

        const std::vector<dbc::interpreter::detail::ChecklistItem>& Checklist(SymbolId Symbol)
        {
            Require(Symbol);
            return Context_.Checklist;
        }

        // Takes the next message, waiting for the network thread to deliver it.
        bool NextMessage()
        {
            const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;)
            {
                Require(SymbolId::Command_GetNetMessage);
                if (Value(SymbolId::Function_NetMessageExists) == 1.0)
                    return true;
                if (std::chrono::steady_clock::now() > Deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        double WaitForValue(SymbolId Symbol)
        {
            const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;)
            {
                const double Result = Value(Symbol);
                if (Result != 0.0 || std::chrono::steady_clock::now() > Deadline)
                    return Result;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        dbc::EngineKernel& Kernel() { return Kernel_; }

        std::string Error;

      private:
        dbc::EngineKernel Kernel_;
        BuiltinContext Context_;
    };
} // namespace

TEST_CASE("Classic net games connect, list players and exchange typed messages over loopback")
{
    Application Host("classic-net-host");
    Application Joiner("classic-net-joiner");
    Application Late("classic-net-late");

    const auto& Connections = Host.Checklist(SymbolId::Command_PerformChecklistForNetConnections);
    REQUIRE(Connections.size() == 1);
    CHECK(Connections.front().Name == dbc::NetworkSystem::KConnectionName);
    CHECK_FALSE(Host.Command(SymbolId::Command_SetNetConnection, {Number(2)}));
    CHECK(Host.Error == "Net connection 2 does not exist; connection 1 is TCP/IP");
    CHECK_FALSE(Host.Command(SymbolId::Command_SetNetConnection, {Number(1), Text("127.0.0.1:port")}));
    CHECK(Host.Error.find("must be a host, host:port or :port") != std::string::npos);
    Host.Require(SymbolId::Command_SetNetConnection, {Number(1), Text(LoopbackAddress())});
    Joiner.Require(SymbolId::Command_SetNetConnection, {Number(1), Text(LoopbackAddress())});
    Late.Require(SymbolId::Command_SetNetConnection, {Number(1), Text(LoopbackAddress())});

    CHECK_FALSE(Host.Command(SymbolId::Command_CreateNetGame, {Text("arena"), Text("Fred"), Number(1)}));
    CHECK(Host.Error == "CREATE NET GAME number of players must be between 2 and 256");
    CHECK_FALSE(Host.Command(SymbolId::Command_CreateNetGame, {Text("arena"), Text("Fred"), Number(4), Number(3)}));
    CHECK(Host.Value(SymbolId::Function_NetGameExists) == 0.0);
    Host.Require(SymbolId::Command_CreateNetGame, {Text("arena"), Text("Fred"), Number(3)});
    CHECK(Host.Value(SymbolId::Function_NetGameExists) == 1.0);
    CHECK_FALSE(Host.Command(SymbolId::Command_CreateNetGame, {Text("again"), Text("Fred"), Number(4), Number(2)}));
    CHECK(Host.Error == "CREATE NET GAME requires FREE NET GAME before another game");

    const auto& Sessions = Joiner.Checklist(SymbolId::Command_PerformChecklistForNetSessions);
    REQUIRE(Sessions.size() == 1);
    CHECK(Sessions.front().Name == "arena");
    Joiner.Require(SymbolId::Command_JoinNetGame, {Number(1), Text("Bill")});
    REQUIRE(Joiner.Value(SymbolId::Function_NetGameExists) == 1.0);

    const auto& Seen = Joiner.Checklist(SymbolId::Command_PerformChecklistForNetPlayers);
    REQUIRE(Seen.size() == 2);
    CHECK(Seen[0].Name == "Fred");
    CHECK(Seen[0].A == 1);
    CHECK(Seen[0].B == 1);
    CHECK(Seen[0].C == 0);
    CHECK(Seen[0].D == 1);
    CHECK(Seen[1].Name == "Bill");
    CHECK(Seen[1].A == 2);
    CHECK(Seen[1].C == 1);
    CHECK(Seen[1].D == 0);
    CHECK(Host.WaitForValue(SymbolId::Function_NetPlayerCreated) == 2.0);
    CHECK(Host.Value(SymbolId::Function_NetPlayerCreated) == 0.0);
    CHECK(Host.Checklist(SymbolId::Command_PerformChecklistForNetPlayers).size() == 2);

    // Classic loops read the current message before the first GET NET MESSAGE: it reads as nothing.
    CHECK(Host.Value(SymbolId::Function_NetMessageExists) == 0.0);
    CHECK(Host.Value(SymbolId::Function_NetMessageInteger) == 0.0);
    CHECK(Host.Value(SymbolId::Function_NetMessageType) == 0.0);
    CHECK(Host.Value(SymbolId::Function_NetMessagePlayerFrom) == 0.0);

    Joiner.Require(SymbolId::Command_SendNetMessageInteger, {Number(0), Number(42)});
    Joiner.Require(SymbolId::Command_SendNetMessageString, {Number(1), Text("hello host")});
    REQUIRE(Host.NextMessage());
    CHECK(Host.Value(SymbolId::Function_NetMessageType) == 1.0);
    CHECK(Host.Value(SymbolId::Function_NetMessageInteger) == 42.0);
    CHECK(Host.Value(SymbolId::Function_NetMessagePlayerFrom) == 2.0);
    CHECK(Host.Value(SymbolId::Function_NetMessagePlayerTo) == 1.0);
    BuiltinValue Unused;
    CHECK_FALSE(Host.Function(SymbolId::Function_NetMessageFloat, Unused));
    CHECK(Host.Error == "NET MESSAGE FLOAT requires a float message; the current net message is an integer");
    REQUIRE(Host.NextMessage());
    CHECK(Host.Value(SymbolId::Function_NetMessageType) == 3.0);
    CHECK(Host.String(SymbolId::Function_NetMessageStringString) == "hello host");
    Host.Require(SymbolId::Command_GetNetMessage);
    CHECK(Host.Value(SymbolId::Function_NetMessageExists) == 0.0);

    Host.Require(SymbolId::Command_SendNetMessageFloat, {Number(2), Number(2.5)});
    REQUIRE(Joiner.NextMessage());
    CHECK(Joiner.Value(SymbolId::Function_NetMessageType) == 2.0);
    CHECK(Joiner.Value(SymbolId::Function_NetMessageFloat) == 2.5);
    CHECK(Joiner.Value(SymbolId::Function_NetMessagePlayerFrom) == 1.0);
    CHECK(Joiner.Value(SymbolId::Function_NetMessagePlayerTo) == 2.0);
    CHECK_FALSE(Joiner.Function(SymbolId::Function_NetMessageStringString, Unused));
    // Negative players are errors; a player who is not in the game only drops the message.
    CHECK_FALSE(Joiner.Command(SymbolId::Command_SendNetMessageInteger, {Number(-1), Number(1)}));
    Joiner.Require(SymbolId::Command_SendNetMessageInteger, {Number(9), Number(1)});

    // Extra local players fill the game; a full game refuses the next application.
    CHECK(Joiner.Value(SymbolId::Function_CreateNetPlayer, {Text("Bot")}) == 3.0);
    CHECK(Host.WaitForValue(SymbolId::Function_NetPlayerCreated) == 3.0);
    Joiner.Require(SymbolId::Command_CreateNetPlayer, {Text("Ally")});
    CHECK(Joiner.Checklist(SymbolId::Command_PerformChecklistForNetPlayers).size() == 3);
    Late.Require(SymbolId::Command_JoinNetGame, {Number(1), Text("Carl")});
    CHECK(Late.Value(SymbolId::Function_NetGameExists) == 0.0);

    Host.Require(SymbolId::Command_SendNetMessageInteger, {Number(0), Number(7)});
    REQUIRE(Joiner.NextMessage());
    CHECK(Joiner.Value(SymbolId::Function_NetMessagePlayerTo) == 2.0);
    REQUIRE(Joiner.NextMessage());
    CHECK(Joiner.Value(SymbolId::Function_NetMessagePlayerTo) == 3.0);
    CHECK(Joiner.Value(SymbolId::Function_NetMessageInteger) == 7.0);

    CHECK_FALSE(Joiner.Command(SymbolId::Command_FreeNetPlayer, {Number(2)}));
    CHECK(Joiner.Error == "FREE NET PLAYER 2 is not a player CREATE NET PLAYER added in this application");
    CHECK_FALSE(Joiner.Command(SymbolId::Command_FreeNetPlayer, {Number(1)}));
    Joiner.Require(SymbolId::Command_FreeNetPlayer, {Number(3)});
    CHECK(Host.WaitForValue(SymbolId::Function_NetPlayerDestroyed) == 3.0);
    Late.Require(SymbolId::Command_PerformChecklistForNetSessions);
    Late.Require(SymbolId::Command_JoinNetGame, {Number(1), Text("Carl")});
    CHECK(Late.Value(SymbolId::Function_NetGameExists) == 1.0);
    CHECK(Late.Checklist(SymbolId::Command_PerformChecklistForNetPlayers).size() == 3);
    CHECK(Joiner.WaitForValue(SymbolId::Function_NetPlayerCreated) == 3.0);

    // The host never changes until host migration exists.
    CHECK(Joiner.Value(SymbolId::Function_NetGameNowHosting) == 0.0);
    Late.Require(SymbolId::Command_FreeNetGame);
    CHECK(Joiner.WaitForValue(SymbolId::Function_NetPlayerDestroyed) == 3.0);
    CHECK(Late.Value(SymbolId::Function_NetGameLost) == 0.0);

    Host.Require(SymbolId::Command_FreeNetGame);
    CHECK(Host.Value(SymbolId::Function_NetGameExists) == 0.0);
    CHECK(Joiner.WaitForValue(SymbolId::Function_NetGameLost) == 1.0);
    CHECK(Joiner.Value(SymbolId::Function_NetGameExists) == 0.0);
    Joiner.Require(SymbolId::Command_SendNetMessageInteger, {Number(0), Number(1)});
    Joiner.Require(SymbolId::Command_FreeNetGame);
    CHECK(Joiner.Value(SymbolId::Function_NetGameLost) == 0.0);
}

TEST_CASE("Classic JOIN NET GAME without a running session leaves no game")
{
    Application Joiner("classic-net-alone");
    // Classic programs try to join session 1 first and host when it fails.
    Joiner.Require(SymbolId::Command_SetNetConnection, {Number(1), Text(LoopbackAddress())});
    Joiner.Require(SymbolId::Command_JoinNetGame, {Number(1), Text("Bill")});
    CHECK(Joiner.Value(SymbolId::Function_NetGameExists) == 0.0);
    CHECK(Joiner.Checklist(SymbolId::Command_PerformChecklistForNetSessions).empty());
    Joiner.Require(SymbolId::Command_GetNetMessage);
    CHECK(Joiner.Value(SymbolId::Function_NetMessageExists) == 0.0);
    CHECK(Joiner.Value(SymbolId::Function_CreateNetPlayer, {Text("Bot")}) == 0.0);
}

TEST_CASE("Classic net games carry memblocks, images, bitmaps, sounds and meshes")
{
    Application Host("classic-net-media-host");
    Application Joiner("classic-net-media-joiner");
    Host.Require(SymbolId::Command_SetNetConnection, {Number(1), Text(LoopbackAddress())});
    Joiner.Require(SymbolId::Command_SetNetConnection, {Number(1), Text(LoopbackAddress())});
    Host.Require(SymbolId::Command_CreateNetGame, {Text("media"), Text("Fred"), Number(2)});
    Joiner.Require(SymbolId::Command_JoinNetGame, {Number(1), Text("Bill")});
    REQUIRE(Joiner.Value(SymbolId::Function_NetGameExists) == 1.0);

    dbc::EngineKernel& Source = Host.Kernel();
    std::string Error;
    REQUIRE(Source.Memblocks().MakeMemblock(1, 16, &Error));
    REQUIRE(Source.Memblocks().WriteMemblockByte(1, 15, 42, &Error));
    REQUIRE(Source.Memblocks().MakeMemblock(2, 20, &Error));
    REQUIRE(Source.Memblocks().WriteMemblockDword(2, 0, 2, &Error));
    REQUIRE(Source.Memblocks().WriteMemblockDword(2, 4, 1, &Error));
    REQUIRE(Source.Memblocks().WriteMemblockDword(2, 8, 32, &Error));
    REQUIRE(Source.Memblocks().WriteMemblockDword(2, 12, 0x00FF0000, &Error));
    REQUIRE(Source.Memblocks().MakeImageFromMemblock(1, 2, &Error));
    REQUIRE(Source.ImageStoreRef().CreateBitmap(1, 4, 3, &Error));
    const std::filesystem::path SoundPath =
        std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/memblocks/behold.wav";
    REQUIRE(Source.Audio().LoadSound(SoundPath.string(), 1, false, false, &Error));
    dbc::Mesh Triangle;
    Triangle.Vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    Triangle.Normals = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}};
    Triangle.Uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    Triangle.Indices = {0, 1, 2};
    Source.Objects().StoreMesh(1, std::move(Triangle));

    CHECK_FALSE(Host.Command(SymbolId::Command_SendNetMessageMemblock, {Number(2), Number(9)}));
    CHECK(Host.Error == "Memblock 9 does not exist");
    Host.Require(SymbolId::Command_SendNetMessageMemblock, {Number(2), Number(1), Number(1)});
    Host.Require(SymbolId::Command_SendNetMessageImage, {Number(2), Number(1), Number(1)});
    Host.Require(SymbolId::Command_SendNetMessageBitmap, {Number(0), Number(1), Number(1)});
    Host.Require(SymbolId::Command_SendNetMessageSound, {Number(2), Number(1), Number(1)});
    Host.Require(SymbolId::Command_SendNetMessageMesh, {Number(2), Number(1), Number(1)});
    // Unguaranteed messages travel apart and may arrive in another order, or not at all on a real network.
    Host.Require(SymbolId::Command_SendNetMessageMemblock, {Number(2), Number(1)});

    CHECK_FALSE(Joiner.Command(SymbolId::Command_NetMessageMemblock, {Number(5)}));
    CHECK(Joiner.Error == "NET MESSAGE MEMBLOCK requires a memblock message; there is no current net message");
    dbc::EngineKernel& Target = Joiner.Kernel();
    int Memblocks = 0;
    for (int Received = 0; Received < 6; ++Received)
    {
        REQUIRE(Joiner.NextMessage());
        CHECK(Joiner.Value(SymbolId::Function_NetMessagePlayerFrom) == 1.0);
        CHECK(Joiner.Value(SymbolId::Function_NetMessagePlayerTo) == 2.0);
        switch (static_cast<int>(Joiner.Value(SymbolId::Function_NetMessageType)))
        {
            case 4:
            {
                CHECK_FALSE(Joiner.Command(SymbolId::Command_NetMessageImage, {Number(3)}));
                CHECK(Joiner.Error ==
                      "NET MESSAGE IMAGE requires an image message; the current net message is a memblock");
                Joiner.Require(SymbolId::Command_NetMessageMemblock, {Number(5 + Memblocks)});
                int Value = 0;
                REQUIRE(Target.Memblocks().ReadMemblockByte(Value, 5 + Memblocks, 15, &Error));
                CHECK(Value == 42);
                ++Memblocks;
                break;
            }
            case 5:
            {
                Joiner.Require(SymbolId::Command_NetMessageImage, {Number(3)});
                const dbc::Image* Received3 = Target.ImageStoreRef().GetImage(3);
                REQUIRE(Received3 != nullptr);
                REQUIRE(Received3->Surface != nullptr);
                CHECK(Received3->Surface->w == 2);
                CHECK(Received3->Surface->h == 1);
                break;
            }
            case 6:
                Joiner.Require(SymbolId::Command_NetMessageBitmap, {Number(2)});
                CHECK(Target.ImageStoreRef().BitmapWidth(2) == 4);
                CHECK(Target.ImageStoreRef().BitmapHeight(2) == 3);
                break;
            case 7:
            {
                Joiner.Require(SymbolId::Command_NetMessageSound, {Number(4)});
                int Exists = 0;
                REQUIRE(Target.Audio().SoundExist(Exists, 4, &Error));
                CHECK(Exists == 1);
                break;
            }
            case 8:
            {
                CHECK_FALSE(Joiner.Command(SymbolId::Command_NetMessageMesh, {Number(0)}));
                CHECK(Joiner.Error == "NET MESSAGE MESH mesh number must be between 1 and 65535");
                Joiner.Require(SymbolId::Command_NetMessageMesh, {Number(6)});
                const std::vector<dbc::Mesh>* Meshes = Target.Objects().GetMeshes(6);
                REQUIRE(Meshes != nullptr);
                REQUIRE_FALSE(Meshes->empty());
                CHECK(Meshes->front().Vertices.size() >= 3);
                break;
            }
            default:
                FAIL("unexpected net message type");
        }
    }
    CHECK(Memblocks == 2);
    Joiner.Require(SymbolId::Command_GetNetMessage);
    CHECK(Joiner.Value(SymbolId::Function_NetMessageExists) == 0.0);
}
