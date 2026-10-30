#include "engine/systems/audio_system.hpp"
#include "engine/systems/bsp_world.hpp"
#include "engine/systems/camera_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/debug_system.hpp"
#include "engine/systems/draw_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/file_io_system.hpp"
#include "engine/systems/graphics_device_system.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/light_manager.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/matrix_manager.hpp"
#include "engine/systems/memblock_system.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/physics_system.hpp"
#include "engine/systems/platform_io.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/sprite_system.hpp"
#include "engine/systems/terrain_mesh.hpp"
#include "engine/systems/text_state.hpp"
#include "engine/systems/text_system.hpp"
#include "engine/systems/video_system.hpp"
#include "interpreter/engine_builtin_api/file_io.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"
#include "interpreter/execution_service.hpp"

#include <doctest/doctest.h>
#include <spdlog/sinks/base_sink.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_touch.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include "gstreamer_video_fixture.hpp"
#endif

using namespace dbc::interpreter::detail;

namespace
{
    int ReadBuiltinInteger(const BuiltinValue& Value)
    {
        int Result = 0;
        std::string Error;
        REQUIRE_MESSAGE(Value.AsInt(Result, &Error), Error);
        return Result;
    }

    const std::vector<uint8_t>& ReadMemblockData(dbc::MemblockSystem& Memblocks, int Number)
    {
        std::string Error;
        const auto* Data = Memblocks.MemblockData(Number, &Error);
        REQUIRE_MESSAGE(Data != nullptr, Error);
        return *Data;
    }

    template <typename Operation> bool RequireEngineStatus(Operation&& Execute)
    {
        std::string Error;
        const bool Succeeded = Execute(Error);
        REQUIRE_MESSAGE(Succeeded, Error);
        return Succeeded;
    }

    template <typename T, typename Reader> T ReadEngineNumber(Reader&& Read)
    {
        T Output{};
        std::string Error;
        REQUIRE_MESSAGE(Read(Error, Output), Error);
        return Output;
    }

    class StubLogSink final : public spdlog::sinks::base_sink<std::mutex>
    {
      public:
        struct Entry
        {
            spdlog::level::level_enum Level{};
            std::string Message{};
        };

        const std::vector<Entry>& Entries() const
        {
            return Entries_;
        }

      protected:
        void sink_it_(const spdlog::details::log_msg& Message) override
        {
            Entries_.push_back({Message.level, std::string(Message.payload.data(), Message.payload.size())});
        }

        void flush_() override {}

      private:
        std::vector<Entry> Entries_{};
    };

    class ScopedStubLogger
    {
      public:
        ScopedStubLogger()
        {
            Previous_ = spdlog::get("dbc");
            spdlog::drop("dbc");
            Sink_ = std::make_shared<StubLogSink>();
            Logger_ = std::make_shared<spdlog::logger>("dbc", Sink_);
            Logger_->set_level(spdlog::level::trace);
            spdlog::register_logger(Logger_);
        }

        ~ScopedStubLogger()
        {
            spdlog::drop("dbc");
            if (Previous_)
                spdlog::register_logger(Previous_);
        }

        const std::vector<StubLogSink::Entry>& Entries() const
        {
            return Sink_->Entries();
        }

      private:
        std::shared_ptr<spdlog::logger> Previous_{};
        std::shared_ptr<StubLogSink> Sink_{};
        std::shared_ptr<spdlog::logger> Logger_{};
    };
} // namespace

TEST_CASE("Companded WAV files load")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("companded-wave", 16, 16));
    const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (const std::uint16_t Format : {std::uint16_t{6}, std::uint16_t{7}})
    {
        // Enough samples to fill SDL_mixer's decode buffer, which used to overflow on the stack.
        const std::uint32_t Samples = 8192;
        std::vector<std::uint8_t> Wave;
        const auto Text = [&](std::string_view Value) { Wave.insert(Wave.end(), Value.begin(), Value.end()); };
        const auto Number = [&](std::uint32_t Value, int Size)
        {
            for (int Index = 0; Index < Size; ++Index)
                Wave.push_back(static_cast<std::uint8_t>(Value >> (Index * 8)));
        };
        Text("RIFF");
        Number(36 + Samples, 4);
        Text("WAVEfmt ");
        Number(16, 4);
        Number(Format, 2);
        Number(1, 2);
        Number(8000, 4);
        Number(8000, 4);
        Number(1, 2);
        Number(8, 2);
        Text("data");
        Number(Samples, 4);
        Wave.resize(Wave.size() + Samples, 0x55);

        const std::filesystem::path Path =
            std::filesystem::temp_directory_path() /
            ("opendbc-companded-" + std::to_string(Stamp) + "-" + std::to_string(Format) + ".wav");
        {
            std::ofstream Output(Path, std::ios::binary);
            Output.write(reinterpret_cast<const char*>(Wave.data()), static_cast<std::streamsize>(Wave.size()));
        }
        std::string Error;
        CHECK(Kernel.Audio().LoadSound(Path.string(), 1, false, false, &Error));
        CHECK(Error.empty());
        CHECK(Kernel.Audio().DeleteSound(1, &Error));
        std::filesystem::remove(Path);
    }
}

TEST_CASE("Primitive meshes use outward winding and enable culling by default")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("primitive-winding", 16, 16));
    Kernel.Objects().MakeObjectSphere(1, 2.0f, 8, 12);
    Kernel.Objects().MakeObjectCylinder(2, 2.0f, 4.0f, 12);
    Kernel.Objects().MakeObjectCone(3, 2.0f, 4.0f, 12);
    REQUIRE(Kernel.Objects().MakeObjectPlain(4, 2.0f, 2.0f));

    for (int ObjectId = 1; ObjectId <= 3; ++ObjectId)
    {
        const dbc::Object3D* Object = Kernel.Objects().GetObject(ObjectId);
        REQUIRE(Object != nullptr);
        CHECK(Object->Cull);
        REQUIRE_FALSE(Object->Meshes.empty());
        const dbc::Mesh& Mesh = Object->Meshes.front();
        for (size_t Index = 0; Index + 2 < Mesh.Geometry().Indices.size(); Index += 3)
        {
            const dbc::Vec3& A = Mesh.Geometry().Vertices[Mesh.Geometry().Indices[Index]];
            const dbc::Vec3& B = Mesh.Geometry().Vertices[Mesh.Geometry().Indices[Index + 1]];
            const dbc::Vec3& C = Mesh.Geometry().Vertices[Mesh.Geometry().Indices[Index + 2]];
            const dbc::Vec3 Normal = dbc::Vec3Cross(dbc::Vec3Sub(B, A), dbc::Vec3Sub(C, A));
            if (dbc::Vec3Dot(Normal, Normal) < 0.000001f)
                continue;
            const dbc::Vec3 Centroid{(A.X + B.X + C.X) / 3.0f, (A.Y + B.Y + C.Y) / 3.0f, (A.Z + B.Z + C.Z) / 3.0f};
            CHECK(dbc::Vec3Dot(Normal, Centroid) > 0.0f);
        }
    }

    const dbc::Object3D* Plain = Kernel.Objects().GetObject(4);
    REQUIRE(Plain != nullptr);
    CHECK(Plain->Cull);
    const dbc::Mesh& Mesh = Plain->Meshes.front();
    const dbc::Vec3 Normal = dbc::Vec3Cross(dbc::Vec3Sub(Mesh.Geometry().Vertices[Mesh.Geometry().Indices[1]],
                                                         Mesh.Geometry().Vertices[Mesh.Geometry().Indices[0]]),
                                            dbc::Vec3Sub(Mesh.Geometry().Vertices[Mesh.Geometry().Indices[2]],
                                                         Mesh.Geometry().Vertices[Mesh.Geometry().Indices[0]]));
    CHECK(Normal.Z > 0.0f);
}

TEST_CASE("Mesh and object file commands resolve through the execution asset root")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-mesh-root-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directories(Root / "models"));
    REQUIRE(std::filesystem::create_directories(Root / "saved"));
    REQUIRE(std::filesystem::copy_file(std::filesystem::path(TEST_SOURCE_DIR) /
                                           "examples/dbpro/tutorials/basic3d/models/ninja.X",
                                       Root / "models/ninja.x"));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("mesh-root", 16, 16));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.AssetRoot = Root;
    std::string Error;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadMesh,
                          {BuiltinValue::FromString("models/ninja.x"), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SaveMesh,
                          {BuiltinValue::FromString("saved/mesh.x"), BuiltinValue::FromNumber(1)}, Error));
    CHECK(std::filesystem::is_regular_file(Root / "saved/mesh.x"));

    Kernel.Objects().MakeObjectCube(1, 1.0f);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SaveObject,
                          {BuiltinValue::FromString("saved/object.dbo"), BuiltinValue::FromNumber(1)}, Error));
    CHECK(std::filesystem::is_regular_file(Root / "saved/object.dbo"));
    std::filesystem::remove_all(Root);
}

TEST_CASE("Historical text X frame matrices accept only the missing final terminator")
{
    const std::filesystem::path SkyPath =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models/dome.x";

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("iced-sky-x", 16, 16));
    REQUIRE(Kernel.Objects().LoadObject(SkyPath.string(), 1));
    const dbc::Object3D* Sky = Kernel.Objects().GetObject(1);
    REQUIRE(Sky != nullptr);
    REQUIRE_FALSE(Sky->Meshes.empty());
    CHECK(Sky->Meshes.front().Geometry().Indices.size() == 12);

    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path InvalidPath =
        std::filesystem::temp_directory_path() / ("opendbc-invalid-matrix-" + std::to_string(Suffix) + ".x");
    std::ifstream Source(SkyPath, std::ios::binary);
    REQUIRE(Source.good());
    std::string Invalid(std::istreambuf_iterator<char>(Source), {});
    const std::string FinalRow = "0.000000, 0.000000, 0.000000, 1.0;";
    const std::size_t FinalRowPosition = Invalid.find(FinalRow);
    REQUIRE(FinalRowPosition != std::string::npos);
    Invalid.replace(FinalRowPosition, FinalRow.size(), "0.000000, 0.000000, 1.0;");
    {
        std::ofstream Output(InvalidPath, std::ios::binary);
        REQUIRE(Output.good());
        Output.write(Invalid.data(), static_cast<std::streamsize>(Invalid.size()));
    }

    std::string Error;
    CHECK_FALSE(Kernel.Objects().LoadObject(InvalidPath.string(), 2, 0, 0, &Error));
    CHECK_FALSE(Error.empty());
    CHECK(Kernel.Objects().ObjectExist(2) == 0);
    std::error_code CleanupError;
    std::filesystem::remove(InvalidPath, CleanupError);
}

TEST_CASE("Classic imported X frames retain the DBC object root limb")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-imported-root-limb", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models";
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString("ARMOUR.X"), BuiltinValue::FromNumber(1)}, Error));
    const dbc::Object3D* Classic = Kernel.Objects().GetObject(1);
    REQUIRE(Classic != nullptr);
    REQUIRE(Classic->LimbNodes.size() == 20);
    REQUIRE(Classic->LimbNodes[0] != nullptr);
    REQUIRE(Classic->LimbNodes[1] != nullptr);
    CHECK(std::string(Classic->LimbNodes[0]->mName.C_Str()).empty());
    CHECK(std::string(Classic->LimbNodes[1]->mName.C_Str()) == "Root");
    CHECK(std::string(Classic->LimbNodes[3]->mName.C_Str()) == "turret_top");
    CHECK(std::string(Classic->LimbNodes[12]->mName.C_Str()) == "cannon");
    CHECK(std::string(Classic->LimbNodes[13]->mName.C_Str()) == "stowage");
    CHECK(std::string(Classic->LimbNodes[14]->mName.C_Str()) == "hatch");
    CHECK(std::string(Classic->LimbNodes[16]->mName.C_Str()) == "machine_gun");
    CHECK(std::string(Classic->LimbNodes[18]->mName.C_Str()) == "turret");
    CHECK(std::string(Classic->LimbNodes[19]->mName.C_Str()) == "mount");
    CHECK(Kernel.Objects().LimbExist(1, 0) == 1);
    CHECK(Kernel.Objects().LimbExist(1, 19) == 1);

    const auto Probe = std::find_if(Classic->Meshes.begin(), Classic->Meshes.end(),
                                    [](const dbc::Mesh& Mesh) { return !Mesh.Geometry().Vertices.empty(); });
    REQUIRE(Probe != Classic->Meshes.end());
    const dbc::Vec3 Before = Kernel.Renderer().WorldMeshVertex(*Classic, *Probe, 0);
    REQUIRE(Kernel.Objects().RotateLimb(1, 0, 0.0f, 90.0f, 0.0f));
    const dbc::Vec3 After = Kernel.Renderer().WorldMeshVertex(*Classic, *Probe, 0);
    const dbc::Vec3 RootDelta = dbc::Vec3Sub(After, Before);
    CHECK(dbc::Vec3Dot(RootDelta, RootDelta) > 0.01f);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RotateLimb,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(19), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(35), BuiltinValue::FromNumber(0)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_OffsetLimb,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(19), BuiltinValue::FromNumber(2),
                           BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(3)},
                          Error));
    CHECK(Kernel.Objects().LimbAngleY(1, 19) == doctest::Approx(35.0f));
    CHECK(Kernel.Objects().LimbOffsetX(1, 19) == doctest::Approx(2.0f));
    CHECK(Kernel.Objects().LimbOffsetZ(1, 19) == doctest::Approx(3.0f));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForObjectLimbs,
                          {BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(Context.Checklist.size() == 19);
    CHECK(Context.Checklist[18].Name == "mount");
    CHECK(Context.Checklist[18].A == 19);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString("turret/attack.x"), BuiltinValue::FromNumber(3)}, Error));
    const dbc::Object3D* PlayerTank = Kernel.Objects().GetObject(3);
    REQUIRE(PlayerTank != nullptr);
    const auto VisibleMeshCount = [PlayerTank]()
    {
        return std::count_if(PlayerTank->Meshes.begin(), PlayerTank->Meshes.end(),
                             [](const dbc::Mesh& MeshValue) { return MeshValue.Visible; });
    };
    const std::size_t VisibleBeforeHide = VisibleMeshCount();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_HideLimb,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1)}, Error));
    CHECK(VisibleMeshCount() < VisibleBeforeHide);
    CHECK(VisibleMeshCount() > 0);
    REQUIRE(PlayerTank->LimbTransforms.size() > 1);
    CHECK_FALSE(PlayerTank->LimbTransforms[1].Hidden);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ShowLimb,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1)}, Error));
    CHECK(VisibleMeshCount() == VisibleBeforeHide);

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString("ARMOUR.X"), BuiltinValue::FromNumber(2)}, Error));
    const dbc::Object3D* DbPro = Kernel.Objects().GetObject(2);
    REQUIRE(DbPro != nullptr);
    REQUIRE(DbPro->LimbNodes.size() == 19);
    CHECK(std::string(DbPro->LimbNodes[0]->mName.C_Str()) == "Root");
    CHECK(std::string(DbPro->LimbNodes[18]->mName.C_Str()) == "mount");
    CHECK(Kernel.Objects().LimbExist(2, 18) == 1);
    CHECK(Kernel.Objects().LimbExist(2, 19) == 0);
}

TEST_CASE("Classic Basic3D exam05 checklist quantity remains the highest imported limb identifier")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-exam05-limb-checklist", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models";
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString("idle.x"), BuiltinValue::FromNumber(1)}, Error));
    const dbc::Object3D* Classic = Kernel.Objects().GetObject(1);
    REQUIRE(Classic != nullptr);
    REQUIRE(Classic->LimbNodes.size() == 16);
    REQUIRE(Classic->CompatibilityRootNode != nullptr);
    CHECK(Classic->LimbNodes[0] == Classic->CompatibilityRootNode.get());

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForObjectLimbs,
                          {BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(Context.Checklist.size() == 15);
    CHECK(Context.Checklist.front().A == 1);
    CHECK(Context.Checklist.back().A == 15);
    for (int Limb = 1; Limb <= static_cast<int>(Context.Checklist.size()); ++Limb)
    {
        CAPTURE(Limb);
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_HideLimb,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(Limb)}, Error));
    }
    CHECK(Kernel.Objects().LimbExist(1, 15) == 1);
    CHECK(Kernel.Objects().LimbExist(1, 16) == 0);

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString("idle.x"), BuiltinValue::FromNumber(2)}, Error));
    const dbc::Object3D* Professional = Kernel.Objects().GetObject(2);
    REQUIRE(Professional != nullptr);
    REQUIRE(Professional->LimbNodes.size() == 16);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForObjectLimbs,
                          {BuiltinValue::FromNumber(2)}, Error));
    REQUIRE(Context.Checklist.size() == 16);
    CHECK(Context.Checklist.front().A == 0);
    CHECK(Context.Checklist.back().A == 15);
}

TEST_CASE("Loaded screen bitmaps persist and object defaults are isolated by execution profile")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("profile-runtime-policy", 32, 32));
    dbc::interpreter::ExecutionService Service(Kernel);
    const auto PrepareProfile = [&](dbc::language::LanguageProfile Profile, const char* LogicalPath)
    {
        dbc::interpreter::ExecutionRequest Request;
        Request.RootLogicalPath = LogicalPath;
        Request.Profile = Profile;
        Request.Sources.push_back({Request.RootLogicalPath, "value = 1\n"});
        return Service.Prepare(std::move(Request));
    };

    REQUIRE(PrepareProfile(dbc::language::LanguageProfile::Dbc, "classic-policy.dba"));
    CHECK(Kernel.RuntimeCompatibility().CreateBitmapAcceptsNeutralDimensions());
    CHECK(Kernel.RuntimeCompatibility().DefaultObjectCollisionShape() == dbc::CollisionShape::Polygon);
    CHECK(Kernel.RuntimeCompatibility().CloneUsesDefaultCollisionShape());
    CHECK_FALSE(Kernel.RuntimeCompatibility().PlainOrientationUsesObjectRotation());
    CHECK(Kernel.RuntimeCompatibility().AddSyntheticObjectRoot());
    CHECK_FALSE(Kernel.RuntimeCompatibility().IncludeSyntheticRootInLimbChecklist());
    CHECK_FALSE(Kernel.RuntimeCompatibility().RecursiveLimbVisibility());
    CHECK(Kernel.RuntimeCompatibility().LoadImageTextureModeUsesClassicHints());
    CHECK(Kernel.RuntimeCompatibility().SetObjectZeroUsesTransparentBlack());
    CHECK(Kernel.RuntimeCompatibility().NeutralOutOfRangeMemblockReads());
    CHECK(Kernel.RuntimeCompatibility().MeshMemblocks() == dbc::MeshMemblockLayout::Classic32ByteHeader);
    CHECK(Kernel.RuntimeCompatibility().SoundMemblocks() == dbc::SoundMemblockLayout::Classic12ByteHeader);

    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().GetObject(1) != nullptr);
    CHECK(Kernel.Objects().GetObject(1)->CollisionShapeValue == dbc::CollisionShape::Polygon);
    REQUIRE(Kernel.Objects().SetObject(1, 1, 0, 1));
    CHECK(Kernel.Objects().GetObject(1)->TransparentBlack);
    REQUIRE(Kernel.Objects().SetObjectCollisionToSpheres(1));
    Kernel.Objects().CloneObject(2, 1);
    REQUIRE(Kernel.Objects().GetObject(2) != nullptr);
    CHECK(Kernel.Objects().GetObject(2)->CollisionShapeValue == dbc::CollisionShape::Polygon);
    REQUIRE(Kernel.Objects().SetObjectCollisionToSpheres(2));
    CHECK(Kernel.Objects().GetObject(2)->CollisionShapeValue == dbc::CollisionShape::Sphere);

    const std::filesystem::path LoadingBitmap =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp/loading.bmp";
    // A picture loaded onto the screen stays there while the program waits (2D frames keep bitmap 0).
    REQUIRE(Kernel.ImageStoreRef().LoadBitmapFile(LoadingBitmap.string(), 0));
    REQUIRE(Kernel.ImageStoreRef().GetBitmapSurface(0) != nullptr);
    SDL_Surface* LoadingFrame = Kernel.Renderer().CaptureScreen();
    REQUIRE(LoadingFrame != nullptr);
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) != nullptr);
    SDL_Surface* FollowingFrame = Kernel.Renderer().CaptureScreen();
    REQUIRE(FollowingFrame != nullptr);
    REQUIRE(LoadingFrame->w == FollowingFrame->w);
    REQUIRE(LoadingFrame->h == FollowingFrame->h);
    SDL_Surface* LoadingSource = dbc::LoadSurface(LoadingBitmap.string());
    REQUIRE(LoadingSource != nullptr);
    SDL_Surface* ExpectedLoading =
        SDL_ScaleSurface(LoadingSource, LoadingFrame->w, LoadingFrame->h, SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(LoadingSource);
    REQUIRE(ExpectedLoading != nullptr);
    std::uint64_t LoadingAbsoluteError = 0;
    std::uint64_t FollowingAbsoluteError = 0;
    std::size_t LoadingComparedChannels = 0;
    for (int Y = 0; Y < LoadingFrame->h; ++Y)
    {
        for (int X = 0; X < LoadingFrame->w; ++X)
        {
            std::array<Uint8, 4> Loading{};
            std::array<Uint8, 4> Following{};
            std::array<Uint8, 4> Expected{};
            REQUIRE(SDL_ReadSurfacePixel(LoadingFrame, X, Y, &Loading[0], &Loading[1], &Loading[2], &Loading[3]));
            REQUIRE(
                SDL_ReadSurfacePixel(FollowingFrame, X, Y, &Following[0], &Following[1], &Following[2], &Following[3]));
            REQUIRE(
                SDL_ReadSurfacePixel(ExpectedLoading, X, Y, &Expected[0], &Expected[1], &Expected[2], &Expected[3]));
            for (std::size_t Channel = 0; Channel < 3; ++Channel)
            {
                LoadingAbsoluteError += static_cast<std::uint64_t>(
                    std::abs(static_cast<int>(Loading[Channel]) - static_cast<int>(Expected[Channel])));
                FollowingAbsoluteError += static_cast<std::uint64_t>(
                    std::abs(static_cast<int>(Following[Channel]) - static_cast<int>(Expected[Channel])));
            }
            LoadingComparedChannels += 3;
        }
    }
    SDL_DestroySurface(ExpectedLoading);
    SDL_DestroySurface(LoadingFrame);
    SDL_DestroySurface(FollowingFrame);
    REQUIRE(LoadingComparedChannels > 0);
    CHECK(static_cast<double>(LoadingAbsoluteError) / static_cast<double>(LoadingComparedChannels) < 16.0);
    CHECK(static_cast<double>(FollowingAbsoluteError) / static_cast<double>(LoadingComparedChannels) < 16.0);

    // Drawing over the loaded picture converts it to the drawable format instead of erasing it.
    Kernel.Draw().SetInk(0xFF0000);
    Kernel.Draw().DrawBox(0, 0, 3, 3);
    SDL_Surface* DrawnScreen = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(DrawnScreen != nullptr);
    CHECK(DrawnScreen->format == SDL_PIXELFORMAT_RGBA32);
    std::array<Uint8, 4> Kept{};
    REQUIRE(SDL_ReadSurfacePixel(DrawnScreen, DrawnScreen->w / 2, DrawnScreen->h / 2, &Kept[0], &Kept[1], &Kept[2],
                                 &Kept[3]));
    CHECK(Kept[3] == 255);

    REQUIRE(PrepareProfile(dbc::language::LanguageProfile::DbPro, "professional-policy.dba"));
    CHECK_FALSE(Kernel.RuntimeCompatibility().CreateBitmapAcceptsNeutralDimensions());
    CHECK(Kernel.RuntimeCompatibility().DefaultObjectCollisionShape() == dbc::CollisionShape::Sphere);
    CHECK_FALSE(Kernel.RuntimeCompatibility().CloneUsesDefaultCollisionShape());
    CHECK(Kernel.RuntimeCompatibility().PlainOrientationUsesObjectRotation());
    CHECK_FALSE(Kernel.RuntimeCompatibility().AddSyntheticObjectRoot());
    CHECK(Kernel.RuntimeCompatibility().IncludeSyntheticRootInLimbChecklist());
    CHECK(Kernel.RuntimeCompatibility().RecursiveLimbVisibility());
    CHECK_FALSE(Kernel.RuntimeCompatibility().LoadImageTextureModeUsesClassicHints());
    CHECK_FALSE(Kernel.RuntimeCompatibility().SetObjectZeroUsesTransparentBlack());
    CHECK_FALSE(Kernel.RuntimeCompatibility().NeutralOutOfRangeMemblockReads());
    CHECK(Kernel.RuntimeCompatibility().MeshMemblocks() == dbc::MeshMemblockLayout::ProfessionalFvf);
    CHECK(Kernel.RuntimeCompatibility().SoundMemblocks() == dbc::SoundMemblockLayout::ProfessionalWaveFormatEx);

    Kernel.Objects().MakeObjectCube(3, 2.0f);
    REQUIRE(Kernel.Objects().GetObject(3) != nullptr);
    CHECK(Kernel.Objects().GetObject(3)->CollisionShapeValue == dbc::CollisionShape::Sphere);
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(3));
    Kernel.Objects().CloneObject(4, 3);
    REQUIRE(Kernel.Objects().GetObject(4) != nullptr);
    CHECK(Kernel.Objects().GetObject(4)->CollisionShapeValue == dbc::CollisionShape::Polygon);
    REQUIRE(Kernel.Objects().SetObject(3, 1, 0, 1));
    CHECK_FALSE(Kernel.Objects().GetObject(3)->TransparentBlack);
    REQUIRE(Kernel.ImageStoreRef().LoadBitmapFile(LoadingBitmap.string(), 0));
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) != nullptr);
}

TEST_CASE("Classic RLE tutorial bitmaps copy through render-compatible surfaces")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-rle-bitmap-copy", 64, 64));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());

    const std::filesystem::path TutorialRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadBitmapFile((TutorialRoot / "backdrop.bmp").string(), 1));
    SDL_Surface* Source = Kernel.ImageStoreRef().GetBitmapSurface(1);
    REQUIRE(Source != nullptr);
    REQUIRE(SDL_BITSPERPIXEL(Source->format) <= 8);

    const auto ReadPixel = [](SDL_Surface* Surface, int X, int Y)
    {
        std::array<Uint8, 4> Pixel{};
        REQUIRE(SDL_ReadSurfacePixel(Surface, X, Y, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        return Pixel;
    };
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> Reference(
        SDL_ConvertSurface(Source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    REQUIRE(Reference != nullptr);
    const auto ExpectedPixel = ReadPixel(Reference.get(), 20, 20);

    CHECK_NOTHROW(Kernel.ImageStoreRef().CopyBitmap(1, 0));
    SDL_Surface* ScreenCopy = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(ScreenCopy != nullptr);
    CHECK(ScreenCopy->format == SDL_PIXELFORMAT_RGBA32);
    CHECK(ScreenCopy->w == Source->w);
    CHECK(ScreenCopy->h == Source->h);
    const auto ScreenPixel = ReadPixel(ScreenCopy, 20, 20);
    CHECK(ScreenPixel == ExpectedPixel);

    CHECK_NOTHROW(Kernel.ImageStoreRef().CopyBitmap(1, 2));
    SDL_Surface* BitmapCopy = Kernel.ImageStoreRef().GetBitmapSurface(2);
    REQUIRE(BitmapCopy != nullptr);
    CHECK(BitmapCopy->format == SDL_PIXELFORMAT_ARGB8888);
    const auto BitmapPixel = ReadPixel(BitmapCopy, 20, 20);
    CHECK(BitmapPixel == ExpectedPixel);

    REQUIRE(Kernel.ImageStoreRef().LoadBitmapFile((TutorialRoot / "badge.bmp").string(), 3));
    SDL_Surface* IndexedDestination = Kernel.ImageStoreRef().GetBitmapSurface(3);
    REQUIRE(IndexedDestination != nullptr);
    REQUIRE(SDL_BITSPERPIXEL(IndexedDestination->format) <= 8);
    CHECK_NOTHROW(Kernel.ImageStoreRef().CopyBitmapRegion(1, 0, 0, 64, 64, 3, 0, 0, 64, 64));
    SDL_Surface* RegionCopy = Kernel.ImageStoreRef().GetBitmapSurface(3);
    REQUIRE(RegionCopy != nullptr);
    CHECK(RegionCopy->format == SDL_PIXELFORMAT_ARGB8888);
    const auto RegionPixel = ReadPixel(RegionCopy, 20, 20);
    CHECK(RegionPixel == ExpectedPixel);
}

TEST_CASE("Classic inert animation dimensions create a selectable bitmap")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-inert-animation-bitmap", 8, 8));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = TEST_SOURCE_DIR;
    EngineBuiltinHost Host(Context);

    // A text file stays inert regardless of which codec plugins the system has installed.
    const std::vector<dbc::vm::Value> AnimationArguments{dbc::vm::Value::String("README.md"),
                                                         dbc::vm::Value::Integer(1)};
    REQUIRE(Host.Invoke({1,
                         dbc::language::SymbolId::Command_LoadAnimation,
                         AnimationArguments,
                         {dbc::language::TypeKind::Void, {}},
                         false})
                .State == dbc::vm::BuiltinState::Completed);
    const std::vector<dbc::vm::Value> AnimationId{dbc::vm::Value::Integer(1)};
    const dbc::vm::BuiltinResult Width = Host.Invoke(
        {2, dbc::language::SymbolId::Function_AnimationWidth, AnimationId, {dbc::language::TypeKind::Real, {}}, false});
    const dbc::vm::BuiltinResult Height = Host.Invoke({3,
                                                       dbc::language::SymbolId::Function_AnimationHeight,
                                                       AnimationId,
                                                       {dbc::language::TypeKind::Real, {}},
                                                       false});
    REQUIRE(Width.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(Height.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(Width.ReturnValue);
    REQUIRE(Height.ReturnValue);
    CHECK(Width.ReturnValue->AsReal() == 0.0);
    CHECK(Height.ReturnValue->AsReal() == 0.0);

    const std::vector<dbc::vm::Value> BitmapArguments{dbc::vm::Value::Integer(1), *Width.ReturnValue,
                                                      *Height.ReturnValue};
    CHECK(Host.Invoke({4,
                       dbc::language::SymbolId::Command_CreateBitmap,
                       BitmapArguments,
                       {dbc::language::TypeKind::Void, {}},
                       false})
              .State == dbc::vm::BuiltinState::Completed);
    CHECK(Kernel.ImageStoreRef().BitmapWidth(1) == 1);
    CHECK(Kernel.ImageStoreRef().BitmapHeight(1) == 1);
    CHECK(Kernel.ImageStoreRef().CurrentBitmap() == 1);
    Kernel.ImageStoreRef().SetCurrentBitmap(0);
    CHECK_NOTHROW(Kernel.ImageStoreRef().SetCurrentBitmap(1));
    std::string BitmapError;
    CHECK_FALSE(Kernel.ImageStoreRef().CreateBitmap(2, 0, 4, &BitmapError));
    CHECK(BitmapError.find("requires positive dimensions") != std::string::npos);

    Kernel.ImageStoreRef().DeleteBitmap(1);
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    const dbc::vm::BuiltinResult Professional = Host.Invoke({5,
                                                             dbc::language::SymbolId::Command_CreateBitmap,
                                                             BitmapArguments,
                                                             {dbc::language::TypeKind::Void, {}},
                                                             false});
    CHECK(Professional.State == dbc::vm::BuiltinState::Failed);
    CHECK(Professional.Error.find("requires positive dimensions") != std::string::npos);
    CHECK(Kernel.ImageStoreRef().BitmapExist(1) == 0);
}

TEST_CASE("Classic animations validate numbers, files, bitmaps and areas")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-animation-state", 64, 64));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = TEST_SOURCE_DIR;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::vector<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, Arguments, Error);
    };
    const auto Number = [&](dbc::language::SymbolId Symbol, int AnimationId)
    {
        BuiltinValue Result;
        std::string QueryError;
        REQUIRE_MESSAGE(InvokeFunction(Context, Symbol, {BuiltinValue::FromNumber(AnimationId)}, Result, QueryError),
                        QueryError);
        return ReadBuiltinInteger(Result);
    };
    using dbc::language::SymbolId;

    BuiltinValue Unused;
    CHECK_FALSE(
        InvokeFunction(Context, SymbolId::Function_AnimationExist, {BuiltinValue::FromNumber(33)}, Unused, Error));
    CHECK(Error == "Animation number must be between 1 and 32");
    CHECK_FALSE(Command(SymbolId::Command_LoadAnimation,
                        {BuiltinValue::FromString("missing.mp4"), BuiltinValue::FromNumber(2)}));
    CHECK(Error.find("LOAD ANIMATION cannot find") != std::string::npos);
    CHECK(Number(SymbolId::Function_AnimationExist, 2) == 0);

    REQUIRE(
        Command(SymbolId::Command_LoadAnimation, {BuiltinValue::FromString("README.md"), BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationExist, 2) == 1);
    CHECK_FALSE(
        Command(SymbolId::Command_LoadAnimation, {BuiltinValue::FromString("README.md"), BuiltinValue::FromNumber(2)}));
    CHECK(Error == "LOAD ANIMATION cannot replace existing animation 2");

    // An inert animation accepts every command but never plays.
    REQUIRE(Command(SymbolId::Command_LoopAnimation, {BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationPlaying, 2) == 0);
    CHECK(Number(SymbolId::Function_AnimationLooped, 2) == 1);
    CHECK(Number(SymbolId::Function_AnimationLooping, 2) == 1);
    REQUIRE(Command(SymbolId::Command_PauseAnimation, {BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationPaused, 2) == 0);
    REQUIRE(Command(SymbolId::Command_StopAnimation, {BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationLooped, 2) == 0);

    REQUIRE(Command(SymbolId::Command_PlaceAnimation,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                     BuiltinValue::FromNumber(50), BuiltinValue::FromNumber(40)}));
    CHECK(Number(SymbolId::Function_AnimationPositionX, 2) == 10);
    CHECK(Number(SymbolId::Function_AnimationPositionY, 2) == 20);
    CHECK(Number(SymbolId::Function_AnimationWidth, 2) == 40);
    CHECK(Number(SymbolId::Function_AnimationHeight, 2) == 20);
    REQUIRE(Command(SymbolId::Command_PlayAnimation,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(6)}));
    CHECK(Number(SymbolId::Function_AnimationPositionX, 2) == 5);
    CHECK(Number(SymbolId::Function_AnimationPositionY, 2) == 6);
    CHECK(Number(SymbolId::Function_AnimationWidth, 2) == 40);
    REQUIRE(Command(SymbolId::Command_PlayAnimation,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                     BuiltinValue::FromNumber(31), BuiltinValue::FromNumber(22)}));
    CHECK(Number(SymbolId::Function_AnimationWidth, 2) == 30);
    CHECK(Number(SymbolId::Function_AnimationHeight, 2) == 20);
    REQUIRE(Command(SymbolId::Command_LoopAnimation,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(5),
                     BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(45), BuiltinValue::FromNumber(26)}));
    CHECK(Number(SymbolId::Function_AnimationPositionX, 2) == 5);
    CHECK(Number(SymbolId::Function_AnimationWidth, 2) == 40);
    REQUIRE(Command(SymbolId::Command_PlayAnimation, {BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationLooped, 2) == 0);
    REQUIRE(Command(SymbolId::Command_ResumeAnimation, {BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationPlaying, 2) == 0);
    CHECK_FALSE(Command(SymbolId::Command_PlaceAnimation,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                         BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(40)}));
    CHECK(Error == "PLACE ANIMATION requires an area with a positive width and height");
    CHECK_FALSE(Command(SymbolId::Command_PlayAnimation,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(0),
                         BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(8)}));
    CHECK(Error == "PLAY ANIMATION requires existing bitmap 7");
    CHECK(Number(SymbolId::Function_AnimationPositionX, 2) == 5);

    CHECK_FALSE(Command(SymbolId::Command_StopAnimation, {BuiltinValue::FromNumber(3)}));
    CHECK(Error == "STOP ANIMATION requires existing animation 3");
    REQUIRE(Command(SymbolId::Command_DeleteAnimation, {BuiltinValue::FromNumber(2)}));
    CHECK(Number(SymbolId::Function_AnimationExist, 2) == 0);
}

#if defined(__APPLE__) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
TEST_CASE("Classic animations play decoded video into their bitmap")
{
#if defined(__linux__)
    const dbc::tests::GStreamerVideoFixture Fixture;
#endif
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-animation-playback", 64, 64));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/video";
#if defined(__linux__)
    Context.AssetRoot = Fixture.Root;
#endif
    std::string Error;
    using dbc::language::SymbolId;
    const auto Command = [&](SymbolId Symbol, std::vector<BuiltinValue> Arguments)
    { REQUIRE_MESSAGE(InvokeCommand(Context, Symbol, Arguments, Error), Error); };
    const auto Number = [&](SymbolId Symbol)
    {
        BuiltinValue Result;
        REQUIRE_MESSAGE(InvokeFunction(Context, Symbol, {BuiltinValue::FromNumber(1)}, Result, Error), Error);
        return ReadBuiltinInteger(Result);
    };
    const auto Pixel = [&](int X, int Y)
    {
        std::array<Uint8, 4> Value{};
        REQUIRE(SDL_ReadSurfacePixel(Kernel.ImageStoreRef().GetBitmapSurface(1), X, Y, &Value[0], &Value[1], &Value[2],
                                     &Value[3]));
        return Value;
    };
    // AVPlayer advances on the main run loop, which SDL pumps in the runtime.
    const auto PlayUntil = [&](auto&& Reached)
    {
        const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!Reached() && std::chrono::steady_clock::now() < Deadline)
        {
#if defined(__APPLE__)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
            Kernel.Video().PaintFrames();
        }
        return Reached();
    };
    const auto Red = [&] { return Pixel(8, 8)[0] > 200 && Pixel(8, 8)[2] < 60; };
    const auto Blue = [&] { return Pixel(8, 8)[2] > 200 && Pixel(8, 8)[0] < 60; };

    // The fixture shows red for one second, then blue for one second.
    REQUIRE(Kernel.ImageStoreRef().CreateBitmap(1, 64, 48, &Error));
    const auto Outside = Pixel(40, 30);
#if defined(__APPLE__) || defined(_WIN32)
    const char* Video = "red_blue.mp4";
    constexpr int Width = 64;
#else
    const char* Video = "red_blue.avi";
    constexpr int Width = 65;
#endif
    Command(SymbolId::Command_LoadAnimation, {BuiltinValue::FromString(Video), BuiltinValue::FromNumber(1)});
    CHECK(Number(SymbolId::Function_AnimationWidth) == Width);
    CHECK(Number(SymbolId::Function_AnimationHeight) == 48);
    Command(SymbolId::Command_PlayAnimation,
            {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0),
             BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(32), BuiltinValue::FromNumber(24)});
    CHECK(Number(SymbolId::Function_AnimationPlaying) == 1);
    CHECK(PlayUntil(Red));
    CHECK(Pixel(40, 30) == Outside);

    Command(SymbolId::Command_PauseAnimation, {BuiltinValue::FromNumber(1)});
    CHECK(Number(SymbolId::Function_AnimationPaused) == 1);
    CHECK(Number(SymbolId::Function_AnimationPlaying) == 0);
    Command(SymbolId::Command_ResumeAnimation, {BuiltinValue::FromNumber(1)});
    CHECK(Number(SymbolId::Function_AnimationPlaying) == 1);
    CHECK(PlayUntil(Blue));
    CHECK(PlayUntil([&] { return Number(SymbolId::Function_AnimationPlaying) == 0; }));
    CHECK(Blue());

    // Looping starts over from the first frame and keeps playing past the end.
    Command(SymbolId::Command_LoopAnimation, {BuiltinValue::FromNumber(1)});
    CHECK(PlayUntil(Red));
    CHECK(PlayUntil(Blue));
    CHECK(PlayUntil(Red));
    CHECK(Number(SymbolId::Function_AnimationPlaying) == 1);
    CHECK(Number(SymbolId::Function_AnimationLooped) == 1);

    // Deleting the animation leaves its last picture in the bitmap.
    Command(SymbolId::Command_DeleteAnimation, {BuiltinValue::FromNumber(1)});
    Kernel.Video().PaintFrames();
    CHECK(Red());
}
#endif

TEST_CASE("Swarm Classic plains face negative Z while DBPro orientations remain unchanged")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("swarm-plain-orientation", 128, 128));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    std::string Error;

    const auto LocalNormalZ = [&](int ObjectId)
    {
        const dbc::Object3D* Object = Kernel.Objects().GetObject(ObjectId);
        REQUIRE(Object != nullptr);
        REQUIRE_FALSE(Object->Meshes.empty());
        const dbc::Mesh& Mesh = Object->Meshes.front();
        REQUIRE(Mesh.Geometry().Indices.size() >= 3);
        const dbc::Vec3 Normal = dbc::Vec3Cross(dbc::Vec3Sub(Mesh.Geometry().Vertices[Mesh.Geometry().Indices[1]],
                                                             Mesh.Geometry().Vertices[Mesh.Geometry().Indices[0]]),
                                                dbc::Vec3Sub(Mesh.Geometry().Vertices[Mesh.Geometry().Indices[2]],
                                                             Mesh.Geometry().Vertices[Mesh.Geometry().Indices[0]]));
        return Normal.Z;
    };

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectPlain,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(6)},
                          Error));
    REQUIRE(Kernel.Objects().GetObject(1) != nullptr);
    CHECK(Kernel.Objects().GetObject(1)->RotY == doctest::Approx(0.0f));
    CHECK(LocalNormalZ(1) < 0.0f);

    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.Objects().HideObject(1);
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(0, 0.0f, -40.0f, -200.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0, 0.0f, 0.0f, 0.0f, &Error); });

    SDL_Surface* Backdrop = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Backdrop != nullptr);
    REQUIRE(SDL_WriteSurfacePixel(Backdrop, 0, 0, 24, 112, 48, 255));
    Kernel.ImageStoreRef().StoreImageFromSurface(90, Backdrop);
    SDL_DestroySurface(Backdrop);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Draw().SetBackdropImage(90, &Error); });

    SDL_Surface* GhostTexture = SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(GhostTexture != nullptr);
    REQUIRE(SDL_WriteSurfacePixel(GhostTexture, 0, 0, 0, 0, 0, 255));
    REQUIRE(SDL_WriteSurfacePixel(GhostTexture, 1, 0, 255, 0, 0, 255));
    Kernel.ImageStoreRef().StoreImageFromSurface(91, GhostTexture);
    SDL_DestroySurface(GhostTexture);
    REQUIRE(Kernel.Objects().MakeObjectPlain(15, 160.0f, 100.0f));
    Kernel.Objects().TextureObject(15, 91);
    REQUIRE(Kernel.Objects().SetObjectFilter(15, 0));
    REQUIRE(Kernel.Objects().SetObjectLight(15, 0));
    Kernel.Objects().GhostObjectOn(15);
    Kernel.Objects().PositionObject(15, 0.0f, 0.0f, 0.0f);
    Kernel.Objects().HideObject(15);
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(0, 0.0f, -40.0f, -200.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0, 0.0f, 0.0f, 0.0f, &Error); });

    SDL_Surface* GhostBaseline = Kernel.Renderer().CaptureScreen();
    REQUIRE(GhostBaseline != nullptr);
    Kernel.Objects().ShowObject(15);
    SDL_Surface* GhostActive = Kernel.Renderer().CaptureScreen();
    REQUIRE(GhostActive != nullptr);
    int GhostChanged = 0;
    int GhostPreserved = 0;
    for (int Y = GhostActive->h / 2 - 24; Y <= GhostActive->h / 2 + 24; ++Y)
    {
        for (int X = GhostActive->w / 2 - 30; X <= GhostActive->w / 2 + 30; ++X)
        {
            Uint8 BaseRed = 0;
            Uint8 BaseGreen = 0;
            Uint8 BaseBlue = 0;
            Uint8 BaseAlpha = 0;
            Uint8 ActiveRed = 0;
            Uint8 ActiveGreen = 0;
            Uint8 ActiveBlue = 0;
            Uint8 ActiveAlpha = 0;
            REQUIRE(SDL_ReadSurfacePixel(GhostBaseline, X, Y, &BaseRed, &BaseGreen, &BaseBlue, &BaseAlpha));
            REQUIRE(SDL_ReadSurfacePixel(GhostActive, X, Y, &ActiveRed, &ActiveGreen, &ActiveBlue, &ActiveAlpha));
            const int Difference = std::abs(static_cast<int>(BaseRed) - static_cast<int>(ActiveRed)) +
                                   std::abs(static_cast<int>(BaseGreen) - static_cast<int>(ActiveGreen)) +
                                   std::abs(static_cast<int>(BaseBlue) - static_cast<int>(ActiveBlue));
            GhostChanged += Difference > 24 ? 1 : 0;
            GhostPreserved += Difference <= 8 ? 1 : 0;
        }
    }
    CHECK(GhostChanged > 100);
    CHECK(GhostPreserved > 100);
    int AdditiveTriangles = 0;
    for (const dbc::Triangle& TriangleValue : Kernel.Renderer().CachedWorldTris())
    {
        if (!TriangleValue.Additive)
            continue;
        ++AdditiveTriangles;
        CHECK_FALSE(TriangleValue.DepthWrite);
    }
    CHECK(AdditiveTriangles > 0);
    SDL_DestroySurface(GhostActive);
    SDL_DestroySurface(GhostBaseline);
    Kernel.Objects().HideObject(15);

    const std::filesystem::path SwarmRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((SwarmRoot / "bmp/beam.bmp").string(), 3));
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((SwarmRoot / "bmp/spark.bmp").string(), 4));
    REQUIRE(Kernel.Objects().MakeObjectPlain(301, 5.0f, 20.0f));
    REQUIRE(Kernel.Objects().MakeObjectPlain(302, 10.0f, 10.0f));
    Kernel.Objects().TextureObject(301, 3);
    Kernel.Objects().TextureObject(302, 4);
    for (const int ObjectId : {301, 302})
    {
        Kernel.Objects().ScaleObject(ObjectId, 100.0f, 100.0f, 200.0f);
        REQUIRE(Kernel.Objects().SetObject(ObjectId, 1, 0, 1));
        Kernel.Objects().GhostObjectOn(ObjectId);
        Kernel.Objects().FadeObject(ObjectId, 200);
        Kernel.Objects().HideObject(ObjectId);
        CHECK(LocalNormalZ(ObjectId) < 0.0f);
        REQUIRE(Kernel.Objects().GetObject(ObjectId) != nullptr);
        CHECK(Kernel.Objects().GetObject(ObjectId)->TransparentBlack);
    }
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(0, 0.0f, -40.0f, -200.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0, 0.0f, 0.0f, 0.0f, &Error); });
    SDL_Surface* Baseline = Kernel.Renderer().CaptureScreen();
    REQUIRE(Baseline != nullptr);

    SDL_Event SpaceDown{};
    SpaceDown.type = SDL_EVENT_KEY_DOWN;
    SpaceDown.key.windowID = SDL_GetWindowID(Kernel.Core().Window());
    SpaceDown.key.scancode = SDL_SCANCODE_SPACE;
    SpaceDown.key.key = SDLK_SPACE;
    SpaceDown.key.repeat = false;
    Kernel.Input().BeginEventFrame();
    Kernel.Input().HandleEvent(SpaceDown);
    CHECK(Kernel.Input().Spacekey() == 1);
    Kernel.Objects().ShowObject(301);
    Kernel.Objects().ShowObject(302);
    Kernel.Objects().PositionObject(301, -35.0f, -20.0f, -10.0f);
    Kernel.Objects().PositionObject(302, 35.0f, 20.0f, -10.0f);
    SDL_Surface* ActiveLasers = Kernel.Renderer().CaptureScreen();
    REQUIRE(ActiveLasers != nullptr);
    REQUIRE(ActiveLasers->w == Baseline->w);
    REQUIRE(ActiveLasers->h == Baseline->h);
    const auto ChangedPixelsAround = [&](int CentreX, int CentreY)
    {
        int Changed = 0;
        for (int Y = std::max(0, CentreY - 10); Y <= std::min(ActiveLasers->h - 1, CentreY + 10); ++Y)
        {
            for (int X = std::max(0, CentreX - 10); X <= std::min(ActiveLasers->w - 1, CentreX + 10); ++X)
            {
                Uint8 BaseRed = 0;
                Uint8 BaseGreen = 0;
                Uint8 BaseBlue = 0;
                Uint8 BaseAlpha = 0;
                Uint8 ActiveRed = 0;
                Uint8 ActiveGreen = 0;
                Uint8 ActiveBlue = 0;
                Uint8 ActiveAlpha = 0;
                REQUIRE(SDL_ReadSurfacePixel(Baseline, X, Y, &BaseRed, &BaseGreen, &BaseBlue, &BaseAlpha));
                REQUIRE(SDL_ReadSurfacePixel(ActiveLasers, X, Y, &ActiveRed, &ActiveGreen, &ActiveBlue, &ActiveAlpha));
                const int Difference = std::abs(static_cast<int>(BaseRed) - static_cast<int>(ActiveRed)) +
                                       std::abs(static_cast<int>(BaseGreen) - static_cast<int>(ActiveGreen)) +
                                       std::abs(static_cast<int>(BaseBlue) - static_cast<int>(ActiveBlue));
                Changed += Difference > 24 ? 1 : 0;
            }
        }
        return Changed;
    };
    const int PlayerLaserX = ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                                   { return Kernel.Objects().ObjectScreenX(Output, 301, &Error); });
    const int PlayerLaserY = ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                                   { return Kernel.Objects().ObjectScreenY(Output, 301, &Error); });
    const int EnemyLaserX = ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                                  { return Kernel.Objects().ObjectScreenX(Output, 302, &Error); });
    const int EnemyLaserY = ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                                  { return Kernel.Objects().ObjectScreenY(Output, 302, &Error); });
    CHECK(std::abs(PlayerLaserX - EnemyLaserX) > 20);
    CHECK(ChangedPixelsAround(PlayerLaserX, PlayerLaserY) > 2);
    CHECK(ChangedPixelsAround(EnemyLaserX, EnemyLaserY) > 2);
    SDL_DestroySurface(ActiveLasers);
    SDL_DestroySurface(Baseline);

    SDL_Event SpaceUp = SpaceDown;
    SpaceUp.type = SDL_EVENT_KEY_UP;
    Kernel.Input().HandleEvent(SpaceUp);
    CHECK(Kernel.Input().Spacekey() == 0);

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectPlain,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(6)},
                          Error));
    REQUIRE(Kernel.Objects().GetObject(2) != nullptr);
    CHECK(Kernel.Objects().GetObject(2)->RotY == doctest::Approx(180.0f));
    CHECK(LocalNormalZ(2) > 0.0f);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectPlain,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(6),
                           BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(Kernel.Objects().GetObject(3) != nullptr);
    CHECK(Kernel.Objects().GetObject(3)->RotY == doctest::Approx(0.0f));
    CHECK(LocalNormalZ(3) > 0.0f);
}

TEST_CASE("JetSki rider keeps Classic limb numbering transforms and hidden attachment meshes")
{
    const std::filesystem::path RiderPath =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models/rider.x";
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("jetski-rider", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Objects().LoadObject(RiderPath.string(), 1));

    const dbc::Object3D* Rider = Kernel.Objects().GetObject(1);
    REQUIRE(Rider != nullptr);
    REQUIRE(Rider->LimbNodes.size() == 19);
    REQUIRE(Rider->Meshes.size() == 18);
    for (const int Limb : {0, 1, 10, 11, 12})
        REQUIRE(Rider->LimbNodes[static_cast<std::size_t>(Limb)] != nullptr);
    REQUIRE(Rider->CompatibilityRootNode != nullptr);
    REQUIRE(Rider->CompatibilitySkippedRootNode == Rider->AssimpScene->mRootNode);
    CHECK(std::string(Rider->AssimpScene->mRootNode->mName.C_Str()) == "$dummy_root");
    CHECK(std::string(Rider->LimbNodes[0]->mName.C_Str()).empty());
    CHECK(std::string(Rider->LimbNodes[1]->mName.C_Str()) == "body");
    CHECK(std::string(Rider->LimbNodes[10]->mName.C_Str()) == "gauge");
    CHECK(std::string(Rider->LimbNodes[11]->mName.C_Str()) == "paddle");
    CHECK(std::string(Rider->LimbNodes[12]->mName.C_Str()) == "flask");
    for (const auto& [Limb, MeshIndex] : {std::pair{10, 9}, std::pair{11, 10}, std::pair{12, 11}})
    {
        const dbc::Mesh* LimbMesh = Kernel.Renderer().FindLimbMesh(*Rider, Limb);
        REQUIRE(LimbMesh != nullptr);
        CHECK(LimbMesh->SourceMeshIndex == MeshIndex);
    }
    CHECK(Rider->CollisionShapeValue == dbc::CollisionShape::Polygon);

    const std::size_t VisibleBefore =
        std::count_if(Rider->Meshes.begin(), Rider->Meshes.end(), [](const dbc::Mesh& Mesh) { return Mesh.Visible; });
    for (const int Limb : {10, 11, 12})
    {
        REQUIRE(Kernel.Objects().HideLimb(1, Limb));
        CHECK(Kernel.Objects().LimbVisible(1, Limb) == 0);
        CHECK_FALSE(Rider->LimbTransforms[static_cast<std::size_t>(Limb)].Hidden);
    }
    CHECK(std::count_if(Rider->Meshes.begin(), Rider->Meshes.end(),
                        [](const dbc::Mesh& Mesh) { return Mesh.Visible; }) == VisibleBefore - 3);

    const dbc::Mesh* BodyMesh = Kernel.Renderer().FindLimbMesh(*Rider, 1);
    REQUIRE(BodyMesh != nullptr);
    REQUIRE_FALSE(BodyMesh->Geometry().Vertices.empty());
    const dbc::Vec3 VertexBefore = Kernel.Renderer().WorldMeshVertex(*Rider, *BodyMesh, 0);
    const dbc::Vec3 OriginBefore = Kernel.Renderer().WorldLimbOrigin(*Rider, 1);
    REQUIRE(Kernel.Objects().OffsetLimb(1, 1, 7.0f, -3.0f, 11.0f));
    const dbc::Vec3 VertexAfter = Kernel.Renderer().WorldMeshVertex(*Rider, *BodyMesh, 0);
    const dbc::Vec3 OriginAfter = Kernel.Renderer().WorldLimbOrigin(*Rider, 1);
    const dbc::Vec3 VertexDelta = dbc::Vec3Sub(VertexAfter, VertexBefore);
    const dbc::Vec3 OriginDelta = dbc::Vec3Sub(OriginAfter, OriginBefore);
    CHECK(VertexDelta.X == doctest::Approx(OriginDelta.X).epsilon(0.001));
    CHECK(VertexDelta.Y == doctest::Approx(OriginDelta.Y).epsilon(0.001));
    CHECK(VertexDelta.Z == doctest::Approx(OriginDelta.Z).epsilon(0.001));

    REQUIRE(Kernel.Objects().RotateLimb(1, 1, 17.0f, 31.0f, -23.0f));
    REQUIRE(Kernel.Objects().ScaleLimb(1, 1, 125.0f, 70.0f, -85.0f, 1));
    CHECK(Kernel.Objects().LimbAngleY(1, 1) == doctest::Approx(31.0f));
    CHECK(Rider->LimbTransforms[1].ScaleZ == doctest::Approx(-0.85f));
    CHECK(std::isfinite(Rider->BoundingRadius));
    CHECK(Rider->BoundingRadius > 0.0f);
    dbc::Vec3 RenderMinimum{};
    dbc::Vec3 RenderMaximum{};
    bool FirstRenderedVertex = true;
    for (const dbc::Mesh& Mesh : Rider->Meshes)
    {
        if (Mesh.Geometry().Vertices.empty())
            continue;
        CHECK(Mesh.Geometry().Indices.size() % 3 == 0);
        for (const std::uint32_t Index : Mesh.Geometry().Indices)
            CHECK(Index < Mesh.Geometry().Vertices.size());
        for (std::size_t VertexIndex = 0; VertexIndex < Mesh.Geometry().Vertices.size(); ++VertexIndex)
        {
            const dbc::Vec3 Position = Kernel.Renderer().WorldMeshVertex(*Rider, Mesh, VertexIndex);
            CHECK(std::isfinite(Position.X));
            CHECK(std::isfinite(Position.Y));
            CHECK(std::isfinite(Position.Z));
            if (FirstRenderedVertex)
            {
                RenderMinimum = Position;
                RenderMaximum = Position;
                FirstRenderedVertex = false;
            }
            else
            {
                RenderMinimum.X = std::min(RenderMinimum.X, Position.X);
                RenderMinimum.Y = std::min(RenderMinimum.Y, Position.Y);
                RenderMinimum.Z = std::min(RenderMinimum.Z, Position.Z);
                RenderMaximum.X = std::max(RenderMaximum.X, Position.X);
                RenderMaximum.Y = std::max(RenderMaximum.Y, Position.Y);
                RenderMaximum.Z = std::max(RenderMaximum.Z, Position.Z);
            }
        }
    }
    REQUIRE_FALSE(FirstRenderedVertex);
    CHECK(Kernel.Objects().ObjectSizeX(1) == doctest::Approx(RenderMaximum.X - RenderMinimum.X).epsilon(0.001));
    CHECK(Kernel.Objects().ObjectSizeY(1) == doctest::Approx(RenderMaximum.Y - RenderMinimum.Y).epsilon(0.001));
    CHECK(Kernel.Objects().ObjectSizeZ(1) == doctest::Approx(RenderMaximum.Z - RenderMinimum.Z).epsilon(0.001));
    const float MaxX = std::max(std::abs(RenderMinimum.X), std::abs(RenderMaximum.X));
    const float MaxY = std::max(std::abs(RenderMinimum.Y), std::abs(RenderMaximum.Y));
    const float MaxZ = std::max(std::abs(RenderMinimum.Z), std::abs(RenderMaximum.Z));
    CHECK(Rider->BoundingRadius == doctest::Approx(std::sqrt(MaxX * MaxX + MaxY * MaxY + MaxZ * MaxZ)).epsilon(0.001));

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(Kernel.Objects().LoadObject(RiderPath.string(), 2));
    const dbc::Object3D* DbProRider = Kernel.Objects().GetObject(2);
    REQUIRE(DbProRider != nullptr);
    REQUIRE(DbProRider->LimbNodes.size() == 19);
    REQUIRE(DbProRider->LimbNodes[0] != nullptr);
    REQUIRE(DbProRider->LimbNodes[1] != nullptr);
    REQUIRE(DbProRider->LimbNodes[10] != nullptr);
    CHECK(DbProRider->CompatibilityRootNode == nullptr);
    CHECK(DbProRider->CompatibilitySkippedRootNode == nullptr);
    CHECK(std::string(DbProRider->LimbNodes[0]->mName.C_Str()) == "$dummy_root");
    CHECK(std::string(DbProRider->LimbNodes[1]->mName.C_Str()) == "body");
    CHECK(std::string(DbProRider->LimbNodes[10]->mName.C_Str()) == "gauge");
    CHECK(DbProRider->CollisionShapeValue == dbc::CollisionShape::Sphere);
}

TEST_CASE("Room collision defaults leave the centre open and retain polygon walls")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-room-collision", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Core().EnsureInitialized());

    REQUIRE(Kernel.Objects().MakeObjectPlain(5, 1200.0f, 800.0f));
    REQUIRE(Kernel.Objects().MakeMeshFromObject(1, 5));
    Kernel.Objects().PositionObject(5, 0.0f, 0.0f, 1200.0f);
    for (int Limb = 1; Limb <= 7; ++Limb)
        REQUIRE(Kernel.Objects().AddLimb(5, Limb, 1));
    const std::array<dbc::Vec3, 7> Offsets{{
        {-800.0f, 0.0f, -350.0f},
        {800.0f, 0.0f, -350.0f},
        {-1200.0f, 0.0f, -1200.0f},
        {1200.0f, 0.0f, -1200.0f},
        {-800.0f, 0.0f, -2050.0f},
        {800.0f, 0.0f, -2050.0f},
        {0.0f, 0.0f, -2400.0f},
    }};
    const std::array<float, 7> Rotations{{135.0f, 45.0f, 270.0f, 90.0f, 45.0f, 135.0f, 180.0f}};
    for (int Limb = 1; Limb <= 7; ++Limb)
    {
        const dbc::Vec3& Offset = Offsets[static_cast<std::size_t>(Limb - 1)];
        REQUIRE(Kernel.Objects().OffsetLimb(5, Limb, Offset.X, Offset.Y, Offset.Z));
        REQUIRE(Kernel.Objects().RotateLimb(5, Limb, 0.0f, Rotations[static_cast<std::size_t>(Limb - 1)], 0.0f));
    }
    Kernel.Objects().HideObject(5);

    Kernel.Objects().MakeObjectSphere(102, 300.0f, 12, 16);
    Kernel.Objects().ScaleObject(102, 100.0f, 60.0f, 100.0f);
    Kernel.Objects().PositionObject(102, 150.0f, -150.0f, 0.0f);
    CHECK(Kernel.Objects().GetObject(5)->CollisionShapeValue == dbc::CollisionShape::Polygon);
    CHECK(Kernel.Objects().GetObject(102)->CollisionShapeValue == dbc::CollisionShape::Polygon);
    CHECK(Kernel.Objects().ObjectCollision(102, 5) == 0);

    Kernel.Objects().PositionObject(102, 0.0f, -150.0f, 1100.0f);
    CHECK(Kernel.Objects().ObjectCollision(102, 5) == 1);

    Kernel.Objects().PositionObject(102, 0.0f, -150.0f, -1100.0f);
    CHECK(Kernel.Objects().ObjectCollision(102, 5) == 1);

    Kernel.Objects().MakeObjectCone(6, 540.0f, 480.0f, 16);
    Kernel.Objects().PositionObject(6, 0.0f, -290.0f, 0.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionOff(6));
    Kernel.Objects().PositionObject(102, 0.0f, -150.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(102, 6) == 0);

    SDL_Event UpDown{};
    UpDown.type = SDL_EVENT_KEY_DOWN;
    UpDown.key.windowID = SDL_GetWindowID(Kernel.Core().Window());
    UpDown.key.scancode = SDL_SCANCODE_UP;
    UpDown.key.key = SDLK_UP;
    UpDown.key.repeat = false;
    Kernel.Input().BeginEventFrame();
    Kernel.Input().HandleEvent(UpDown);
    CHECK(Kernel.Input().Upkey() == 1);
    float PlayerZ = 0.0f;
    bool HitWall = false;
    for (int Frame = 0; Frame < 30 && Kernel.Input().Upkey() != 0; ++Frame)
    {
        const float AttemptedZ = PlayerZ + 100.0f;
        Kernel.Objects().PositionObject(102, 0.0f, -150.0f, AttemptedZ);
        if (Kernel.Objects().ObjectCollision(102, 5) != 0)
        {
            Kernel.Objects().PositionObject(102, 0.0f, -150.0f, PlayerZ);
            HitWall = true;
            break;
        }
        PlayerZ = AttemptedZ;
    }
    CHECK(HitWall);
    CHECK(PlayerZ > 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(102, 5) == 0);
    Kernel.Objects().PositionObject(102, 0.0f, -150.0f, PlayerZ + 100.0f);
    CHECK(Kernel.Objects().ObjectCollision(102, 5) == 1);
    Kernel.Objects().PositionObject(102, 0.0f, -150.0f, PlayerZ);

    SDL_Event UpUp = UpDown;
    UpUp.type = SDL_EVENT_KEY_UP;
    Kernel.Input().HandleEvent(UpUp);
    CHECK(Kernel.Input().Upkey() == 0);
    const float ReleasedZ = Kernel.Objects().GetObject(102)->PosZ;
    if (Kernel.Input().Upkey() != 0)
        Kernel.Objects().PositionObject(102, 0.0f, -150.0f, ReleasedZ + 100.0f);
    CHECK(Kernel.Objects().GetObject(102)->PosZ == doctest::Approx(ReleasedZ));

    Kernel.Objects().MakeObjectSphere(21, 100.0f, 12, 16);
    Kernel.Objects().MakeObjectSphere(103, 150.0f, 12, 16);
    CHECK(Kernel.Objects().GetObject(21)->CollisionShapeValue == dbc::CollisionShape::Polygon);
    CHECK(Kernel.Objects().GetObject(103)->CollisionShapeValue == dbc::CollisionShape::Polygon);

    Kernel.Objects().PositionObject(21, 0.0f, 0.0f, 0.0f);
    Kernel.Objects().PositionObject(102, 150.0f, -300.0f, 0.0f);
    Kernel.Objects().PositionObject(103, 25.0f, -50.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(21, 103) == 1);
    CHECK(Kernel.Objects().ObjectCollision(21, 0) == 103);
    CHECK(Kernel.Objects().GetObjectCollisionX(21) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionY(21) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionZ(21) == doctest::Approx(0.0f));

    Kernel.Objects().PositionObject(21, 0.0f, -250.0f, 0.0f);
    Kernel.Objects().PositionObject(103, 2000.0f, -50.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(21, 102) == 1);
    for (int Query = 0; Query < 8; ++Query)
        CHECK(Kernel.Objects().ObjectCollision(21, 0) == 102);
    CHECK(Kernel.Objects().GetObjectCollisionX(21) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionY(21) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionZ(21) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().ObjectHit(21, 0) == 102);
    CHECK(Kernel.Objects().ObjectHit(21, 0) == 0);

    Kernel.Objects().PositionObject(102, 1000.0f, -300.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(21, 102) == 0);
    CHECK(Kernel.Objects().ObjectHit(21, 0) == 0);
    Kernel.Objects().PositionObject(102, 150.0f, -300.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectHit(21, 0) == 102);
}

TEST_CASE("Imported DBPro limb identifiers remain stable after removal")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("imported-limbs", 16, 16));
    REQUIRE(Kernel.Objects().LoadObject(
        (std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/basic3d/models/ninja.X").string(), 1));

    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    REQUIRE(Object->LimbNodes.size() == 17);
    CHECK(Kernel.Objects().LimbExist(1, 16) == 1);

    REQUIRE(Kernel.Objects().RemoveLimb(1, 11));
    CHECK(Kernel.Objects().LimbExist(1, 11) == 0);
    CHECK(Kernel.Objects().LimbExist(1, 16) == 1);
    REQUIRE(Kernel.Objects().RotateLimb(1, 16, 0.0f, 45.0f, 0.0f));

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForObjectLimbs,
                          {BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(Context.Checklist.size() == 17);
    CHECK(Context.Checklist[11].Name.empty());
    CHECK(Context.Checklist[16].Name == "x3ds_Head");
}

TEST_CASE("Clones, instances and objects made from a mesh share geometry until one of them edits it")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("shared-geometry", 16, 16));
    dbc::ObjectManager& Objects = Kernel.Objects();
    Objects.MakeObjectCube(1, 2.0f);
    Objects.CloneObject(2, 1);
    Objects.InstanceObject(3, 1);
    const auto MeshOf = [&](int Id) -> const dbc::Mesh&
    {
        const dbc::Object3D* Object = Objects.GetObject(Id);
        REQUIRE(Object != nullptr);
        REQUIRE(Object->Meshes.size() == 1);
        return Object->Meshes.front();
    };
    CHECK(MeshOf(2).SharesGeometryWith(MeshOf(1)));
    CHECK(MeshOf(3).SharesGeometryWith(MeshOf(1)));
    const uint64_t Revision = MeshOf(1).Geometry().Revision;
    const dbc::Vec3 Corner = MeshOf(1).Geometry().Vertices.front();

    // Editing the source detaches it: a new revision, and the clone and the instance keep the old vertex.
    REQUIRE(Objects.LockVertexdataForLimb(1, 0));
    REQUIRE(Objects.SetVertexdataPosition(0, 9.0f, 9.0f, 9.0f));
    Objects.UnlockVertexdata();
    CHECK(MeshOf(1).Geometry().Vertices.front().X == 9.0f);
    CHECK(MeshOf(1).Geometry().Revision != Revision);
    CHECK_FALSE(MeshOf(1).SharesGeometryWith(MeshOf(3)));
    CHECK(MeshOf(2).SharesGeometryWith(MeshOf(3)));
    CHECK(MeshOf(3).Geometry().Vertices.front().X == Corner.X);
    CHECK(MeshOf(3).Geometry().Revision == Revision);

    // An object made from a stored mesh shares it, and editing the stored mesh leaves the object as it was.
    REQUIRE(Objects.MakeMeshFromObject(1, 2));
    Objects.MakeObject(4, 1, 0);
    const std::vector<dbc::Mesh>* Stored = Objects.GetMeshes(1);
    REQUIRE(Stored != nullptr);
    CHECK(MeshOf(4).SharesGeometryWith(Stored->front()));
    REQUIRE(Objects.LockVertexdataForMesh(1));
    REQUIRE(Objects.SetVertexdataPosition(0, -5.0f, 0.0f, 0.0f));
    Objects.UnlockVertexdata();
    CHECK(Objects.GetMeshes(1)->front().Geometry().Vertices.front().X == -5.0f);
    CHECK(MeshOf(4).Geometry().Vertices.front().X != -5.0f);
}

TEST_CASE("Batched world mesh vertices match the per-vertex query exactly")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("batched-vertices", 16, 16));
    dbc::ObjectManager& Objects = Kernel.Objects();
    // A skinned, posed, rotated and scaled character, and a cube glued to it.
    REQUIRE(Objects.LoadObject(
        (std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/camera/miko/miko.X").string(), 1));
    REQUIRE(Objects.SetObjectFrame(1, 23.5f));
    Objects.RotateObject(1, 12.0f, 37.0f, -8.0f);
    Objects.ScaleObject(1, 90.0f, 110.0f, 100.0f);
    Objects.PositionObject(1, 3.0f, -2.0f, 40.0f);
    Objects.MakeObjectCube(2, 1.0f);
    Objects.RotateObject(2, 0.0f, 30.0f, 0.0f);
    REQUIRE(Objects.GlueObjectToLimb(2, 1, 3));

    std::size_t Compared = 0;
    std::size_t Mismatched = 0;
    for (const int Id : {1, 2})
    {
        const dbc::Object3D* Object = Objects.GetObject(Id);
        REQUIRE(Object != nullptr);
        for (const dbc::Mesh& MeshValue : Object->Meshes)
        {
            std::vector<dbc::Vec3> Batched;
            Kernel.Renderer().WorldMeshVertices(*Object, MeshValue, Batched);
            REQUIRE(Batched.size() == MeshValue.Geometry().Vertices.size());
            for (std::size_t Vertex = 0; Vertex < Batched.size(); ++Vertex)
            {
                const dbc::Vec3 Single = Kernel.Renderer().WorldMeshVertex(*Object, MeshValue, Vertex);
                ++Compared;
                if (Batched[Vertex].X != Single.X || Batched[Vertex].Y != Single.Y || Batched[Vertex].Z != Single.Z)
                    ++Mismatched;
            }
        }
    }
    CHECK(Compared > 1000);
    CHECK(Mismatched == 0);
}

TEST_CASE("Imported DBPro skeletal animation deforms weighted vertices")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("imported-skinning", 16, 16));
    REQUIRE(Kernel.Objects().LoadObject(
        (std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/camera/miko/miko.X").string(), 1));

    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    REQUIRE_FALSE(Object->AnimationClips.empty());
    const auto MeshIt = std::find_if(Object->Meshes.begin(), Object->Meshes.end(),
                                     [](const dbc::Mesh& Mesh) { return !Mesh.Geometry().BoneInfluences.empty(); });
    REQUIRE(MeshIt != Object->Meshes.end());
    const dbc::Mesh& Mesh = *MeshIt;
    REQUIRE(Mesh.Geometry().BoneInfluences.size() == Mesh.Geometry().Vertices.size());
    REQUIRE(Mesh.Geometry().BoneNodes.size() == Mesh.Geometry().BoneOffsets.size());

    std::size_t AnimatedVertex = Mesh.Geometry().Vertices.size();
    for (std::size_t Vertex = 0; Vertex < Mesh.Geometry().BoneInfluences.size(); ++Vertex)
    {
        const dbc::BoneInfluence& Influence = Mesh.Geometry().BoneInfluences[Vertex];
        float Total = 0.0f;
        int Count = 0;
        bool Animated = false;
        for (std::size_t Slot = 0; Slot < Influence.Weights.size(); ++Slot)
        {
            if (Influence.Weights[Slot] <= 0.0f)
                continue;
            ++Count;
            Total += Influence.Weights[Slot];
            REQUIRE(Influence.Indices[Slot] >= 0);
            REQUIRE(static_cast<std::size_t>(Influence.Indices[Slot]) < Mesh.Geometry().BoneNodes.size());
            const aiNode* Node = Mesh.Geometry().BoneNodes[static_cast<std::size_t>(Influence.Indices[Slot])];
            Animated = Animated || (Node && Object->AnimChannelByNode.contains(Node->mName.C_Str()));
        }
        CHECK(Count <= 4);
        if (Count > 0)
            CHECK(Total == doctest::Approx(1.0f));
        if (AnimatedVertex == Mesh.Geometry().Vertices.size() && Animated)
            AnimatedVertex = Vertex;
    }
    REQUIRE(AnimatedVertex < Mesh.Geometry().Vertices.size());

    REQUIRE(Kernel.Objects().SetObjectFrame(1, 0));
    const dbc::Vec3 BindPoint = Kernel.Renderer().WorldMeshVertex(*Object, Mesh, AnimatedVertex);
    const int EndFrame = std::max(1, Object->AnimationClips.front().EndFrame);
    bool Moved = false;
    for (int Frame : {std::max(1, EndFrame / 4), std::max(1, EndFrame / 2), EndFrame})
    {
        REQUIRE(Kernel.Objects().SetObjectFrame(1, Frame));
        const dbc::Vec3 Point = Kernel.Renderer().WorldMeshVertex(*Object, Mesh, AnimatedVertex);
        const dbc::Vec3 Delta = dbc::Vec3Sub(Point, BindPoint);
        Moved = Moved || dbc::Vec3Dot(Delta, Delta) > 0.000001f;
    }
    CHECK(Moved);
}

TEST_CASE("Historical DDS and TGA assets decode to stable RGBA pixels")
{
    struct AssetExpectation
    {
        const char* Path;
        int Width;
        int Height;
        uint64_t Fnv1a;
    };

    const std::array<AssetExpectation, 5> Assets = {
        AssetExpectation{"examples/dbpro/tutorials/bitmap/2DFiles/logo.dds", 320, 240, UINT64_C(0x0e8c93894e3b976f)},
        AssetExpectation{"examples/dbpro/tutorials/basic3d/edge.dds", 128, 1, UINT64_C(0xbe78bcdbd952dd25)},
        AssetExpectation{"examples/dbpro/tutorials/world/sky/sb1.DDS", 256, 256, UINT64_C(0x6fd8739a5b726975)},
        AssetExpectation{"examples/dbpro/tutorials/basic3d/models/ninja.DDS", 256, 256, UINT64_C(0x160efb16beef2a86)},
        AssetExpectation{"examples/dbpro/tutorials/bitmap/2DFiles/logo.tga", 320, 240, UINT64_C(0x3d94287d0dd997c5)},
    };

    for (const AssetExpectation& Asset : Assets)
    {
        CAPTURE(std::string(Asset.Path));
        const std::filesystem::path Path = std::filesystem::path(TEST_SOURCE_DIR) / Asset.Path;
        SDL_Surface* Loaded = dbc::LoadSurface(Path.string());
        REQUIRE(Loaded != nullptr);
        CHECK(Loaded->w == Asset.Width);
        CHECK(Loaded->h == Asset.Height);
        SDL_Surface* Rgba = SDL_ConvertSurface(Loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(Loaded);
        REQUIRE(Rgba != nullptr);
        REQUIRE(SDL_LockSurface(Rgba));
        uint64_t Hash = UINT64_C(14695981039346656037);
        for (int Y = 0; Y < Rgba->h; ++Y)
        {
            const auto* Row = static_cast<const uint8_t*>(Rgba->pixels) + static_cast<size_t>(Y) * Rgba->pitch;
            for (int X = 0; X < Rgba->w * 4; ++X)
            {
                Hash ^= Row[X];
                Hash *= UINT64_C(1099511628211);
            }
        }
        SDL_UnlockSurface(Rgba);
        SDL_DestroySurface(Rgba);
        CHECK(Hash == Asset.Fnv1a);
    }
}

TEST_CASE("DDS decoding accepts legacy DXT alpha blocks and rejects truncated payloads")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-dds-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));
    const auto WriteU32 = [](std::array<uint8_t, 144>& Bytes, size_t Offset, uint32_t Value)
    {
        for (int Byte = 0; Byte < 4; ++Byte)
            Bytes[Offset + Byte] = static_cast<uint8_t>(Value >> (Byte * 8));
    };
    const auto MakeDds = [&](const std::filesystem::path& Path, uint32_t FourCc, bool IncludePayload)
    {
        std::array<uint8_t, 144> Bytes{};
        WriteU32(Bytes, 0, UINT32_C(0x20534444));
        WriteU32(Bytes, 4, 124);
        WriteU32(Bytes, 8, UINT32_C(0x00081007));
        WriteU32(Bytes, 12, 4);
        WriteU32(Bytes, 16, 4);
        WriteU32(Bytes, 20, 16);
        WriteU32(Bytes, 76, 32);
        WriteU32(Bytes, 80, 4);
        WriteU32(Bytes, 84, FourCc);
        WriteU32(Bytes, 108, UINT32_C(0x00001000));
        if (IncludePayload)
        {
            if (FourCc == UINT32_C(0x33545844))
                std::fill(Bytes.begin() + 128, Bytes.begin() + 136, 0xff);
            else
                Bytes[128] = 255;
            Bytes[136] = 0x00;
            Bytes[137] = 0xf8;
            Bytes[138] = 0xe0;
            Bytes[139] = 0x07;
        }
        std::ofstream File(Path, std::ios::binary);
        File.write(reinterpret_cast<const char*>(Bytes.data()), IncludePayload ? 144 : 128);
    };

    for (const auto& [Name, FourCc] : std::array<std::pair<const char*, uint32_t>, 2>{
             std::pair{"alpha-dxt3.dds", UINT32_C(0x33545844)}, std::pair{"alpha-dxt5.dds", UINT32_C(0x35545844)}})
    {
        const std::filesystem::path Path = Root / Name;
        MakeDds(Path, FourCc, true);
        SDL_Surface* Surface = dbc::LoadSurface(Path.string());
        REQUIRE(Surface != nullptr);
        CHECK(Surface->w == 4);
        CHECK(Surface->h == 4);
        REQUIRE(SDL_LockSurface(Surface));
        const auto* Pixels = static_cast<const uint8_t*>(Surface->pixels);
        CHECK(Pixels[0] == 255);
        CHECK(Pixels[1] == 0);
        CHECK(Pixels[2] == 0);
        CHECK(Pixels[3] == 255);
        CHECK(Pixels[Surface->pitch * 3 + 15] == 255);
        SDL_UnlockSurface(Surface);
        SDL_DestroySurface(Surface);
    }

    const std::filesystem::path Truncated = Root / "truncated.dds";
    MakeDds(Truncated, UINT32_C(0x31545844), false);
    CHECK(dbc::LoadSurface(Truncated.string()) == nullptr);
    CHECK(std::string(SDL_GetError()).find("truncated mip payload") != std::string::npos);
    std::filesystem::remove_all(Root);
}

TEST_CASE("DBPro bitmap source containers load through the shared surface path")
{
    const std::filesystem::path Root =
        std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/bitmap/2DFiles";
    for (const char* Name : {"logo.dds", "logo.rle", "logo.tga", "logo.bmp", "logo.dib", "logo.jpg", "logo.png"})
    {
        CAPTURE(std::string(Name));
        SDL_Surface* Surface = dbc::LoadSurface((Root / Name).string());
        REQUIRE(Surface != nullptr);
        CHECK(Surface->w == 320);
        CHECK(Surface->h == 240);
        SDL_DestroySurface(Surface);
    }
}

TEST_CASE("DBPro ODE rigid bodies simulate through the portable Jolt backend")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    Kernel.Objects().MakeObjectBox(1, 40.0f, 2.0f, 40.0f);
    Kernel.Objects().PositionObject(1, 0.0f, -1.0f, 0.0f);
    Kernel.Objects().MakeObjectSphere(2, 1.0f, 8, 8);
    Kernel.Objects().PositionObject(2, 0.0f, 8.0f, 0.0f);
    Kernel.Objects().MakeObjectSphere(3, 2.0f, 8, 8);
    Kernel.Objects().PositionObject(3, 100.0f, 0.0f, 0.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, int ObjectId)
    {
        Error.clear();
        BuiltinValue Result;
        REQUIRE(InvokeFunction(Context, Symbol, {BuiltinValue::FromNumber(ObjectId)}, Result, Error));
        return static_cast<float>(Result.AsNumber());
    };

    CHECK_FALSE(Command(dbc::language::SymbolId::Command_ExtOdeUpdate));
    CHECK(Error.find("ODE START") != std::string::npos);
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeStart));
    CHECK(Kernel.Physics().IsStarted());
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeCreateStaticBox, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeCreateStaticSphere, {BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeCreateDynamicSphere, {BuiltinValue::FromNumber(2)}));
    CHECK(Function(dbc::language::SymbolId::Function_ExtOdeGetBodyHeight, 2) == doctest::Approx(1.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetBodyFriction,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0.75)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetBodyPosition,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7),
                     BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetBodyRotation,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                     BuiltinValue::FromNumber(30)}));
    CHECK(Kernel.Objects().ObjectPositionX(2) == doctest::Approx(1.0f));
    CHECK(Kernel.Objects().ObjectAngleY(2) == doctest::Approx(20.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetGravity,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetLinearVelocity,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                     BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetAngularVelocity,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                     BuiltinValue::FromNumber(0)}));
    CHECK(Function(dbc::language::SymbolId::Function_ExtOdeGetBodyLinearVelocityX, 2) == doctest::Approx(1.0f));
    CHECK(Function(dbc::language::SymbolId::Function_ExtOdeGetBodyLinearVelocityY, 2) == doctest::Approx(2.0f));
    CHECK(Function(dbc::language::SymbolId::Function_ExtOdeGetBodyLinearVelocityZ, 2) == doctest::Approx(3.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeUpdate, {BuiltinValue::FromNumber(0.25)}));
    CHECK(Kernel.Objects().ObjectPositionX(2) > 1.2f);
    CHECK(Kernel.Objects().ObjectPositionY(2) > 7.4f);

    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetEnabled,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0)}));
    const float DisabledY = Kernel.Objects().ObjectPositionY(2);
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetLinearVelocity,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                     BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeUpdate));
    CHECK(Kernel.Objects().ObjectPositionY(2) > DisabledY);
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetEnabled,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetActive,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetActive,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));

    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetWorldGravity,
                    {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(-20), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetGravity,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeSetLinearVelocity,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    for (int Step = 0; Step < 60; ++Step)
        REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeUpdate));
    CHECK(Kernel.Objects().ObjectPositionY(2) >= 0.4f);
    CHECK(Kernel.Objects().ObjectPositionY(2) <= 0.7f);

    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeDestroyObject, {BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeDestroyObject, {BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ExtOdeEnd));
    CHECK_FALSE(Kernel.Physics().IsStarted());
}

TEST_CASE("DRAW TO BACK and DRAW TO FRONT place every 2D batch around the 3D world")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    Kernel.Objects().MakeObjectCube(1, 4.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().ColorObject(1, 0xFF0000);
    REQUIRE(Kernel.Core().EnsureInitialized());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto CaptureCenter = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        INFO("capture ", Capture->w, "x", Capture->h, " logical ", Kernel.Core().WindowWidth(), "x",
             Kernel.Core().WindowHeight());
        std::array<Uint8, 4> Pixel{};
        REQUIRE(
            SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };

    Context.Profile = dbc::language::LanguageProfile::Dbc;
    REQUIRE(Command(dbc::language::SymbolId::Command_Ink,
                    {BuiltinValue::FromNumber(0x00FF00), BuiltinValue::FromNumber(0)}));
    const auto QueueGreenScreen = [&]()
    {
        return Command(dbc::language::SymbolId::Command_Box,
                       {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                        BuiltinValue::FromNumber(Kernel.Core().WindowWidth() - 1),
                        BuiltinValue::FromNumber(Kernel.Core().WindowHeight() - 1)});
    };
    Kernel.Objects().HideObject(1);
    REQUIRE(QueueGreenScreen());
    const auto OverlayOnly = CaptureCenter();
    INFO("overlay pixel ", OverlayOnly[0], ",", OverlayOnly[1], ",", OverlayOnly[2]);
    CHECK(OverlayOnly[0] <= 16);
    CHECK(OverlayOnly[1] >= 240);
    CHECK(OverlayOnly[2] <= 16);
    const auto PersistentOverlay = CaptureCenter();
    CHECK(PersistentOverlay[0] <= 16);
    CHECK(PersistentOverlay[1] >= 240);
    CHECK(PersistentOverlay[2] <= 16);
    Kernel.Draw().ClearPrimitives2D();
    Kernel.Objects().ShowObject(1);

    REQUIRE(Command(dbc::language::SymbolId::Command_DrawToFront));
    CHECK(Kernel.Draw().IsDrawToFront());
    REQUIRE(QueueGreenScreen());
    CHECK(Kernel.Gpu().SurfacePipeline(false, false, false, false, true, false) != nullptr);
    CHECK(Kernel.Gpu().SurfacePipeline(true, false, false, false, true, false) != nullptr);
    const auto Front = CaptureCenter();
    INFO("front pixel ", Front[0], ",", Front[1], ",", Front[2], " error=", Kernel.Debug().LastRenderError());
    CHECK(Front[0] <= 16);
    CHECK(Front[1] >= 240);
    CHECK(Front[2] <= 16);

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(Command(dbc::language::SymbolId::Command_DrawToBack));
    CHECK_FALSE(Kernel.Draw().IsDrawToFront());
    REQUIRE(QueueGreenScreen());
    const auto Back = CaptureCenter();
    CHECK(Back[0] >= 240);
    CHECK(Back[1] <= 16);
    CHECK(Back[2] <= 16);

    REQUIRE(Command(dbc::language::SymbolId::Command_DrawToFront));
    CHECK(Kernel.Draw().IsDrawToFront());
}

TEST_CASE("screen 2D commands preserve source order")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("screen-2d", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.Draw().SetInk(0x00FF00);
    Kernel.Draw().DrawBox(0, 0, 31, 31);

    SDL_Surface* Blue = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Blue != nullptr);
    REQUIRE(SDL_WriteSurfacePixel(Blue, 0, 0, 0, 0, 255, 255));
    Kernel.ImageStoreRef().StoreImageFromSurface(1, Blue);
    SDL_DestroySurface(Blue);
    Kernel.Draw().SetInk(0xFF0000);
    Kernel.Draw().DrawBox(14, 14, 18, 18);
    Kernel.ImageStoreRef().PasteImage(1, 16, 16);
    Kernel.Draw().SetInk(0x00FF00);
    Kernel.Draw().DrawDot(16, 16);

    SDL_Surface* Screen = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(Screen != nullptr);
    std::array<Uint8, 4> Pixel{};
    REQUIRE(SDL_ReadSurfacePixel(Screen, 16, 16, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[0] == 0);
    CHECK(Pixel[1] == 255);
    CHECK(Pixel[2] == 0);
}

TEST_CASE("Screen drawing lasts one frame once a 3D scene is shown")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("screen-2d-over-3d", 32, 32));
    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    const int Width = Kernel.Core().WindowWidth();
    const int Height = Kernel.Core().WindowHeight();
    const auto ScreenPixel = [&](int X, int Y)
    {
        std::array<Uint8, 4> Pixel{};
        SDL_Surface* Screen = Kernel.ImageStoreRef().GetBitmapSurface(0);
        REQUIRE(Screen != nullptr);
        REQUIRE(SDL_ReadSurfacePixel(Screen, X, Y, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        return Pixel;
    };

    // A 2D-only program keeps its screen across SYNC.
    Kernel.Draw().SetInk(0xFF0000);
    Kernel.Draw().DrawBox(0, 0, 3, 3);
    Kernel.Renderer().RenderScene();
    CHECK_FALSE(Kernel.Renderer().LastFrameShowedWorld());
    CHECK(ScreenPixel(1, 1)[3] == 255);

    // Once a 3D scene is shown, the backdrop repaints the screen at every SYNC: earlier drawing must not
    // pile up under later frames.
    Kernel.Objects().MakeObjectCube(1, 4.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().ColorObject(1, 0xFF8000);
    Kernel.Draw().SetInk(0x00FF00);
    Kernel.Draw().DrawBox(Width - 4, Height - 4, Width - 1, Height - 1);
    Kernel.Renderer().RenderScene();
    CHECK(Kernel.Renderer().LastFrameShowedWorld());
    CHECK(ScreenPixel(1, 1)[3] == 0);
    CHECK(ScreenPixel(Width - 2, Height - 2)[3] == 0);

    // BACKDROP OFF keeps the same lifetime: per-frame text such as an FPS counter must not stack.
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().DisableBackdrop(&Error); });
    Kernel.Draw().DrawBox(0, 0, 3, 3);
    Kernel.Renderer().RenderScene();
    CHECK(Kernel.Renderer().LastFrameShowedWorld());
    CHECK(ScreenPixel(1, 1)[3] == 0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().EnableBackdrop(&Error); });

    // A loading screen has no alpha channel; the 3D frame must still clear it rather than paint it black.
    const std::filesystem::path LoadingBitmap =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp/loading.bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadBitmapFile(LoadingBitmap.string(), 0));
    Kernel.Renderer().RenderScene();
    CHECK(Kernel.Renderer().LastFrameShowedWorld());
    CHECK(ScreenPixel(Width / 2, Height / 2)[3] == 0);

    // GET IMAGE from the screen reads the composed frame, not the 2D layer alone.
    Kernel.Draw().DrawBox(Width - 4, Height - 4, Width - 1, Height - 1);
    REQUIRE(Kernel.ImageStoreRef().GetImage(1, 0, 0, Width, Height));
    const dbc::Image* Grabbed = Kernel.ImageStoreRef().GetImage(1);
    REQUIRE(Grabbed != nullptr);
    REQUIRE(Grabbed->Surface != nullptr);
    std::array<Uint8, 4> Centre{};
    REQUIRE(
        SDL_ReadSurfacePixel(Grabbed->Surface, Width / 2, Height / 2, &Centre[0], &Centre[1], &Centre[2], &Centre[3]));
    CHECK(Centre[3] == 255);
    CHECK(Centre[0] > 200);
    CHECK(Centre[2] < 64);
    std::array<Uint8, 4> Corner{};
    REQUIRE(
        SDL_ReadSurfacePixel(Grabbed->Surface, Width - 2, Height - 2, &Corner[0], &Corner[1], &Corner[2], &Corner[3]));
    CHECK(Corner[1] == 255);
    CHECK(Corner[0] == 0);
}

TEST_CASE("LOAD IMAGE applies the configured color key to opaque images")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("image-color-key", 16, 16));
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Path =
        std::filesystem::temp_directory_path() / ("opendbc-color-key-" + std::to_string(Suffix) + ".bmp");
    const std::filesystem::path OneToOnePath =
        std::filesystem::temp_directory_path() / ("opendbc-color-key-" + std::to_string(Suffix) + ".png");
    SDL_Surface* Source = SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Source != nullptr);
    REQUIRE(SDL_WriteSurfacePixel(Source, 0, 0, 0, 0, 0, 255));
    REQUIRE(SDL_WriteSurfacePixel(Source, 1, 0, 255, 0, 0, 255));
    REQUIRE(SDL_SaveBMP(Source, Path.string().c_str()));
    REQUIRE(SDL_SaveBMP(Source, OneToOnePath.string().c_str()));
    SDL_DestroySurface(Source);

    REQUIRE(Kernel.ImageStoreRef().LoadImageFile(Path.string(), 1));
    const dbc::Image* DefaultKey = Kernel.ImageStoreRef().GetImage(1);
    REQUIRE(DefaultKey != nullptr);
    REQUIRE(DefaultKey->Surface != nullptr);
    std::array<Uint8, 4> Pixel{};
    REQUIRE(SDL_ReadSurfacePixel(DefaultKey->Surface, 0, 0, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[3] == 0);
    REQUIRE(SDL_ReadSurfacePixel(DefaultKey->Surface, 1, 0, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[3] == 255);
    CHECK(DefaultKey->HasAlpha);

    Kernel.ImageStoreRef().SetImageColorKey(255, 0, 0);
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile(Path.string(), 2));
    const dbc::Image* RedKey = Kernel.ImageStoreRef().GetImage(2);
    REQUIRE(RedKey != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(RedKey->Surface, 0, 0, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[3] == 255);
    REQUIRE(SDL_ReadSurfacePixel(RedKey->Surface, 1, 0, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[3] == 0);

    Kernel.ImageStoreRef().SetImageColorKey(0, 0, 0);
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile(OneToOnePath.string(), 3, 1));
    const dbc::Image* OneToOne = Kernel.ImageStoreRef().GetImage(3);
    REQUIRE(OneToOne != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(OneToOne->Surface, 0, 0, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[3] == 255);

    std::error_code Ignored;
    std::filesystem::remove(Path, Ignored);
    std::filesystem::remove(OneToOnePath, Ignored);
}

TEST_CASE("Classic Basic3D exam22 LOAD IMAGE modes remain resource hints")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-exam22-image-modes", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    std::string Error;

    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadImage,
        {BuiltinValue::FromString("brick.bmp"), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadImage,
        {BuiltinValue::FromString("brick.bmp"), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(2)}, Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadImage,
        {BuiltinValue::FromString("brick.dds"), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadImage,
        {BuiltinValue::FromString("brick.dds"), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(2)}, Error));
    for (int ImageId = 1; ImageId <= 4; ++ImageId)
    {
        CAPTURE(ImageId);
        const dbc::Image* Image = Kernel.ImageStoreRef().GetImage(ImageId);
        REQUIRE(Image != nullptr);
        REQUIRE(Image->Surface != nullptr);
        CHECK(Image->Surface->w == 128);
        CHECK(Image->Surface->h == 128);
    }

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadImage,
        {BuiltinValue::FromString("brick.dds"), BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(1)}, Error));
    Error.clear();
    CHECK_FALSE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadImage,
        {BuiltinValue::FromString("brick.dds"), BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(2)}, Error));
    CHECK(Error.find("cube and volume texture flags are not available") != std::string::npos);
    CHECK(Kernel.ImageStoreRef().ImageExist(6) == 0);
}

TEST_CASE("DBPro planar reflection shading uses the object as a stencil mirror")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 96, 96));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 5.0f, -15.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 5.0f, &Error); });
    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 20.0f, 20.0f));
    Kernel.Objects().RotateObject(1, 270.0f, 0.0f, 0.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().ColorObject(1, 0x202020);
    Kernel.Objects().MakeObjectCube(2, 3.0f);
    Kernel.Objects().PositionObject(2, 0.0f, 4.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(2, 0));
    Kernel.Objects().ColorObject(2, 0xFF0000);
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE(Kernel.Gpu().SupportsStencil());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, {BuiltinValue::FromNumber(1)}, Error);
    };
    const auto RedPixelCount = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        int Count = 0;
        for (int Y = 0; Y < Capture->h; ++Y)
        {
            for (int X = 0; X < Capture->w; ++X)
            {
                Uint8 Red = 0;
                Uint8 Green = 0;
                Uint8 Blue = 0;
                Uint8 Alpha = 0;
                if (!SDL_ReadSurfacePixel(Capture, X, Y, &Red, &Green, &Blue, &Alpha))
                {
                    SDL_DestroySurface(Capture);
                    FAIL("reflection capture pixel read failed");
                }
                if (Red > 96 && Red > Green * 2 && Red > Blue * 2)
                    ++Count;
            }
        }
        SDL_DestroySurface(Capture);
        return Count;
    };

    const int WithoutReflection = RedPixelCount();
    REQUIRE(Command(dbc::language::SymbolId::Command_SetReflectionShadingOn));
    REQUIRE(Kernel.Gpu().ReflectionMaskPipeline() != nullptr);
    const dbc::Object3D* Plane = Kernel.Objects().GetObject(1);
    REQUIRE(Plane != nullptr);
    CHECK(Plane->ReflectionShading);
    const int WithReflection = RedPixelCount();
    INFO("red pixels without reflection=", WithoutReflection, " with reflection=", WithReflection,
         " render error=", Kernel.Debug().LastRenderError());
    CHECK(WithReflection > WithoutReflection);
    CHECK_FALSE(Kernel.Renderer().CachedReflectedTris().empty());

    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadingOff));
    CHECK_FALSE(Plane->ReflectionShading);
    CHECK(RedPixelCount() == WithoutReflection);
}

TEST_CASE("DBPro shadow shading casts portable stencil volumes")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 128, 96));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 8.0f, -15.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 6.0f, &Error); });
    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 24.0f, 24.0f));
    Kernel.Objects().RotateObject(1, 270.0f, 0.0f, 0.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 6.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().ColorObject(1, 0xE0E0E0);
    Kernel.Objects().MakeObjectCube(2, 4.0f);
    Kernel.Objects().PositionObject(2, 0.0f, 2.1f, 6.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(2, 0));
    Kernel.Objects().ColorObject(2, 0xE0E0E0);
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE(Kernel.Gpu().SupportsStencil());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto CapturePixels = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::vector<std::array<Uint8, 4>> Pixels(static_cast<std::size_t>(Capture->w * Capture->h));
        for (int Y = 0; Y < Capture->h; ++Y)
        {
            for (int X = 0; X < Capture->w; ++X)
            {
                auto& Pixel = Pixels[static_cast<std::size_t>(Y * Capture->w + X)];
                if (!SDL_ReadSurfacePixel(Capture, X, Y, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]))
                {
                    SDL_DestroySurface(Capture);
                    FAIL("shadow capture pixel read failed");
                }
            }
        }
        SDL_DestroySurface(Capture);
        return Pixels;
    };
    const auto DarkenedPixelCount = [](const auto& Baseline, const auto& Shadowed)
    {
        REQUIRE(Baseline.size() == Shadowed.size());
        int Count = 0;
        for (std::size_t Index = 0; Index < Baseline.size(); ++Index)
        {
            if (static_cast<int>(Baseline[Index][0]) - static_cast<int>(Shadowed[Index][0]) > 20 &&
                static_cast<int>(Baseline[Index][1]) - static_cast<int>(Shadowed[Index][1]) > 20 &&
                static_cast<int>(Baseline[Index][2]) - static_cast<int>(Shadowed[Index][2]) > 20)
            {
                ++Count;
            }
        }
        return Count;
    };

    const auto Baseline = CapturePixels();
    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowPosition,
                    {BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(-7), BuiltinValue::FromNumber(10),
                     BuiltinValue::FromNumber(-3)}));
    CHECK(Kernel.Renderer().ShadowPositionMode() == -1);
    CHECK(Kernel.Renderer().ShadowPosition().X == doctest::Approx(-7.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowShadingOn, {BuiltinValue::FromNumber(2)}));
    REQUIRE(Kernel.Gpu().ShadowVolumePipeline() != nullptr);
    REQUIRE(Kernel.Gpu().ShadowOverlayPipeline() != nullptr);
    const dbc::Object3D* Cube = Kernel.Objects().GetObject(2);
    REQUIRE(Cube != nullptr);
    CHECK(Cube->ShadowShading);
    CHECK(Cube->ShadowConfigured);
    CHECK(Cube->ShadowMeshId == 0);
    const auto Shadowed = CapturePixels();
    const int Darkened = DarkenedPixelCount(Baseline, Shadowed);
    INFO("shadow-darkened pixels=", Darkened, " render error=", Kernel.Debug().LastRenderError());
    CHECK(Darkened > 20);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowShadingOff, {BuiltinValue::FromNumber(2)}));
    CHECK_FALSE(Cube->ShadowShading);
    CHECK_FALSE(Cube->ShadowConfigured);
    CHECK(CapturePixels() == Baseline);

    REQUIRE(Kernel.Objects().MakeMeshFromObject(7, 2));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowShadingOn,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(250),
                     BuiltinValue::FromNumber(1)}));
    CHECK(Cube->ShadowShading);
    CHECK(Cube->ShadowMeshId == 7);
    CHECK(Cube->ShadowRange == doctest::Approx(250.0f));
    CHECK(Cube->ShadowUseShader);
    CHECK_FALSE(Cube->ShadowMeshes.empty());
    CHECK(DarkenedPixelCount(Baseline, CapturePixels()) > 20);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadingOff, {BuiltinValue::FromNumber(2)}));
    CHECK_FALSE(Cube->ShadowShading);
    CHECK_FALSE(Cube->ShadowConfigured);
    CHECK(Cube->ShadowMeshes.empty());
    CHECK(CapturePixels() == Baseline);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetReflectionShadingOn, {BuiltinValue::FromNumber(1)}));
    const auto ReflectionOnly = CapturePixels();
    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowShadingOn, {BuiltinValue::FromNumber(2)}));
    const int CombinedDarkened = DarkenedPixelCount(ReflectionOnly, CapturePixels());
    INFO("reflection-plus-shadow darkened pixels=", CombinedDarkened,
         " render error=", Kernel.Debug().LastRenderError());
    CHECK(CombinedDarkened > 20);
}

TEST_CASE("A DBPro shadow does not depend on how the camera sees its caster")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 128, 96));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 400.0f, 400.0f));
    Kernel.Objects().RotateObject(1, 270.0f, 0.0f, 0.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().ColorObject(1, 0xE0E0E0);
    // Lit from straight above, only the cube's top faces the light.
    Kernel.Objects().MakeObjectCube(2, 8.0f);
    Kernel.Objects().PositionObject(2, 0.0f, 20.0f, 0.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(2, 0));
    Kernel.Objects().ColorObject(2, 0xE0E0E0);
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE(Kernel.Gpu().SupportsStencil());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowPosition,
                    {BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(100),
                     BuiltinValue::FromNumber(0)}));
    const auto RedChannels = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::vector<int> Reds;
        Reds.reserve(static_cast<std::size_t>(Capture->w * Capture->h));
        for (int Y = 0; Y < Capture->h; ++Y)
            for (int X = 0; X < Capture->w; ++X)
            {
                Uint8 Red = 0, Green = 0, Blue = 0, Alpha = 0;
                if (!SDL_ReadSurfacePixel(Capture, X, Y, &Red, &Green, &Blue, &Alpha))
                    FAIL("shadow capture pixel read failed");
                Reds.push_back(Red);
            }
        SDL_DestroySurface(Capture);
        return Reds;
    };
    // How many pixels the shadow darkens from there.
    const auto Darkened = [&](float EyeX, float EyeY, float EyeZ, float AtX, float AtZ)
    {
        RequireEngineStatus([&](std::string& Status)
                            { return Kernel.Camera().PositionCamera(EyeX, EyeY, EyeZ, &Status); });
        RequireEngineStatus([&](std::string& Status) { return Kernel.Camera().PointCamera(AtX, 0.0f, AtZ, &Status); });
        REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowShadingOff, {BuiltinValue::FromNumber(2)}));
        const std::vector<int> Plain = RedChannels();
        REQUIRE(Command(dbc::language::SymbolId::Command_SetShadowShadingOn, {BuiltinValue::FromNumber(2)}));
        const std::vector<int> Shadowed = RedChannels();
        int Count = 0;
        for (std::size_t Index = 0; Index < Plain.size(); ++Index)
            Count += Plain[Index] - Shadowed[Index] > 20 ? 1 : 0;
        return Count;
    };

    // From above: the cube's top, the face that casts, is in sight.
    CHECK(Darkened(0.0f, 80.0f, -40.0f, 0.0f, 0.0f) > 100);
    // From low down the top turns away from the camera, though the cube is still in frame.
    CHECK(Darkened(-25.0f, 3.0f, -25.0f, 0.0f, 0.0f) > 100);
    // Close up, with the cube above the frame.
    CHECK(Darkened(0.0f, 8.0f, -8.0f, 0.0f, 0.0f) > 100);
    // Under the cube, looking down: all of it is behind the camera's near plane.
    CHECK(Darkened(0.0f, 10.0f, -0.1f, 0.0f, 0.0f) > 100);
}

TEST_CASE("Portable object visibility and GPU capability queries preserve DBPro semantics")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    Kernel.Objects().MakeObjectCube(1, 4.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    std::string Error;
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE_MESSAGE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error), Error);
        return Result.AsNumber();
    };

    const int Width = Kernel.Core().WindowWidth();
    const int Height = Kernel.Core().WindowHeight();
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    CHECK(Function(dbc::language::SymbolId::Function_ObjectScreenX, {BuiltinValue::FromNumber(1)}) == Width / 2);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectScreenY, {BuiltinValue::FromNumber(1)}) == Height / 2);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectInScreen, {BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_TnlAvailable) == 1.0);

    const int Left = Width / 10;
    const int Top = Height / 5;
    const int Right = Width * 7 / 10;
    const int Bottom = Height * 4 / 5;
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().SetCameraView(Left, Top, Right, Bottom, &Error); });
    CHECK(Function(dbc::language::SymbolId::Function_ObjectScreenX, {BuiltinValue::FromNumber(1)}) ==
          Left + (Right - Left) / 2);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectScreenY, {BuiltinValue::FromNumber(1)}) ==
          Top + (Bottom - Top) / 2);

    Kernel.Objects().PositionObject(1, 12.0f, 0.0f, 10.0f);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectInScreen, {BuiltinValue::FromNumber(1)}) == 1.0);
    Kernel.Objects().HideObject(1);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectInScreen, {BuiltinValue::FromNumber(1)}) == 1.0);
    Kernel.Objects().PositionObject(1, 100.0f, 0.0f, 10.0f);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectInScreen, {BuiltinValue::FromNumber(1)}) == 0.0);

    Kernel.Objects().MakeObjectBox(2, 12.0f, 18.0f, 470.0f);
    Kernel.Objects().PositionObject(2, 100.0f, 0.0f, 100.0f);
    const dbc::Object3D* ElongatedBox = Kernel.Objects().GetObject(2);
    REQUIRE(ElongatedBox != nullptr);
    CHECK(ElongatedBox->BoundingRadius == doctest::Approx(7.5f));
    CHECK(Function(dbc::language::SymbolId::Function_ObjectInScreen, {BuiltinValue::FromNumber(2)}) == 1.0);
    CHECK(ElongatedBox->RenderBoundingRadius == doctest::Approx(235.2488f));

    BuiltinValue MissingResult;
    Error.clear();
    CHECK_FALSE(InvokeFunction(Context, dbc::language::SymbolId::Function_ObjectScreenX, {BuiltinValue::FromNumber(99)},
                               MissingResult, Error));
    CHECK(Error.find("OBJECT SCREEN X requires existing object 99") != std::string::npos);

    Kernel.Objects().ShowObject(1);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    CHECK(Function(dbc::language::SymbolId::Function_ObjectScreenX, {BuiltinValue::FromNumber(1)}) ==
          Left + (Right - Left) / 2);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectScreenY, {BuiltinValue::FromNumber(1)}) ==
          Top + (Bottom - Top) / 2);
    CHECK(Function(dbc::language::SymbolId::Function_ObjectInScreen, {BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_TnlAvailable) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_GetMaximumVertexShaderVersion) == 3.0);
    CHECK(Function(dbc::language::SymbolId::Function_GetMaximumPixelShaderVersion) == 3.0);
    CHECK(Function(dbc::language::SymbolId::Function_Statistic, {BuiltinValue::FromNumber(1)}) ==
          Kernel.Renderer().LastFramePolygons());
    CHECK(Function(dbc::language::SymbolId::Function_Statistic, {BuiltinValue::FromNumber(5)}) ==
          Kernel.Renderer().LastFrameDrawCalls());
    CHECK(Function(dbc::language::SymbolId::Function_Statistic, {BuiltinValue::FromNumber(2)}) ==
          (Kernel.Gpu().SupportsStencil() ? 8.0 : 0.0));
    CHECK(Function(dbc::language::SymbolId::Function_Statistic, {BuiltinValue::FromNumber(10)}) == 0.0);
    CHECK(Function(dbc::language::SymbolId::Function_Statistic, {BuiltinValue::FromNumber(999)}) == 0.0);
    CHECK(Kernel.Renderer().LastFramePolygons() > 0);
    CHECK(Kernel.Renderer().LastFrameDrawCalls() > 0);
}

TEST_CASE("The screen layer keeps its GPU texture, uploads what changed and skips an empty layer")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    // A small cube in the middle makes every frame a 3D frame, which clears the screen layer.
    Kernel.Objects().MakeObjectCube(1, 1.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());
    dbc::ImageStore& Store = Kernel.ImageStoreRef();
    REQUIRE(Store.EnsureScreenBitmap() != nullptr);
    const int Width = Kernel.Core().WindowWidth();
    const int Height = Kernel.Core().WindowHeight();
    const auto Paint = [&](SDL_Rect Area, Uint8 Red, Uint8 Green, Uint8 Blue)
    {
        SDL_Surface* Screen = Store.GetBitmapSurface(0);
        REQUIRE(Screen != nullptr);
        REQUIRE(SDL_FillSurfaceRect(Screen, &Area, SDL_MapSurfaceRGBA(Screen, Red, Green, Blue, 255)));
        Store.InvalidateBitmapGpuTexture(0, &Area);
    };
    const auto ShownPixel = [&](int X, int Y)
    {
        std::array<Uint8, 4> Pixel{};
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        REQUIRE(SDL_ReadSurfacePixel(Capture, X, Y, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };

    Kernel.Renderer().RenderScene();
    CHECK(Kernel.Renderer().LastFrameShowedWorld());
    // Nothing drawn since the clear: no texture to draw and no batch for the layer.
    CHECK(Store.GetBitmapGpuTexture(0) == nullptr);
    Kernel.Renderer().RenderScene();
    const int EmptyLayerBatches = Kernel.Renderer().LastFrameDrawCalls();

    Paint(SDL_Rect{0, 0, 8, 8}, 255, 0, 0);
    SDL_GPUTexture* Texture = Store.GetBitmapGpuTexture(0);
    REQUIRE(Texture != nullptr);
    CHECK(ShownPixel(2, 2)[0] >= 240);
    Kernel.Renderer().RenderScene();
    CHECK(Kernel.Renderer().LastFrameDrawCalls() == EmptyLayerBatches + 1);

    // The next frame paints elsewhere: the same texture is updated, and the corner erased by the clear shows
    // the scene again instead of last frame's paint.
    Paint(SDL_Rect{Width - 8, Height - 8, 8, 8}, 0, 255, 0);
    CHECK(Store.GetBitmapGpuTexture(0) == Texture);
    const auto Erased = ShownPixel(2, 2);
    CHECK(Erased[0] <= 16);
    const auto Painted = ShownPixel(Width - 3, Height - 3);
    CHECK(Painted[1] >= 240);
    CHECK(Painted[0] <= 16);

    // More separate areas than the store keeps apart merge, and all of them still show, then clear.
    const auto Background = ShownPixel(4, Height / 2);
    Kernel.Renderer().RenderScene();
    for (int Index = 0; Index < 12; ++Index)
        Paint(SDL_Rect{10 + Index * 20, 40, 4, 4}, 0, 0, 255);
    for (int Index = 0; Index < 12; ++Index)
        CHECK(ShownPixel(11 + Index * 20, 41)[2] >= 240);
    CHECK(ShownPixel(Width - 3, Height - 3) == Background);
    Kernel.Renderer().RenderScene();
    Paint(SDL_Rect{0, 0, 2, 2}, 255, 0, 0);
    for (int Index = 0; Index < 12; ++Index)
        CHECK(ShownPixel(11 + Index * 20, 41) == Background);
}

TEST_CASE("Renderer frame stats count objects, culling and views without the debug overlay")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    // In view, behind the camera, and hidden.
    Kernel.Objects().MakeObjectCube(1, 1.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    Kernel.Objects().MakeObjectCube(2, 1.0f);
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, -10.0f);
    Kernel.Objects().MakeObjectCube(3, 1.0f);
    Kernel.Objects().HideObject(3);
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE_FALSE(Kernel.Debug().IsOverlayEnabled());

    const auto CheckCounts = [&]
    {
        const dbc::RendererFrameStats& Stats = Kernel.Renderer().LastFrameStats();
        CHECK(Stats.Views == 1);
        CHECK(Stats.Objects == 3);
        CHECK(Stats.VisibleObjects == 2);
        CHECK(Stats.CulledObjects == 1);
        CHECK(Stats.Triangles == Kernel.Renderer().LastFramePolygons());
        CHECK(Stats.Batches == Kernel.Renderer().LastFrameDrawCalls());
        CHECK(Stats.Triangles > 0);
        CHECK(Stats.Vertices >= Stats.Triangles * 3);
    };
    Kernel.Renderer().RenderScene();
    CheckCounts();
    // Nothing moved: the cached scene is drawn again and still reports its objects.
    Kernel.Renderer().RenderScene();
    CheckCounts();
    const dbc::RendererFrameStats Mean = Kernel.Renderer().MeanStepMilliseconds();
    CHECK(Mean.BuildMilliseconds > 0.0);
    CHECK(Mean.RecordMilliseconds > 0.0);
}

TEST_CASE("Objects locked to the screen cull their faces from the eye, wherever the camera is")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().LockObjectOn(1));
    Kernel.Objects().PositionObject(1, 1.5f, -1.0f, 6.0f);
    Kernel.Objects().RotateObject(1, 25.0f, 35.0f, 0.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());
    // The overlay triangles a camera position yields, as sorted screen corners.
    const auto Drawn = [&](float X, float Y, float Z)
    {
        RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(X, Y, Z, &Error); });
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        std::vector<std::array<float, 6>> Triangles;
        for (const dbc::Triangle& Value : Kernel.Renderer().CachedOverlayTris())
            Triangles.push_back(
                {Value.Pos[0][0], Value.Pos[0][1], Value.Pos[1][0], Value.Pos[1][1], Value.Pos[2][0], Value.Pos[2][1]});
        std::sort(Triangles.begin(), Triangles.end());
        return Triangles;
    };

    const auto AtOrigin = Drawn(0.0f, 0.0f, 0.0f);
    CHECK(AtOrigin.size() == 6);
    CHECK(Drawn(0.0f, 0.0f, 1000.0f) == AtOrigin);
    CHECK(Drawn(-700.0f, 300.0f, -50.0f) == AtOrigin);
}

TEST_CASE("CHANGE MESH redraws a still scene with the new limb mesh")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    Kernel.Objects().MakeObjectSphere(2, 2.0f, 12, 12);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(1, 2));
    Kernel.Objects().HideObject(2);
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto DrawnTriangles = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        return Kernel.Renderer().LastFrameStats().Triangles;
    };

    const int Cube = DrawnTriangles();
    REQUIRE(Kernel.Memblocks().ChangeMesh(1, 0, 1));
    CHECK(DrawnTriangles() > Cube);
}

TEST_CASE("SET OBJECT FRAME redraws a still scene in the new pose")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Objects().LoadObject(
        (std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/camera/miko/miko.X").string(), 1));
    const float Size = Kernel.Objects().ObjectSize(1);
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(0.0f, Size * 0.5f, -2.0f * Size, &Error); });
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto DrawnPositions = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        double Sum = 0.0;
        for (const dbc::Triangle& TriangleValue : Kernel.Renderer().CachedWorldTris())
            for (const auto& Corner : TriangleValue.Pos)
                Sum += Corner[0] + Corner[1] * 3.0 + Corner[2] * 7.0;
        return Sum;
    };

    REQUIRE(Kernel.Objects().SetObjectFrame(1, 5.0f));
    const double First = DrawnPositions();
    REQUIRE(Kernel.Objects().SetObjectFrame(1, 30.0f));
    CHECK(DrawnPositions() != First);
}

TEST_CASE("TEXTURE OBJECT refreshes a still scene")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "spark.bmp").string(), 2));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    Kernel.Objects().TextureObject(1, 1);
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto DrawnTexture = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        REQUIRE(Kernel.Renderer().CachedMeshDraws().size() == 1);
        return Kernel.Renderer().CachedMeshDraws().front().Texture;
    };

    const SDL_GPUTexture* First = DrawnTexture();
    Kernel.Objects().TextureObject(1, 2);
    CHECK(DrawnTexture() != First);
}

// The scene drawn with and without GPU meshes: the GPU draws and draw calls of the first frame, the pixels the
// second covers and those where the two differ beyond rounding.
struct GpuMeshComparison
{
    std::size_t GpuDraws = 0;
    int DrawCalls = 0;
    int Covered = 0;
    int Different = 0;
};

static GpuMeshComparison CompareGpuMeshFrames(dbc::EngineKernel& Kernel)
{
    const auto Capture = [&](bool GpuMeshes)
    {
        Kernel.Renderer().SetGpuMeshesEnabled(GpuMeshes);
        SDL_Surface* Frame = Kernel.Renderer().CaptureScreen();
        REQUIRE(Frame != nullptr);
        return Frame;
    };
    GpuMeshComparison Result;
    SDL_Surface* Gpu = Capture(true);
    Result.GpuDraws = Kernel.Renderer().CachedMeshDraws().size();
    Result.DrawCalls = Kernel.Renderer().LastFrameDrawCalls();
    SDL_Surface* Cpu = Capture(false);
    CHECK(Kernel.Renderer().CachedMeshDraws().empty());
    REQUIRE(Gpu->w == Cpu->w);
    REQUIRE(Gpu->h == Cpu->h);
    // The two paths transform and clip in different orders, so only rounding and edge pixels may differ.
    for (int Y = 0; Y < Gpu->h; ++Y)
    {
        for (int X = 0; X < Gpu->w; ++X)
        {
            Uint8 GpuPixel[4]{};
            Uint8 CpuPixel[4]{};
            REQUIRE(SDL_ReadSurfacePixel(Gpu, X, Y, &GpuPixel[0], &GpuPixel[1], &GpuPixel[2], &GpuPixel[3]));
            REQUIRE(SDL_ReadSurfacePixel(Cpu, X, Y, &CpuPixel[0], &CpuPixel[1], &CpuPixel[2], &CpuPixel[3]));
            Result.Covered += (CpuPixel[0] | CpuPixel[1] | CpuPixel[2]) != 0 ? 1 : 0;
            int Difference = 0;
            for (int Channel = 0; Channel < 3; ++Channel)
                Difference += std::abs(static_cast<int>(GpuPixel[Channel]) - static_cast<int>(CpuPixel[Channel]));
            Result.Different += Difference > 12 ? 1 : 0;
        }
    }
    SDL_DestroySurface(Cpu);
    SDL_DestroySurface(Gpu);
    return Result;
}

TEST_CASE("A shadow caster draws from GPU buffers and casts the same shadow")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(0.0f, 40.0f, -40.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 0.0f, &Error); });
    dbc::ObjectManager& Objects = Kernel.Objects();
    REQUIRE(Objects.MakeObjectPlain(1, 400.0f, 400.0f));
    Objects.RotateObject(1, 270.0f, 0.0f, 0.0f);
    REQUIRE(Objects.SetObjectLight(1, 0));
    Objects.ColorObject(1, 0xE0E0E0);
    Objects.MakeObjectCube(2, 8.0f);
    Objects.PositionObject(2, 0.0f, 20.0f, 0.0f);
    REQUIRE(Objects.SetObjectLight(2, 0));
    Objects.ColorObject(2, 0xE0E0E0);
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE(Kernel.Gpu().SupportsStencil());
    Kernel.Renderer().SetShadowPosition(-1, 0.0f, 100.0f, 0.0f);
    const auto Reds = [&]
    {
        SDL_Surface* Frame = Kernel.Renderer().CaptureScreen();
        REQUIRE(Frame != nullptr);
        std::vector<int> Values;
        Values.reserve(static_cast<std::size_t>(Frame->w * Frame->h));
        for (int Y = 0; Y < Frame->h; ++Y)
            for (int X = 0; X < Frame->w; ++X)
            {
                Uint8 Red = 0, Green = 0, Blue = 0, Alpha = 0;
                REQUIRE(SDL_ReadSurfacePixel(Frame, X, Y, &Red, &Green, &Blue, &Alpha));
                Values.push_back(Red);
            }
        SDL_DestroySurface(Frame);
        return Values;
    };
    const std::vector<int> Plain = Reds();

    REQUIRE(Objects.SetShadowShadingOn(2));
    const GpuMeshComparison Frames = CompareGpuMeshFrames(Kernel);
    CHECK(Frames.GpuDraws == 2);
    CHECK(Frames.Covered > 320 * 240 / 10);
    CHECK(Frames.Different * 200 < Frames.Covered);
    Kernel.Renderer().SetGpuMeshesEnabled(true);
    const std::vector<int> Shadowed = Reds();
    int Darkened = 0;
    for (std::size_t Index = 0; Index < Plain.size(); ++Index)
        Darkened += Plain[Index] - Shadowed[Index] > 20 ? 1 : 0;
    CHECK(Darkened > 100);
}

TEST_CASE("Meshes drawn from GPU buffers look as the CPU path draws them")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 1.0f, 6.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(6.0f, 4.0f, 0.0f, &Error); });
    dbc::ObjectManager& Objects = Kernel.Objects();
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    // A pivoted, ZYX-rotated, scaled and textured cube with a chain of scaled, rotated and offset sphere limbs.
    Objects.MakeObjectSphere(5, 1.5f, 10, 10);
    REQUIRE(Objects.MakeMeshFromObject(1, 5));
    Objects.DeleteObject(5);
    Objects.MakeObjectCube(1, 2.0f);
    Objects.RotateObject(1, 17.0f, 33.0f, -21.0f);
    REQUIRE(Objects.FixObjectPivot(1));
    Objects.RotateObject(1, 9.0f, -14.0f, 27.0f);
    REQUIRE(Objects.SetObjectRotationZyx(1));
    Objects.ScaleObject(1, 120.0f, 80.0f, 95.0f);
    Objects.PositionObject(1, -2.5f, 0.5f, 14.0f);
    Objects.TextureObject(1, 1);
    REQUIRE(Objects.ScaleObjectTexture(1, 2.0f, 3.0f));
    REQUIRE(Objects.ScrollObjectTexture(1, 0.25f, 0.5f));
    REQUIRE(Objects.AddLimb(1, 1, 1));
    REQUIRE(Objects.AddLimb(1, 2, 1));
    REQUIRE(Objects.OffsetLimb(1, 1, 1.5f, 0.0f, 0.0f));
    REQUIRE(Objects.RotateLimb(1, 1, 30.0f, 10.0f, 5.0f));
    REQUIRE(Objects.ScaleLimb(1, 2, 50.0f, 150.0f, 50.0f));
    REQUIRE(Objects.OffsetLimb(1, 2, 0.0f, 1.2f, 0.0f));
    REQUIRE(Objects.LinkLimb(1, 1, 2));
    // A coloured sphere with a diffuse material, mirrored on one axis.
    Objects.MakeObjectSphere(2, 3.0f, 16, 12);
    Objects.ColorObject(2, 0xC08040);
    REQUIRE(Objects.SetObjectDiffuse(2, 0xFF80FF80u));
    Objects.ScaleObject(2, -100.0f, 100.0f, 100.0f);
    Objects.PositionObject(2, 2.5f, 1.0f, 12.0f);
    // An unlit plain seen from behind, which only an unculled object shows.
    REQUIRE(Objects.MakeObjectPlain(3, 4.0f, 3.0f));
    REQUIRE(Objects.SetObjectLight(3, 0));
    REQUIRE(Objects.SetObjectCull(3, 0));
    Objects.ColorObject(3, 0x4060E0);
    Objects.RotateObject(3, 0.0f, 200.0f, 0.0f);
    Objects.PositionObject(3, 0.0f, -2.0f, 18.0f);
    // An imported model, placed by its node transforms.
    REQUIRE(Objects.LoadObject(
        (std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models/dome.x").string(), 4));
    const float DomeSize = std::max(Objects.ObjectSize(4), 0.001f);
    Objects.ScaleObject(4, 300.0f / DomeSize, 300.0f / DomeSize, 300.0f / DomeSize);
    Objects.PositionObject(4, 0.0f, 3.5f, 16.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());

    const GpuMeshComparison Frames = CompareGpuMeshFrames(Kernel);
    CHECK(Frames.GpuDraws == 6);
    CHECK(Frames.Covered > 640 * 480 / 10);
    CHECK(Frames.Different * 200 < Frames.Covered);
}

TEST_CASE("Objects that share a mesh, texture and surface draw as instances of one call")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 2.0f, -4.0f, &Error); });
    dbc::ObjectManager& Objects = Kernel.Objects();
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "spark.bmp").string(), 2));
    Objects.MakeObjectSphere(1, 1.5f, 12, 10);
    Objects.TextureObject(1, 1);
    // Clones and instances in a grid, each placed, turned, scaled and coloured its own way; the last one textured
    // differently.
    for (int Id = 2; Id <= 12; ++Id)
    {
        if (Id % 2 == 0)
            Objects.CloneObject(Id, 1);
        else
            Objects.InstanceObject(Id, 1);
        Objects.PositionObject(Id, static_cast<float>(Id % 4) * 2.0f - 3.0f, static_cast<float>(Id / 4) * 1.8f - 1.0f,
                               8.0f + static_cast<float>(Id % 3));
        Objects.RotateObject(Id, static_cast<float>(Id) * 17.0f, static_cast<float>(Id) * 29.0f, 0.0f);
        Objects.ScaleObject(Id, 60.0f + static_cast<float>(Id) * 5.0f, 100.0f, 100.0f);
        Objects.ColorObject(Id, 0x204060 * Id);
    }
    Objects.HideObject(1);
    Objects.TextureObject(12, 2);
    REQUIRE(Kernel.Core().EnsureInitialized());

    const GpuMeshComparison Frames = CompareGpuMeshFrames(Kernel);
    CHECK(Frames.GpuDraws == 11);
    // Ten instances in one call, the differently textured one in another.
    CHECK(Frames.DrawCalls == 2);
    CHECK(Frames.Covered > 640 * 480 / 40);
    CHECK(Frames.Different * 200 < Frames.Covered);
}

TEST_CASE("Identical MAKE OBJECT primitives share their geometry until one is edited")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    dbc::ObjectManager& Objects = Kernel.Objects();
    Objects.MakeObjectCube(1, 2.0f);
    Objects.MakeObjectCube(2, 2.0f);
    Objects.MakeObjectBox(3, 2.0f, 2.0f, 2.0f);
    Objects.MakeObjectCube(4, 3.0f);
    Objects.MakeObjectSphere(5, 1.5f, 12, 10);
    Objects.MakeObjectSphere(6, 1.5f, 12, 10);
    Objects.MakeObjectSphere(7, 1.5f, 12, 11);
    const auto Shares = [&](int A, int B)
    { return Objects.GetObject(A)->Meshes.front().SharesGeometryWith(Objects.GetObject(B)->Meshes.front()); };
    CHECK(Shares(1, 2));
    CHECK(Shares(1, 3));
    CHECK_FALSE(Shares(1, 4));
    CHECK(Shares(5, 6));
    CHECK_FALSE(Shares(5, 7));

    // An edit stays with the edited object, and later cubes share the geometry left unchanged.
    Objects.GetObject(2)->Meshes.front().EditGeometry().Vertices.front().X = 9.0f;
    CHECK_FALSE(Shares(1, 2));
    CHECK(Objects.GetObject(1)->Meshes.front().Geometry().Vertices.front().X == doctest::Approx(-1.0f));
    Objects.MakeObjectCube(8, 2.0f);
    CHECK(Shares(1, 8));

    // Objects made apart with the same parameters draw as instances of one call.
    for (const int Id : {2, 4, 5, 6, 7})
        Objects.HideObject(Id);
    Objects.PositionObject(1, -2.0f, 0.0f, 10.0f);
    Objects.PositionObject(3, 0.0f, 0.0f, 10.0f);
    Objects.PositionObject(8, 2.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedMeshDraws().size() == 3);
    CHECK(Kernel.Renderer().LastFrameDrawCalls() == 1);
}

TEST_CASE("Objects loaded from one model share its geometry until one is edited")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    dbc::ObjectManager& Objects = Kernel.Objects();
    const std::filesystem::path Root(TEST_SOURCE_DIR);
    const std::string Dome = (Root / "tests/fixtures/classic/models/dome.x").string();
    REQUIRE(Objects.LoadObject(Dome, 1));
    REQUIRE(Objects.LoadObject(Dome, 2));
    const auto Shares = [&](int A, int B)
    {
        const std::vector<dbc::Mesh>& First = Objects.GetObject(A)->Meshes;
        const std::vector<dbc::Mesh>& Second = Objects.GetObject(B)->Meshes;
        REQUIRE(First.size() == Second.size());
        REQUIRE_FALSE(First.empty());
        return std::all_of(First.begin(), First.end(), [&](const dbc::Mesh& Value)
                           { return Value.SharesGeometryWith(Second[static_cast<std::size_t>(&Value - &First[0])]); });
    };
    CHECK(Shares(1, 2));

    // Bones name the nodes of each load's own scene, so a skinned mesh keeps its geometry.
    const std::string Miko = (Root / "examples/dbpro/tutorials/camera/miko/miko.X").string();
    REQUIRE(Objects.LoadObject(Miko, 5));
    REQUIRE(Objects.LoadObject(Miko, 6));
    int Skinned = 0;
    for (std::size_t Index = 0; Index < Objects.GetObject(5)->Meshes.size(); ++Index)
    {
        const dbc::Mesh& First = Objects.GetObject(5)->Meshes[Index];
        if (First.Geometry().BoneNodes.empty())
            continue;
        ++Skinned;
        CHECK_FALSE(First.SharesGeometryWith(Objects.GetObject(6)->Meshes[Index]));
    }
    CHECK(Skinned > 0);
    Objects.HideObject(5);
    Objects.HideObject(6);

    // An edit stays with the edited object; a later load still shares the unedited geometry.
    Objects.GetObject(2)->Meshes.front().EditGeometry().Vertices.front().X += 1.0f;
    CHECK_FALSE(Shares(1, 2));
    REQUIRE(Objects.LoadObject(Dome, 3));
    CHECK(Shares(1, 3));

    // The loaded objects draw as instances.
    Objects.HideObject(2);
    Objects.PositionObject(1, -2.0f, 0.0f, 12.0f);
    Objects.PositionObject(3, 2.0f, 0.0f, 12.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedMeshDraws().size() == 2 * Objects.GetObject(1)->Meshes.size());
    CHECK(static_cast<std::size_t>(Kernel.Renderer().LastFrameDrawCalls()) == Objects.GetObject(1)->Meshes.size());
}

TEST_CASE("Commands that change how an object draws refresh a still scene")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    dbc::ObjectManager& Objects = Kernel.Objects();
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    Objects.MakeObjectSphere(3, 1.0f, 8, 8);
    REQUIRE(Objects.MakeMeshFromObject(1, 3));
    Objects.DeleteObject(3);
    Objects.MakeObjectCube(1, 2.0f);
    Objects.PositionObject(1, 0.0f, 0.0f, 10.0f);
    REQUIRE(Objects.AddLimb(1, 1, 1));
    Objects.MakeObjectCube(2, 2.0f);
    Objects.PositionObject(2, 3.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());

    // Each command runs on a scene drawn and cached, which must then be rebuilt.
    const auto Refreshes = [&](const char* Name, auto&& Command)
    {
        INFO(std::string(Name));
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        REQUIRE(Kernel.Renderer().HasCachedTris());
        REQUIRE_FALSE(Kernel.Objects().AnyObjectDirty());
        Command();
        CHECK((Kernel.Objects().AnyObjectDirty() || !Kernel.Renderer().HasCachedTris()));
    };
    Refreshes("GHOST OBJECT ON", [&] { Objects.GhostObjectOn(1); });
    Refreshes("GHOST OBJECT OFF", [&] { Objects.GhostObjectOff(1); });
    Refreshes("COLOR OBJECT", [&] { Objects.ColorObject(1, 0xFF0000); });
    Refreshes("LOCK OBJECT ON", [&] { REQUIRE(Objects.LockObjectOn(1)); });
    Refreshes("LOCK OBJECT OFF", [&] { REQUIRE(Objects.LockObjectOff(1)); });
    Refreshes("DISABLE OBJECT ZDEPTH", [&] { REQUIRE(Objects.DisableObjectZdepth(1)); });
    Refreshes("ENABLE OBJECT ZDEPTH", [&] { REQUIRE(Objects.EnableObjectZdepth(1)); });
    Refreshes("SCALE OBJECT TEXTURE", [&] { REQUIRE(Objects.ScaleObjectTexture(1, 2.0f, 2.0f)); });
    Refreshes("TEXTURE LIMB", [&] { REQUIRE(Objects.TextureLimb(1, 1, 1)); });
    Refreshes("SCROLL LIMB TEXTURE", [&] { REQUIRE(Objects.ScrollLimbTexture(1, 1, 0.5f, 0.0f)); });
    Refreshes("SCALE LIMB TEXTURE", [&] { REQUIRE(Objects.ScaleLimbTexture(1, 1, 2.0f, 2.0f)); });
    Refreshes("SET OBJECT RADIUS", [&] { Objects.SetObjectRadius(1, 5.0f); });
    Refreshes("SET VERTEXDATA POSITION",
              [&]
              {
                  REQUIRE(Objects.LockVertexdataForLimb(1, 1));
                  REQUIRE(Objects.SetVertexdataPosition(0, 0.0f, 3.0f, 0.0f));
                  Objects.UnlockVertexdata();
              });
    Refreshes("DELETE OBJECT", [&] { Objects.DeleteObject(2); });

    // End to end: a ghost blends with what is behind it, so the still frame changes.
    const auto CenterPixel = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::array<Uint8, 4> Pixel{};
        REQUIRE(
            SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };
    const std::array<Uint8, 4> Solid = CenterPixel();
    Objects.GhostObjectOn(1);
    CHECK(CenterPixel() != Solid);
}

TEST_CASE("Meshes stay on the CPU path where blending or the depth order decides the result")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    for (int Id = 1; Id <= 3; ++Id)
    {
        Kernel.Objects().MakeObjectCube(Id, 2.0f);
        Kernel.Objects().PositionObject(Id, static_cast<float>(Id) * 3.0f - 6.0f, 0.0f, 10.0f);
    }
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto GpuDraws = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        return Kernel.Renderer().CachedMeshDraws().size();
    };

    CHECK(GpuDraws() == 3);
    // A ghost blends: only its own mesh leaves the GPU path.
    Kernel.Objects().GhostObjectOn(2);
    CHECK(GpuDraws() == 2);
    CHECK_FALSE(Kernel.Renderer().CachedWorldTris().empty());
    // An opaque object that writes no depth must stay in depth order with the others: no mesh is drawn apart.
    REQUIRE(Kernel.Objects().DisableObjectZwrite(3));
    CHECK(GpuDraws() == 0);
    REQUIRE(Kernel.Objects().EnableObjectZwrite(3));
    CHECK(GpuDraws() == 2);
}

TEST_CASE("A mirror alone in view draws its mask with samplers bound")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 8.0f, 8.0f));
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Objects().SetReflectionShadingOn(1));
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().LastFrameDrawCalls() > 0);
}

TEST_CASE("A black-keyed texture is built once, however often the scene is rebuilt")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    Kernel.Objects().TextureObject(1, 1);
    REQUIRE(Kernel.Objects().SetObjectTransparency(1, 1));
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto Render = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
    };
    Render();
    const SDL_GPUTexture* First = Kernel.ImageStoreRef().GetImage(1)->GpuColorKeyTexture;
    REQUIRE(First != nullptr);
    Kernel.Objects().PositionObject(1, 1.0f, 0.0f, 10.0f);
    Render();
    Kernel.Objects().PositionObject(1, 2.0f, 0.0f, 10.0f);
    Render();
    CHECK(Kernel.ImageStoreRef().GetImage(1)->GpuColorKeyTexture == First);
}

TEST_CASE("A tiny mesh without vertex normals, scaled up, lights as on the CPU path")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    Kernel.Objects().MakeObjectBox(1, 0.0005f, 0.0005f, 0.0005f);
    Kernel.Objects().ScaleObject(1, 800000.0f, 800000.0f, 800000.0f);
    Kernel.Objects().RotateObject(1, 25.0f, 35.0f, 0.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Core().EnsureInitialized());
    const GpuMeshComparison Frames = CompareGpuMeshFrames(Kernel);
    CHECK(Frames.GpuDraws == 1);
    CHECK(Frames.Covered > 640 * 480 / 40);
    CHECK(Frames.Different * 200 < Frames.Covered);
}

TEST_CASE("A still scene never draws with an image texture released since it was built")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    // One object drawn from GPU buffers, one from CPU triangles.
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, -2.0f, 0.0f, 10.0f);
    Kernel.Objects().TextureObject(1, 1);
    Kernel.Objects().MakeObjectCube(2, 2.0f);
    Kernel.Objects().PositionObject(2, 2.0f, 0.0f, 10.0f);
    Kernel.Objects().TextureObject(2, 1);
    Kernel.Objects().GhostObjectOn(2);
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto Render = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        REQUIRE(Kernel.Renderer().CachedMeshDraws().size() == 1);
        REQUIRE_FALSE(Kernel.Renderer().CachedWorldTris().empty());
    };

    Render();
    REQUIRE(Kernel.Renderer().CachedMeshDraws().front().Texture != nullptr);
    // Nothing moves: only the released texture tells that the cached scene is stale.
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "spark.bmp").string(), 1));
    Render();
    const dbc::Image* Replaced = Kernel.ImageStoreRef().GetImage(1);
    REQUIRE(Replaced != nullptr);
    REQUIRE(Replaced->GpuTexture != nullptr);
    CHECK(Kernel.Renderer().CachedMeshDraws().front().Texture == Replaced->GpuTexture);
    CHECK(Kernel.Renderer().CachedWorldTris().front().GpuTexture == Replaced->GpuTexture);
}

TEST_CASE("Rendered object vertices match the query transforms exactly")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    dbc::ObjectManager& Objects = Kernel.Objects();
    // A pivoted, ZYX-rotated, scaled cube whose second limb is scaled and linked to a rotated, offset first limb.
    Objects.MakeObjectCube(1, 2.0f);
    Objects.RotateObject(1, 17.0f, 33.0f, -21.0f);
    REQUIRE(Objects.FixObjectPivot(1));
    Objects.RotateObject(1, 9.0f, -14.0f, 27.0f);
    REQUIRE(Objects.SetObjectRotationZyx(1));
    Objects.ScaleObject(1, 120.0f, 80.0f, 95.0f);
    Objects.PositionObject(1, -2.0f, 0.5f, 14.0f);
    REQUIRE(Objects.MakeMeshFromObject(1, 1));
    REQUIRE(Objects.AddLimb(1, 1, 1));
    REQUIRE(Objects.AddLimb(1, 2, 1));
    REQUIRE(Objects.OffsetLimb(1, 1, 1.5f, 0.0f, 0.0f));
    REQUIRE(Objects.RotateLimb(1, 1, 30.0f, 10.0f, 5.0f));
    REQUIRE(Objects.ScaleLimb(1, 2, 50.0f, 50.0f, 50.0f));
    REQUIRE(Objects.OffsetLimb(1, 2, 0.0f, 1.2f, 0.0f));
    REQUIRE(Objects.LinkLimb(1, 1, 2));
    // A cone glued to the first limb.
    Objects.MakeObjectCone(2, 1.0f, 2.0f, 8);
    Objects.RotateObject(2, 0.0f, 45.0f, 0.0f);
    REQUIRE(Objects.GlueObjectToLimb(2, 1, 1));
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);

    const dbc::RenderView View = Kernel.Renderer().CameraView(320, 240);
    std::set<std::array<float, 3>> Expected;
    for (const int Id : {1, 2})
    {
        const dbc::Object3D* Object = Objects.GetObject(Id);
        REQUIRE(Object != nullptr);
        for (const dbc::Mesh& MeshValue : Object->Meshes)
            for (std::size_t Vertex = 0; Vertex < MeshValue.Geometry().Vertices.size(); ++Vertex)
            {
                const dbc::Vec3 World = Kernel.Renderer().WorldMeshVertex(*Object, MeshValue, Vertex);
                const glm::vec4 Point = View.WorldToView * glm::vec4(World.X, World.Y, World.Z, 1.0f);
                Expected.insert({Point.x, Point.y, Point.z});
            }
    }
    const std::vector<dbc::Triangle>& Triangles = Kernel.Renderer().CachedWorldTris();
    REQUIRE(Triangles.size() > 12);
    int Unmatched = 0;
    for (const dbc::Triangle& TriangleValue : Triangles)
        for (const auto& Corner : TriangleValue.Pos)
            Unmatched += Expected.count({Corner[0], Corner[1], Corner[2]}) == 0 ? 1 : 0;
    CHECK(Unmatched == 0);
}

TEST_CASE("Opaque objects interleaved in depth batch by texture")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    const std::filesystem::path Images = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/bmp";
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "beam.bmp").string(), 1));
    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Images / "spark.bmp").string(), 2));
    // Six cubes one behind the other, their textures alternating with depth.
    for (int Id = 1; Id <= 6; ++Id)
    {
        Kernel.Objects().MakeObjectCube(Id, 2.0f);
        Kernel.Objects().PositionObject(Id, static_cast<float>(Id % 2) * 3.0f - 1.5f, 0.0f, 6.0f + 4.0f * Id);
        Kernel.Objects().TextureObject(Id, Id % 2 + 1);
    }
    // Batching applies to CPU triangles; meshes drawn from GPU buffers take one draw each.
    Kernel.Renderer().SetGpuMeshesEnabled(false);
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto DrawCalls = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        return Kernel.Renderer().LastFrameDrawCalls();
    };

    const int Alternating = DrawCalls();
    for (int Id = 1; Id <= 6; ++Id)
        Kernel.Objects().TextureObject(Id, 1);
    const int Single = DrawCalls();
    CHECK(Single > 0);
    // The second texture costs one batch, not one per object.
    CHECK(Alternating == Single + 1);
}

TEST_CASE("DBPro BSP worlds load, draw the leaves their camera sees and delete")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-bsp-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directories(Root / "maps"));
    const std::filesystem::path Tutorial = std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/world";
    for (const char* Name : {"ikzdm1.BSP", "CYGRASS1.png", "CYWALLT.png"})
        REQUIRE(std::filesystem::copy_file(Tutorial / Name, Root / "maps" / Name));
    {
        std::ofstream Quake3(Root / "maps/q3.bsp", std::ios::binary);
        Quake3.write("IBSP\x2E\0\0\0", 8);
        Quake3 << std::string(136, '\0');
    }
    {
        std::ofstream Archive(Root / "maps/pack.pk3", std::ios::binary);
        Archive << "PK";
    }

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("bsp", 64, 48));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Core().EnsureInitialized());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.AssetRoot = Root;
    std::string Error;
    const auto Load = [&](const char* Pk3, const char* Map)
    {
        Error.clear();
        return InvokeCommand(Context, dbc::language::SymbolId::Command_LoadBsp,
                             {BuiltinValue::FromString(Pk3), BuiltinValue::FromString(Map)}, Error);
    };
    const auto WorldTriangles = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        return Kernel.Renderer().CachedWorldTris().size();
    };

    CHECK_FALSE(Load("", "maps/missing.bsp"));
    CHECK(Error.find("cannot find BSP file") != std::string::npos);
    CHECK_FALSE(Load("", "maps/q3.bsp"));
    CHECK(Error.find("Quake 3 maps") != std::string::npos);
    CHECK_FALSE(Load("maps/pack.pk3", "inside.bsp"));
    CHECK(Error.find("pk3 archives") != std::string::npos);
    CHECK_FALSE(Kernel.BspWorld().HasWorld());

    // As in the tutorial: the pk3 does not exist, the map is found in the pk3's folder whatever its case,
    // and the .bmp texture names fall back to the converted media next to the map.
    REQUIRE_MESSAGE(Load("maps/ikzdm1.pk3", "ikzdm1.bsp"), Error);
    REQUIRE(Kernel.BspWorld().HasWorld());
    CHECK(Kernel.BspWorld().TextureImage(0) != 0);
    CHECK(Kernel.BspWorld().TextureImage(19) != 0);

    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -30.0f, 2.0f));
    const std::size_t Inside = WorldTriangles();
    CHECK(Inside > 0);

    // A BSP camera that does not exist culls from the origin, which is in the same open leaf.
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_SetBspCamera, {BuiltinValue::FromNumber(5)}, Error));
    CHECK(Kernel.BspWorld().CullingCamera() == 5);
    CHECK(WorldTriangles() == Inside);
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_SetBspCamera, {BuiltinValue::FromNumber(0)}, Error));
    // From solid space under the floor nothing is drawn.
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -40.0f, 2.0f));
    CHECK(WorldTriangles() == 0);

    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -30.0f, 2.0f));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_DeleteBsp, {}, Error));
    CHECK_FALSE(Kernel.BspWorld().HasWorld());
    CHECK(WorldTriangles() == 0);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_DeleteBsp, {}, Error));

    Kernel.Shutdown();
    std::filesystem::remove_all(Root);
}

TEST_CASE("DBPro BSP collision keeps cameras and objects on the world's open side")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-bsp-collision-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directories(Root));
    REQUIRE(std::filesystem::copy_file(
        std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/world/ikzdm1.BSP", Root / "ikzdm1.BSP"));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("bsp-collision", 32, 24));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Core().EnsureInitialized());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.AssetRoot = Root;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<double> Arguments)
    {
        std::vector<BuiltinValue> Values;
        for (const double Value : Arguments)
            Values.push_back(BuiltinValue::FromNumber(Value));
        Error.clear();
        return InvokeCommand(Context, Symbol, Values, Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, double Index)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, {BuiltinValue::FromNumber(Index)}, Result, Error));
        return Result.AsNumber();
    };
    const auto CameraY = [&] { return Kernel.Camera().PositionY(); };
    using dbc::language::SymbolId;

    // Before LOAD BSP the setup is accepted and ignored, as in DBPro; indices stay validated.
    CHECK(Command(SymbolId::Command_SetBspCameraCollision, {1, 0, 0.75, 0}));
    CHECK_FALSE(Command(SymbolId::Command_SetBspCameraCollision, {25, 0, 0.75, 0}));
    CHECK(Error.find("between 1 and 24") != std::string::npos);
    CHECK_FALSE(Command(SymbolId::Command_ProcessBspCollision, {0}));
    CHECK(Error.find("between 1 and 24") != std::string::npos);

    REQUIRE(InvokeCommand(Context, SymbolId::Command_LoadBsp,
                          {BuiltinValue::FromString(""), BuiltinValue::FromString("ikzdm1.bsp")}, Error));
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, 3.0f, 2.0f));
    Kernel.Core().Sync();
    // The earlier setup did not survive: the camera falls through the floor.
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -40.0f, 2.0f));
    Kernel.Core().Sync();
    CHECK(CameraY() == doctest::Approx(-40.0f));

    // world1.dba: radius 0.75 is a (0.75, 1.5, 0.75) ellipsoid and the floor is at y = -32.
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, 3.0f, 2.0f));
    REQUIRE(Command(SymbolId::Command_SetBspCameraCollision, {1, 0, 0.75, 0}));
    REQUIRE(Command(SymbolId::Command_SetBspCollisionThreshhold, {1, 0.001}));
    for (int Frame = 0; Frame < 400; ++Frame)
    {
        REQUIRE(Kernel.Camera().PositionCamera(2.0f, CameraY() - 0.1f, 2.0f));
        Kernel.Core().Sync();
    }
    CHECK(CameraY() == doctest::Approx(-30.5f).epsilon(0.001));
    CHECK(Function(SymbolId::Function_BspCollisionHit, 1) == 1);
    CHECK(Function(SymbolId::Function_BspCollisionHit, 1) == 0);
    CHECK(Function(SymbolId::Function_BspCollisionY, 1) > 0.05);
    CHECK(Function(SymbolId::Function_BspCollisionX, 1) == doctest::Approx(0.0));

    // PROCESS BSP COLLISION resolves at once, before the next SYNC.
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -40.0f, 2.0f));
    REQUIRE(Command(SymbolId::Command_ProcessBspCollision, {1}));
    CHECK(CameraY() == doctest::Approx(-30.5f).epsilon(0.001));

    // The height adjustment lowers the collision centre below the camera. Like DBPro, the solver does
    // not push out an ellipsoid that already overlaps the floor, so the camera rises before falling.
    REQUIRE(Command(SymbolId::Command_SetBspCollisionHeightAdjustment, {1, 1.0}));
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -25.0f, 2.0f));
    Kernel.Core().Sync();
    for (int Frame = 0; Frame < 60; ++Frame)
    {
        REQUIRE(Kernel.Camera().PositionCamera(2.0f, CameraY() - 0.1f, 2.0f));
        Kernel.Core().Sync();
    }
    CHECK(CameraY() == doctest::Approx(-29.5f).epsilon(0.001));

    REQUIRE(Command(SymbolId::Command_SetBspCollisionOff, {1}));
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -40.0f, 2.0f));
    Kernel.Core().Sync();
    CHECK(CameraY() == doctest::Approx(-40.0f));

    // Response 1 puts an object back where it was before the blocked move.
    Kernel.Objects().MakeObjectCube(1, 1.0f);
    Kernel.Objects().PositionObject(1, 2.0f, -20.0f, 2.0f);
    REQUIRE(Command(SymbolId::Command_SetBspObjectCollision, {2, 1, 0.5, 1}));
    Kernel.Objects().PositionObject(1, 2.0f, -40.0f, 2.0f);
    Kernel.Core().Sync();
    REQUIRE(Kernel.Objects().GetObject(1) != nullptr);
    CHECK(Kernel.Objects().GetObject(1)->PosY == doctest::Approx(-20.0f));
    CHECK(Function(SymbolId::Function_BspCollisionHit, 2) == 1);

    // The radius form sets the ellipsoid directly and keeps the slot's response.
    REQUIRE(Command(SymbolId::Command_SetBspObjectCollisionRadius, {2, 1, 2, 4, 2}));
    Kernel.Objects().PositionObject(1, 2.0f, -40.0f, 2.0f);
    Kernel.Core().Sync();
    CHECK(Kernel.Objects().GetObject(1)->PosY == doctest::Approx(-20.0f));
    REQUIRE(Command(SymbolId::Command_SetBspCameraCollisionRadius, {3, 0, 1, 1, 1}));
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -40.0f, 2.0f));
    Kernel.Core().Sync();
    CHECK(CameraY() == doctest::Approx(-40.0f));
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -20.0f, 2.0f));
    Kernel.Core().Sync();
    REQUIRE(Kernel.Camera().PositionCamera(2.0f, -40.0f, 2.0f));
    Kernel.Core().Sync();
    CHECK(CameraY() == doctest::Approx(-31.0f).epsilon(0.001));

    Kernel.Shutdown();
    std::filesystem::remove_all(Root);
}

TEST_CASE("Portable terrain matrices preserve DBPro grid texture normal and vector contracts")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(20), BuiltinValue::FromNumber(20),
                     BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(2)}));
    CHECK(Function(dbc::language::SymbolId::Function_MatrixExist, {BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_MatrixTileCount, {BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_MatrixTilesExist, {BuiltinValue::FromNumber(1)}) == 0.0);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_MakeMatrix,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1),
                         BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    CHECK(Error.find("cannot replace matrix 1") != std::string::npos);

    Kernel.ImageStoreRef().CreateBitmap(1, 4, 4);
    Kernel.ImageStoreRef().GetImage(1, 0, 0, 4, 4);
    REQUIRE(Command(dbc::language::SymbolId::Command_PrepareMatrixTexture,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                     BuiltinValue::FromNumber(2)}));
    CHECK(Function(dbc::language::SymbolId::Function_MatrixTileCount, {BuiltinValue::FromNumber(1)}) == 4.0);
    CHECK(Function(dbc::language::SymbolId::Function_MatrixTilesExist, {BuiltinValue::FromNumber(1)}) == 1.0);
    REQUIRE(Kernel.Matrices().GetMatrix(1) != nullptr);
    CHECK_FALSE(Kernel.Matrices().GetMatrix(1)->Wireframe);

    REQUIRE(Command(dbc::language::SymbolId::Command_FillMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixTile,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(4)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixNormal,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3)}));
    const dbc::Matrix* MatrixValue = Kernel.Matrices().GetMatrix(1);
    REQUIRE(MatrixValue != nullptr);
    CHECK(dbc::MatrixManager::MatrixTile(*MatrixValue, 0, 0) == 4);
    REQUIRE(MatrixValue->Normals.size() == 27);
    CHECK(MatrixValue->Normals[3] == doctest::Approx(1.0f));
    CHECK(MatrixValue->Normals[4] == doctest::Approx(2.0f));
    CHECK(MatrixValue->Normals[5] == doctest::Approx(3.0f));

    Kernel.Renderer().CacheScene(std::make_shared<dbc::SceneTriangles>());
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixHeight,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    CHECK_FALSE(Kernel.Renderer().HasCachedTris());
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().SetMatrixHeight(1, 1, 0, 10.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().SetMatrixHeight(1, 0, 1, 20.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().SetMatrixHeight(1, 1, 1, 40.0f, &Error); });
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().PositionMatrix(1, 100.0f, 200.0f, 300.0f, &Error); });
    CHECK(Function(dbc::language::SymbolId::Function_GetGroundHeight,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7.5), BuiltinValue::FromNumber(2.5)}) ==
          doctest::Approx(15.0));
    CHECK(Function(dbc::language::SymbolId::Function_MatrixPositionY, {BuiltinValue::FromNumber(1)}) == 200.0);

    Context.Vectors[1] = {40.0f, 50.0f, 60.0f};
    Context.Vectors[2] = {};
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToMatrixPosition,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    CHECK(Context.Vectors[2][0] == doctest::Approx(40.0f));
    CHECK(Context.Vectors[2][1] == doctest::Approx(50.0f));
    CHECK(Context.Vectors[2][2] == doctest::Approx(60.0f));

    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().ShiftMatrixLeft(1, &Error); });
    MatrixValue = Kernel.Matrices().GetMatrix(1);
    REQUIRE(MatrixValue != nullptr);
    CHECK(MatrixValue->Normals[0] == doctest::Approx(1.0f));
    CHECK(MatrixValue->Normals[1] == doctest::Approx(2.0f));
    CHECK(MatrixValue->Normals[2] == doctest::Approx(3.0f));
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().ShiftMatrixRight(1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().ShiftMatrixUp(1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().ShiftMatrixDown(1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().RandomizeMatrix(1, 1.0f, &Error); });
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Matrices().GetMatrixHeight(Output, 1, 2, 2, &Error); }) == 0.0f);
    REQUIRE(Command(dbc::language::SymbolId::Command_UpdateMatrix, {BuiltinValue::FromNumber(1)}));

    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetMatrixHeight,
                        {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                         BuiltinValue::FromNumber(1)}));
    CHECK(Error.find("SET MATRIX HEIGHT requires existing matrix 99") != std::string::npos);
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteMatrix, {BuiltinValue::FromNumber(1)}));
    CHECK(Function(dbc::language::SymbolId::Function_MatrixExist, {BuiltinValue::FromNumber(1)}) == 0.0);
}

TEST_CASE("DBC matrices render from below and retain the classic zero tile alias")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("dbc-matrix-profile", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, -5.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 10.0f, &Error); });

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(10),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().PositionMatrix(1, -5.0f, 0.0f, 5.0f, &Error); });
    const dbc::Matrix* MatrixValue = Kernel.Matrices().GetMatrix(1);
    REQUIRE(MatrixValue != nullptr);
    CHECK_FALSE(MatrixValue->Cull);

    Kernel.ImageStoreRef().CreateBitmap(1, 4, 4);
    SDL_Surface* MatrixTexture = Kernel.ImageStoreRef().GetBitmapSurface(1);
    REQUIRE(MatrixTexture != nullptr);
    REQUIRE(SDL_FillSurfaceRect(MatrixTexture, nullptr, SDL_MapSurfaceRGBA(MatrixTexture, 255, 255, 255, 255)));
    Kernel.ImageStoreRef().GetImage(1, 0, 0, 4, 4);
    Kernel.ImageStoreRef().SetCurrentBitmap(0);
    REQUIRE(Command(dbc::language::SymbolId::Command_PrepareMatrixTexture,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4),
                     BuiltinValue::FromNumber(4)}));
    REQUIRE(MatrixValue->OwnsImage);
    REQUIRE(MatrixValue->ImageId < 0);
    REQUIRE(SDL_FillSurfaceRect(MatrixTexture, nullptr, SDL_MapSurfaceRGBA(MatrixTexture, 255, 0, 0, 255)));
    Kernel.ImageStoreRef().GetImage(1, 0, 0, 4, 4);
    const dbc::Image* MatrixImage = Kernel.ImageStoreRef().GetImage(MatrixValue->ImageId);
    REQUIRE(MatrixImage != nullptr);
    REQUIRE(MatrixImage->Surface != nullptr);
    Uint8 SnapshotRed = 0;
    Uint8 SnapshotGreen = 0;
    Uint8 SnapshotBlue = 0;
    Uint8 SnapshotAlpha = 0;
    REQUIRE(
        SDL_ReadSurfacePixel(MatrixImage->Surface, 0, 0, &SnapshotRed, &SnapshotGreen, &SnapshotBlue, &SnapshotAlpha));
    CHECK(SnapshotRed == 255);
    CHECK(SnapshotGreen == 255);
    CHECK(SnapshotBlue == 255);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixTile,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    CHECK(dbc::MatrixManager::MatrixTile(*MatrixValue, 0, 0) == 1);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixTile,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(16)}));
    CHECK(dbc::MatrixManager::MatrixTile(*MatrixValue, 0, 0) == 16);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixTile,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    CHECK(dbc::MatrixManager::MatrixTile(*MatrixValue, 0, 0) == 1);

    REQUIRE(Command(dbc::language::SymbolId::Command_FogColor, {BuiltinValue::FromNumber(0xFF0000)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_FogDistance, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ColorBackdrop, {BuiltinValue::FromNumber(0x0000FF)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_FogOn, {}));
    REQUIRE(Command(dbc::language::SymbolId::Command_BackdropOn, {}));

    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    Uint8 CenterRed = 0;
    Uint8 CenterGreen = 0;
    Uint8 CenterBlue = 0;
    Uint8 CenterAlpha = 0;
    Uint8 CornerRed = 0;
    Uint8 CornerGreen = 0;
    Uint8 CornerBlue = 0;
    Uint8 CornerAlpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &CenterRed, &CenterGreen, &CenterBlue,
                                 &CenterAlpha));
    REQUIRE(SDL_ReadSurfacePixel(Capture, 0, 0, &CornerRed, &CornerGreen, &CornerBlue, &CornerAlpha));
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedWorldTris().size() == 2);
    CHECK(CenterRed >= 240);
    CHECK(CenterGreen <= 16);
    CHECK(CenterBlue <= 16);
    CHECK(CornerRed <= 16);
    CHECK(CornerGreen <= 16);
    CHECK(CornerBlue >= 240);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    CHECK(MatrixValue->Cull);
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &CenterRed, &CenterGreen, &CenterBlue,
                                 &CenterAlpha));
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedWorldTris().empty());
    CHECK(CenterRed <= 16);
    CHECK(CenterGreen <= 16);
    CHECK(CenterBlue >= 240);

    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteMatrix, {BuiltinValue::FromNumber(1)}));
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeMatrix,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(10),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().PositionMatrix(2, -5.0f, 0.0f, 5.0f, &Error); });
    MatrixValue = Kernel.Matrices().GetMatrix(2);
    REQUIRE(MatrixValue != nullptr);
    CHECK(MatrixValue->Cull);
    REQUIRE(Command(dbc::language::SymbolId::Command_PrepareMatrixTexture,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4),
                     BuiltinValue::FromNumber(4)}));
    CHECK_FALSE(MatrixValue->OwnsImage);
    CHECK(MatrixValue->ImageId == 1);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetMatrixTile,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                         BuiltinValue::FromNumber(0)}));
    CHECK(Error.find("tile number must be between 1 and 16") != std::string::npos);

    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedWorldTris().empty());

    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrix,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    CHECK_FALSE(MatrixValue->Cull);
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedWorldTris().size() == 2);
}

TEST_CASE("Portable matrix appearance drives DBPro renderer state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 5.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 10.0f, &Error); });
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().MakeMatrix(1, 10.0f, 10.0f, 1, 1, true, &Error); });
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().PositionMatrix(1, -5.0f, 0.0f, 5.0f, &Error); });

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto WireframeState = [&]()
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_MatrixWireframeState,
                               {BuiltinValue::FromNumber(1)}, Result, Error));
        return ReadBuiltinInteger(Result);
    };

    CHECK(WireframeState() == 1);
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedWorldTris().size() == 2);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixWireframeOff, {BuiltinValue::FromNumber(1)}));
    CHECK(WireframeState() == 0);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixWireframeOn, {BuiltinValue::FromNumber(1)}));
    CHECK(WireframeState() == 1);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                     BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixTexture,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixTrim,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0.125), BuiltinValue::FromNumber(0.25)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrixPriority,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));

    const dbc::Matrix* MatrixValue = Kernel.Matrices().GetMatrix(1);
    REQUIRE(MatrixValue != nullptr);
    CHECK_FALSE(MatrixValue->Wireframe);
    CHECK(MatrixValue->Transparency);
    CHECK_FALSE(MatrixValue->Cull);
    CHECK_FALSE(MatrixValue->Filter);
    CHECK_FALSE(MatrixValue->Light);
    CHECK_FALSE(MatrixValue->Fog);
    CHECK_FALSE(MatrixValue->Ambient);
    CHECK(MatrixValue->TextureMode == 2);
    CHECK(MatrixValue->MipMode == 1);
    CHECK(MatrixValue->TrimX == doctest::Approx(0.125f));
    CHECK(MatrixValue->TrimY == doctest::Approx(0.25f));
    CHECK(MatrixValue->RenderAfterObjects);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                     BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));

    for (int Mode = 0; Mode <= 4; ++Mode)
    {
        REQUIRE(Command(dbc::language::SymbolId::Command_GhostMatrixOn,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(Mode)}));
        MatrixValue = Kernel.Matrices().GetMatrix(1);
        REQUIRE(MatrixValue != nullptr);
        CHECK(MatrixValue->Ghost);
        CHECK(MatrixValue->GhostMode == Mode);
    }
    REQUIRE_FALSE(Command(dbc::language::SymbolId::Command_GhostMatrixOn,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(5)}));
    CHECK(Error.find("mode must be between 0 and 4") != std::string::npos);

    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    REQUIRE_FALSE(Kernel.Renderer().CachedWorldTris().empty());
    const dbc::Triangle& Triangle = Kernel.Renderer().CachedWorldTris().front();
    CHECK(Triangle.Transparent);
    CHECK_FALSE(Triangle.AlphaDepthWrite);
    CHECK(Triangle.MatrixGhostMode == 4);
    CHECK(Triangle.RenderPriority == 1);
    CHECK_FALSE(Triangle.Style.Wireframe);
    CHECK(Triangle.Style.Fog);
    CHECK(Triangle.Style.BlackFog);
    CHECK_FALSE(Triangle.Style.Ambient);
    CHECK_FALSE(Triangle.Style.Filter);
    CHECK(Triangle.Style.TextureAddressModes[0] == 2);
    CHECK(Triangle.Style.TextureMipModes[0] == 1);
    CHECK(Triangle.Normal[0][0] == doctest::Approx(0.0f));
    CHECK(Triangle.Normal[0][1] == doctest::Approx(0.0f));
    CHECK(Triangle.Uv[0].x == doctest::Approx(0.125f));
    CHECK(Triangle.Uv[0].y == doctest::Approx(0.25f));
    for (int Mode = 0; Mode <= 4; ++Mode)
    {
        CHECK(Kernel.Gpu().SurfaceGhostPipeline(Mode, false, true) != nullptr);
        CHECK(Kernel.Gpu().SurfaceGhostPipeline(Mode, true, true) != nullptr);
        CHECK(Kernel.Gpu().SurfaceGhostPipeline(Mode, false, false) != nullptr);
        CHECK(Kernel.Gpu().SurfaceGhostPipeline(Mode, true, false) != nullptr);
    }
    CHECK(Kernel.Gpu().SurfaceSampler(2, false, 1) != nullptr);

    REQUIRE(Command(dbc::language::SymbolId::Command_GhostMatrixOff, {BuiltinValue::FromNumber(1)}));
    MatrixValue = Kernel.Matrices().GetMatrix(1);
    REQUIRE(MatrixValue != nullptr);
    CHECK_FALSE(MatrixValue->Ghost);
    CHECK_FALSE(MatrixValue->RenderAfterObjects);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetMatrixWireframeOff, {BuiltinValue::FromNumber(99)}));
    CHECK(Error.find("requires existing matrix 99") != std::string::npos);
}

TEST_CASE("DBPro object depth UV alpha and bounds controls drive portable rendering")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, -10.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 0.0f, &Error); });
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 4.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };

    dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    REQUIRE_FALSE(Object->Meshes.empty());
    const std::vector<SDL_FPoint> OriginalUvs = Object->Meshes.front().Geometry().Uvs;
    Object->Meshes.front().EditGeometry().UvStages[1] = OriginalUvs;
    REQUIRE(Command(dbc::language::SymbolId::Command_ScaleObjectTexture,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ScaleObjectTexture,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4),
                     BuiltinValue::FromNumber(5)}));
    REQUIRE(Object->Meshes.front().Geometry().Uvs.size() == OriginalUvs.size());
    REQUIRE(Object->Meshes.front().Geometry().UvStages.at(1).size() == OriginalUvs.size());
    for (std::size_t Index = 0; Index < OriginalUvs.size(); ++Index)
    {
        CHECK(Object->Meshes.front().Geometry().Uvs[Index].x == doctest::Approx(OriginalUvs[Index].x * 2.0f));
        CHECK(Object->Meshes.front().Geometry().Uvs[Index].y == doctest::Approx(OriginalUvs[Index].y * 3.0f));
        CHECK(Object->Meshes.front().Geometry().UvStages.at(1)[Index].x ==
              doctest::Approx(OriginalUvs[Index].x * 4.0f));
        CHECK(Object->Meshes.front().Geometry().UvStages.at(1)[Index].y ==
              doctest::Approx(OriginalUvs[Index].y * 5.0f));
    }

    REQUIRE(Command(dbc::language::SymbolId::Command_DisableObjectZread, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_DisableObjectZwrite, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetAlphaMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(37.5)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ShowObjectBounds, {BuiltinValue::FromNumber(1)}));
    CHECK_FALSE(Object->DepthTest);
    CHECK_FALSE(Object->DepthWrite);
    CHECK(Object->AlphaMapping);
    CHECK(Object->AlphaMappingAlpha == doctest::Approx(0.375f));
    CHECK(Object->DrawBounds);
    CHECK_FALSE(Object->BoundsBoxOnly);

    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    const std::size_t FullBoundsCount = Kernel.Renderer().CachedWorldTris().size();
    REQUIRE(FullBoundsCount > 24);
    const auto ObjectTriangle = std::find_if(
        Kernel.Renderer().CachedWorldTris().begin(), Kernel.Renderer().CachedWorldTris().end(),
        [](const dbc::Triangle& Triangle) { return Triangle.RenderPriority == 0 && !Triangle.Style.Wireframe; });
    REQUIRE(ObjectTriangle != Kernel.Renderer().CachedWorldTris().end());
    CHECK(ObjectTriangle->Transparent);
    CHECK_FALSE(ObjectTriangle->DepthTest);
    CHECK_FALSE(ObjectTriangle->DepthWrite);
    CHECK(ObjectTriangle->Style.AlphaOverride == doctest::Approx(0.375f));
    CHECK(std::any_of(Kernel.Renderer().CachedWorldTris().begin(), Kernel.Renderer().CachedWorldTris().end(),
                      [](const dbc::Triangle& Triangle)
                      {
                          return Triangle.RenderPriority == 2 && Triangle.Style.Wireframe && Triangle.DepthTest &&
                                 !Triangle.DepthWrite;
                      }));

    REQUIRE(Command(dbc::language::SymbolId::Command_ShowObjectBounds,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    const std::size_t BoxOnlyCount = Kernel.Renderer().CachedWorldTris().size();
    CHECK(BoxOnlyCount < FullBoundsCount);
    CHECK(Object->BoundsBoxOnly);

    REQUIRE(Command(dbc::language::SymbolId::Command_HideObjectBounds, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_EnableObjectZread, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_EnableObjectZwrite, {BuiltinValue::FromNumber(1)}));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(Kernel.Renderer().CachedWorldTris().size() == 2);
    CHECK(Object->DepthTest);
    CHECK(Object->DepthWrite);
    CHECK_FALSE(Object->DrawBounds);
    CHECK(Kernel.Gpu().SurfacePipeline(true, false, false, false, false, false) != nullptr);
    CHECK(Kernel.Gpu().SurfacePipeline(true, false, false, true, false, true) != nullptr);
    CHECK(Kernel.Gpu().SurfacePipeline(false, false, false, false, true, false) != nullptr);
    CHECK(Kernel.Gpu().SurfacePipeline(false, false, true, true, true, true) != nullptr);

    CHECK_FALSE(Command(dbc::language::SymbolId::Command_ScaleObjectTexture,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(1),
                         BuiltinValue::FromNumber(1)}));
    CHECK(Error.find("stage must be non-negative") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_DisableObjectZread, {BuiltinValue::FromNumber(99)}));
    CHECK(Error.find("requires existing object 99") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_ShowObjectBounds, {BuiltinValue::FromNumber(99)}));
    CHECK(Error.find("requires existing object 99") != std::string::npos);
}

TEST_CASE("Standard DBPro particles preserve simulation billboard and state contracts")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, -10.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 0.0f, &Error); });

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };

    REQUIRE_MESSAGE(Command(dbc::language::SymbolId::Command_MakeParticles,
                            {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(50),
                             BuiltinValue::FromNumber(2)}),
                    Error);
    CHECK(Function(dbc::language::SymbolId::Function_ParticlesExist, {BuiltinValue::FromNumber(1)}) == 1.0);
    const dbc::ParticleEmitter* Emitter = Kernel.Objects().GetParticles(1);
    REQUIRE(Emitter != nullptr);
    CHECK(Emitter->ImageId == 0);
    CHECK(Emitter->FlushSize == 50);
    CHECK(Emitter->Radius == doctest::Approx(2.0f));
    CHECK(Emitter->SecondsPerFrame == doctest::Approx(0.005f));
    CHECK(Emitter->EmissionsPerFrame == 4);
    CHECK(Emitter->EmissionVelocity == doctest::Approx(4.0f));
    CHECK(Emitter->Gravity == doctest::Approx(5.0f));
    CHECK(Emitter->LifePercent == 100);
    CHECK(Emitter->FloorEnabled == 1);
    CHECK(Emitter->Ghost);
    CHECK(Emitter->GhostMode == -1);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_MakeParticles,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1),
                         BuiltinValue::FromNumber(1)}));
    CHECK(Error.find("already exists") != std::string::npos);

    REQUIRE(Command(dbc::language::SymbolId::Command_PositionParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                     BuiltinValue::FromNumber(3)}));
    CHECK(Function(dbc::language::SymbolId::Function_ParticlesPositionX, {BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_ParticlesPositionY, {BuiltinValue::FromNumber(1)}) == 2.0);
    CHECK(Function(dbc::language::SymbolId::Function_ParticlesPositionZ, {BuiltinValue::FromNumber(1)}) == 3.0);

    Context.Vectors[7] = {4.0f, 5.0f, 6.0f};
    Context.Vectors[8] = {};
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToParticlesPosition,
                    {BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(1)}));
    CHECK(Context.Vectors[8][0] == doctest::Approx(4.0f));
    CHECK(Context.Vectors[8][1] == doctest::Approx(5.0f));
    CHECK(Context.Vectors[8][2] == doctest::Approx(6.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_RotateParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                     BuiltinValue::FromNumber(30)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_RotateParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToParticlesRotation,
                    {BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(1)}));
    CHECK(Context.Vectors[8][0] == doctest::Approx(4.0f));
    CHECK(Context.Vectors[8][1] == doctest::Approx(5.0f));
    CHECK(Context.Vectors[8][2] == doctest::Approx(6.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_PositionParticleEmissions,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4),
                     BuiltinValue::FromNumber(6)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ColorParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(64), BuiltinValue::FromNumber(128),
                     BuiltinValue::FromNumber(255)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleSpeed,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0.01)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleEmissions,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleVelocity,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleGravity,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleLife,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(75)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleFloor,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleChaos,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(250)}));
    Emitter = Kernel.Objects().GetParticles(1);
    REQUIRE(Emitter != nullptr);
    CHECK(Emitter->EmissionPosition.X == doctest::Approx(1.0f));
    CHECK(Emitter->EmissionPosition.Y == doctest::Approx(2.0f));
    CHECK(Emitter->EmissionPosition.Z == doctest::Approx(3.0f));
    CHECK(Emitter->EmissionColor.r == doctest::Approx(64.0f / 255.0f));
    CHECK(Emitter->SecondsPerFrame == doctest::Approx(0.01f));
    CHECK(Emitter->EmissionsPerFrame == 1);
    CHECK(Emitter->EmissionVelocity == doctest::Approx(0.0f));
    CHECK(Emitter->Gravity == doctest::Approx(0.0f));
    CHECK(Emitter->LifePercent == 75);
    CHECK(Emitter->FloorEnabled == 0);
    CHECK(Emitter->Chaos == doctest::Approx(0.25f));

    REQUIRE(Command(dbc::language::SymbolId::Command_GhostParticlesOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    Emitter = Kernel.Objects().GetParticles(1);
    REQUIRE(Emitter != nullptr);
    CHECK(Emitter->GhostMode == 2);
    CHECK(Emitter->GhostPercentage == doctest::Approx(-1.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_GhostParticlesOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(50)}));
    Emitter = Kernel.Objects().GetParticles(1);
    REQUIRE(Emitter != nullptr);
    CHECK(Emitter->GhostMode == 5);
    CHECK(Emitter->GhostPercentage == doctest::Approx(50.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_GhostParticlesOff, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_HideParticles, {BuiltinValue::FromNumber(1)}));
    Emitter = Kernel.Objects().GetParticles(1);
    REQUIRE(Emitter != nullptr);
    CHECK_FALSE(Emitter->Visible);
    REQUIRE(Command(dbc::language::SymbolId::Command_ShowParticles, {BuiltinValue::FromNumber(1)}));

    REQUIRE(Command(dbc::language::SymbolId::Command_PositionParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_RotateParticles,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionParticleEmissions,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleChaos,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE(Command(dbc::language::SymbolId::Command_GhostParticlesOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(50)}));
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    REQUIRE_FALSE(Kernel.Renderer().CachedWorldTris().empty());
    for (const dbc::Triangle& Triangle : Kernel.Renderer().CachedWorldTris())
    {
        CHECK(Triangle.Transparent);
        CHECK_FALSE(Triangle.DepthWrite);
        CHECK(Triangle.Style.AlphaOverride == doctest::Approx(0.5f));
    }
    dbc::ParticleEmitter* MutableEmitter = Kernel.Objects().GetParticles(1);
    REQUIRE(MutableEmitter != nullptr);
    MutableEmitter->Particles.clear();
    MutableEmitter->SimulationTime = 0.0f;
    MutableEmitter->RandomState = 0x9E3779B9u ^ 1u;
    REQUIRE(Command(dbc::language::SymbolId::Command_GhostParticlesOff, {BuiltinValue::FromNumber(1)}));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    REQUIRE_FALSE(Kernel.Renderer().CachedWorldTris().empty());
    for (const dbc::Triangle& Triangle : Kernel.Renderer().CachedWorldTris())
    {
        CHECK_FALSE(Triangle.Transparent);
        CHECK_FALSE(Triangle.DepthWrite);
        CHECK(Triangle.Style.AlphaOverride == doctest::Approx(-1.0f));
    }
    CHECK(Kernel.Gpu().SurfaceOpaquePipeline(false, false) != nullptr);
    MutableEmitter->Particles.clear();
    MutableEmitter->SimulationTime = 0.0f;
    MutableEmitter->RandomState = 0x9E3779B9u ^ 1u;
    REQUIRE(Command(dbc::language::SymbolId::Command_GhostParticlesOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    CHECK(ReadEngineNumber<std::size_t>([&](std::string& Error, std::size_t& Output)
                                        { return Kernel.Objects().ParticleCount(Output, 1, &Error); }) == 1);
    REQUIRE(Kernel.Renderer().CachedWorldTris().size() == 10);
    for (const dbc::Triangle& Triangle : Kernel.Renderer().CachedWorldTris())
    {
        CHECK(Triangle.Transparent);
        CHECK(Triangle.MatrixGhostMode == 0);
        CHECK_FALSE(Triangle.DepthWrite);
        CHECK_FALSE(Triangle.Style.Filter);
        CHECK(Triangle.Style.TextureMipModes[0] == 1);
    }
    CHECK(Kernel.Gpu().SurfaceGhostPipeline(0, false, false) != nullptr);

    CHECK_FALSE(Command(dbc::language::SymbolId::Command_GhostParticlesOn,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(6)}));
    CHECK(Error.find("mode must be between 0 and 5") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetParticleEmissions,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-1)}));
    CHECK(Error.find("must not be negative") != std::string::npos);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetParticleEmissions,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4096)}));
    Kernel.Objects().UpdateParticles();
    CHECK(ReadEngineNumber<std::size_t>([&](std::string& Error, std::size_t& Output)
                                        { return Kernel.Objects().ParticleCount(Output, 1, &Error); }) == 2048);
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteParticles, {BuiltinValue::FromNumber(1)}));
    CHECK(Function(dbc::language::SymbolId::Function_ParticlesExist, {BuiltinValue::FromNumber(1)}) == 0.0);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_ShowParticles, {BuiltinValue::FromNumber(1)}));
    CHECK(Error.find("requires existing particles 1") != std::string::npos);
}

TEST_CASE("DBPro LOD terrain preserves heightmap identity query and render contracts")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Heightmap =
        std::filesystem::temp_directory_path() / ("opendbc-terrain-" + std::to_string(Suffix) + ".bmp");
    SDL_Surface* Surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_WriteSurfacePixel(Surface, 0, 0, 0, 0, 0, 255));
    REQUIRE(SDL_WriteSurfacePixel(Surface, 1, 0, 255, 255, 255, 255));
    REQUIRE(SDL_WriteSurfacePixel(Surface, 0, 1, 64, 64, 64, 255));
    REQUIRE(SDL_WriteSurfacePixel(Surface, 1, 1, 128, 128, 128, 255));
    REQUIRE(SDL_SaveBMP(Surface, Heightmap.string().c_str()));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    Kernel.Camera().SetAutoCamOff();
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeMatrix,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(2),
                     BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeTerrain,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromString(Heightmap.string())}));
    CHECK(Kernel.Matrices().GetMatrix(1) != nullptr);
    const dbc::Matrix* Terrain = Kernel.Matrices().GetTerrain(1);
    REQUIRE(Terrain != nullptr);
    CHECK(Terrain->IsTerrain);
    CHECK_FALSE(Terrain->Wireframe);
    CHECK(Terrain->SegX == 1);
    CHECK(Terrain->SegZ == 1);
    CHECK(Terrain->Width == doctest::Approx(2.0f));
    CHECK(Terrain->Depth == doctest::Approx(2.0f));
    CHECK(Terrain->HeightScale == doctest::Approx(100.0f));
    CHECK(Terrain->PosZ == doctest::Approx(2.0f));
    CHECK(dbc::MatrixManager::MatrixHeight(*Terrain, 0, 1) == doctest::Approx(64.0f / 255.0f));
    CHECK(Function(dbc::language::SymbolId::Function_TerrainExist, {BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_GetTerrainHeight,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1)}) ==
          doctest::Approx(6400.0 / 255.0));
    CHECK(Function(dbc::language::SymbolId::Function_GetTotalTerrainHeight, {BuiltinValue::FromNumber(1)}) == 100.0);

    Kernel.ImageStoreRef().CreateBitmap(7, 2, 2);
    Kernel.ImageStoreRef().GetImage(7, 0, 0, 2, 2);
    SDL_DestroySurface(Surface);
    REQUIRE(Command(dbc::language::SymbolId::Command_TextureTerrain,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionTerrain,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(5),
                     BuiltinValue::FromNumber(6)}));
    CHECK(Function(dbc::language::SymbolId::Function_TerrainPositionX, {BuiltinValue::FromNumber(1)}) == 4.0);
    CHECK(Function(dbc::language::SymbolId::Function_TerrainPositionY, {BuiltinValue::FromNumber(1)}) == 5.0);
    CHECK(Function(dbc::language::SymbolId::Function_TerrainPositionZ, {BuiltinValue::FromNumber(1)}) == 6.0);
    Terrain = Kernel.Matrices().GetTerrain(1);
    REQUIRE(Terrain != nullptr);
    CHECK(Terrain->ImageId == 7);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(5.0f, 150.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(5.0f, 5.0f, 5.0f, &Error); });
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    // The terrain draws from static GPU buffers; the matrix still goes through CPU triangles.
    CHECK(Kernel.Renderer().CachedWorldTris().size() >= 2);
    CHECK(Kernel.Renderer().LastStaticTerrainTriangles() >= 2);
    dbc::TerrainMesh Mesh;
    dbc::BuildTerrainMesh(*Terrain, Mesh);
    REQUIRE_FALSE(Mesh.Vertices.empty());
    CHECK(std::all_of(
        Mesh.Vertices.begin(), Mesh.Vertices.end(), [](const dbc::TerrainVertex& Vertex)
        { return Vertex.Uv[0] >= 0.0f && Vertex.Uv[0] <= 1.0f && Vertex.Uv[1] >= 0.0f && Vertex.Uv[1] <= 1.0f; }));

    REQUIRE_FALSE(Command(dbc::language::SymbolId::Command_MakeTerrain,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromString(Heightmap.string() + ".missing")}));
    CHECK(Error.find("MAKE TERRAIN failed to load heightmap") != std::string::npos);
    CHECK(Kernel.Matrices().GetTerrain(2) == nullptr);
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteTerrain, {BuiltinValue::FromNumber(1)}));
    CHECK(Function(dbc::language::SymbolId::Function_TerrainExist, {BuiltinValue::FromNumber(1)}) == 0.0);

    std::error_code Ignored;
    std::filesystem::remove(Heightmap, Ignored);
}

TEST_CASE("Matrices skip blocks outside the view and terrains coarsen far from the camera")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("matrix-blocks", 64, 64));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Core().EnsureInitialized());
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCameraRange(1.0f, 100000.0f, &Error); });
    const auto RenderedTriangles = [&]
    {
        Kernel.Renderer().InvalidateTriCache();
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        return Kernel.Renderer().CachedWorldTris().size() + Kernel.Renderer().LastStaticTerrainTriangles();
    };

    // A small matrix in full view keeps every cell at full resolution.
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().MakeMatrix(1, 40.0f, 40.0f, 4, 4, true, &Error); });
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(20.0f, 60.0f, -20.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(20.0f, 0.0f, 20.0f, &Error); });
    CHECK(RenderedTriangles() == 32);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().DeleteMatrix(1, &Error); });

    // A wide matrix seen from its centre only builds the blocks inside the view frustum.
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().MakeMatrix(1, 6400.0f, 6400.0f, 128, 128, true, &Error); });
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Matrices().PositionMatrix(1, -3200.0f, 0.0f, -3200.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 10.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCameraFov(20.0f, &Error); });
    const std::size_t MatrixTriangles = RenderedTriangles();
    CHECK(MatrixTriangles > 0);
    // Half of the cells lie in front of the camera, but a 20 degree view only sees a narrow wedge of them.
    CHECK(MatrixTriangles < 128 * 128 * 2 / 6);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCameraFov(75.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Matrices().DeleteMatrix(1, &Error); });

    // A 256 x 256 terrain: nearby blocks keep every cell, distant ones use a coarser grid.
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Heightmap =
        std::filesystem::temp_directory_path() / ("opendbc-lod-" + std::to_string(Suffix) + ".bmp");
    SDL_Surface* Surface = SDL_CreateSurface(257, 257, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, 0, 0, 0, 255)));
    REQUIRE(SDL_SaveBMP(Surface, Heightmap.string().c_str()));
    SDL_DestroySurface(Surface);
    std::string Error;
    REQUIRE(Kernel.Matrices().MakeTerrain(1, Heightmap.string(), &Error));
    std::error_code Ignored;
    std::filesystem::remove(Heightmap, Ignored);
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(128.0f, 200.0f, 128.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(90.0f, 0.0f, 0.0f, &Error); });
    const std::size_t TerrainTriangles = RenderedTriangles();
    CHECK(TerrainTriangles > 0);
    CHECK(TerrainTriangles < 256 * 256 * 2 / 8);
}

TEST_CASE("Static GPU terrains draw their upper side, follow the view and rebuild after changes")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("static-terrain", 64, 64));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Core().EnsureInitialized());
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCameraRange(1.0f, 1000.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetBackdropColor(0x0000FF, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().EnableBackdrop(&Error); });

    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Heightmap =
        std::filesystem::temp_directory_path() / ("opendbc-static-terrain-" + std::to_string(Suffix) + ".bmp");
    SDL_Surface* Surface = SDL_CreateSurface(33, 33, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, 0, 0, 0, 255)));
    REQUIRE(SDL_SaveBMP(Surface, Heightmap.string().c_str()));
    SDL_DestroySurface(Surface);
    std::string Error;
    REQUIRE(Kernel.Matrices().MakeTerrain(1, Heightmap.string(), &Error));
    std::error_code Ignored;
    std::filesystem::remove(Heightmap, Ignored);
    const dbc::Matrix* Terrain = Kernel.Matrices().GetTerrain(1);
    REQUIRE(Terrain != nullptr);
    REQUIRE(Kernel.Renderer().DrawsTerrainOnGpu(*Terrain));

    // Centre pixel: the untextured terrain is lit white, the backdrop is blue.
    const auto CenterIsBackdrop = [&]
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        Uint8 Red = 0, Green = 0, Blue = 0, Alpha = 0;
        REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
        SDL_DestroySurface(Capture);
        return Red < 32 && Green < 32 && Blue > 200;
    };
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(16.0f, 40.0f, 16.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(90.0f, 0.0f, 0.0f, &Error); });
    CHECK_FALSE(CenterIsBackdrop());
    CHECK(Kernel.Renderer().LastStaticTerrainTriangles() > 0);
    CHECK(Kernel.Renderer().CachedWorldTris().empty());

    // From below, the culled underside leaves the backdrop, as on the CPU path.
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(16.0f, -40.0f, 16.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(-90.0f, 0.0f, 0.0f, &Error); });
    CHECK(CenterIsBackdrop());

    // Moving the terrain out of the view rebuilds its buffer: nothing is drawn from above any more.
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(16.0f, 40.0f, 16.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(90.0f, 0.0f, 0.0f, &Error); });
    REQUIRE(Kernel.Matrices().PositionTerrain(1, 500.0f, 0.0f, 500.0f, &Error));
    CHECK(CenterIsBackdrop());
    CHECK(Kernel.Renderer().LastStaticTerrainTriangles() == 0);
    REQUIRE(Kernel.Matrices().PositionTerrain(1, 0.0f, 0.0f, 33.0f, &Error));
    CHECK_FALSE(CenterIsBackdrop());
    REQUIRE(Kernel.Matrices().DeleteTerrain(1, &Error));
    CHECK(CenterIsBackdrop());
}

TEST_CASE("Static GPU terrain keeps its render priority's place among the opaque batches")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("terrain-order", 64, 64));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE(Kernel.Core().EnsureInitialized());
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Heightmap =
        std::filesystem::temp_directory_path() / ("opendbc-terrain-order-" + std::to_string(Suffix) + ".bmp");
    SDL_Surface* Surface = SDL_CreateSurface(33, 33, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, 0, 0, 0, 255)));
    REQUIRE(SDL_SaveBMP(Surface, Heightmap.string().c_str()));
    SDL_DestroySurface(Surface);
    std::string Error;
    REQUIRE(Kernel.Matrices().MakeTerrain(1, Heightmap.string(), &Error));
    std::error_code Ignored;
    std::filesystem::remove(Heightmap, Ignored);
    // A GPU mesh above the terrain, its bounds drawn without depth writes at render priority 2.
    Kernel.Objects().MakeObjectCube(1, 4.0f);
    Kernel.Objects().PositionObject(1, 16.0f, 4.0f, 16.0f);
    REQUIRE(Kernel.Objects().ShowObjectBounds(1));
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Camera().PositionCamera(16.0f, 40.0f, 16.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(90.0f, 0.0f, 0.0f, &Error); });

    const dbc::RenderView View = Kernel.Renderer().CameraView(64, 64);
    dbc::PreparedViewGeometry Geometry;
    dbc::RenderFrameState Frame;
    REQUIRE(Kernel.Renderer().BuildPreparedView(View, Geometry, Frame));
    const auto FirstBatch = [&](auto&& Matches)
    {
        const auto Found = std::find_if(Geometry.Batches.begin(), Geometry.Batches.end(), Matches);
        REQUIRE(Found != Geometry.Batches.end());
        return Found - Geometry.Batches.begin();
    };
    const auto Terrain = FirstBatch([](const dbc::GpuBatch& Batch) { return Batch.StaticVertices && !Batch.MeshDraw; });
    const auto Mesh = FirstBatch([](const dbc::GpuBatch& Batch) { return Batch.MeshDraw != nullptr; });
    const auto Bounds =
        FirstBatch([](const dbc::GpuBatch& Batch) { return !Batch.StaticVertices && Batch.Style.Wireframe; });
    // As their CPU triangles: terrain at priority -1, objects at 0, bounds at 2.
    CHECK(Terrain < Mesh);
    CHECK(Mesh < Bounds);
}

TEST_CASE("Classic sound memblocks use the documented 12-byte header and accept a zero sample count")
{
    std::string MemblockError;
    std::string AudioError;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-sound-memblock", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    const std::filesystem::path SoundPath =
        std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/memblocks/behold.wav";

    REQUIRE(Kernel.Audio().LoadSound(SoundPath.string(), 1, false, false, &AudioError));
    REQUIRE(Kernel.Memblocks().MakeMemblockFromSound(1, 1, &AudioError));
    const int ClassicSize = ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                                  { return Kernel.Memblocks().MemblockSize(Output, 1, &Error); });
    REQUIRE(ClassicSize > 12);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 0, &Error); }) == 8);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 4, &Error); }) == 22050);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 8, &Error); }) ==
          ClassicSize - 12);

    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 4, 25000, &MemblockError));
    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 8, 0, &MemblockError));
    CHECK(Kernel.Memblocks().MakeSoundFromMemblock(2, 1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 2, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundSpeed(Output, 2, &Error); }) == 25000);

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(Kernel.Memblocks().MakeMemblockFromSound(2, 1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().MemblockSize(Output, 2, &Error); }) == ClassicSize + 16);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 0, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 4, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 8, &Error); }) == 22050);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 20, &Error); }) == 8);
    CHECK(Kernel.Memblocks().MakeSoundFromMemblock(3, 2, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 3, &Error); }) == 1);
}

TEST_CASE("Portable sound music 3D audio and memblocks preserve DBPro state")
{
    std::string MemblockError;
    std::string AudioError;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    const std::filesystem::path Root(TEST_SOURCE_DIR);
    const std::string SoundPath = (Root / "examples/dbpro/tutorials/sound/sound/ghost.wav").string();
    const std::string MusicPath = (Root / "examples/dbpro/tutorials/music/music/hero.mp3").string();

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    Context.AssetRoot = std::filesystem::path(SoundPath).parent_path();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadSound,
                          {BuiltinValue::FromString("ghost.wav"), BuiltinValue::FromNumber(8)}, Error));
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadSound,
                          {BuiltinValue::FromString("ghost.wav"), BuiltinValue::FromNumber(10)}, Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_LoadSound,
        {BuiltinValue::FromString("ghost.wav"), BuiltinValue::FromNumber(11), BuiltinValue::FromNumber(77)}, Error));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 10, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 11, &Error); }) == 1);
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Load3dsound,
                          {BuiltinValue::FromString("ghost.wav"), BuiltinValue::FromNumber(9)}, Error));
    Context.AssetRoot = std::filesystem::path(MusicPath).parent_path();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadMusic,
                          {BuiltinValue::FromString("hero.mp3"), BuiltinValue::FromNumber(2)}, Error));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 8, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundType(Output, 9, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicExist(Output, 2, &Error); }) == 1);

    REQUIRE(Kernel.Audio().LoadSound(SoundPath, 1, false, false, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 1, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundType(Output, 1, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundSpeed(Output, 1, &Error); }) >= 100);
    REQUIRE(Kernel.Audio().SetSoundPan(1, -2500, &AudioError));
    REQUIRE(Kernel.Audio().SetSoundVolume(1, 35, &AudioError));
    REQUIRE(Kernel.Audio().SetSoundSpeed(1, 12000, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPan(Output, 1, &Error); }) == -2500);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundVolume(Output, 1, &Error); }) == 35);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundSpeed(Output, 1, &Error); }) == 12000);

    REQUIRE(Kernel.Audio().CloneSound(2, 1, &AudioError));
    REQUIRE(Kernel.Audio().DeleteSound(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 2, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPan(Output, 2, &Error); }) == -2500);
    REQUIRE(Kernel.Memblocks().MakeMemblockFromSound(1, 2, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().MemblockSize(Output, 1, &Error); }) > 28);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 8, &Error); }) > 0);
    REQUIRE(Kernel.Audio().DeleteSound(2, &AudioError));
    REQUIRE(Kernel.Memblocks().MakeSoundFromMemblock(3, 1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 3, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundType(Output, 3, &Error); }) == 0);

    const auto SavedSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path SavedSound =
        std::filesystem::temp_directory_path() / ("opendbc-saved-sound-" + std::to_string(SavedSuffix) + ".wav");
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = SavedSound.parent_path();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SaveSound,
                          {BuiltinValue::FromString(SavedSound.filename().string()), BuiltinValue::FromNumber(3)},
                          Error));
    std::ifstream SavedInput(SavedSound, std::ios::binary);
    const std::vector<std::uint8_t> SavedBytes((std::istreambuf_iterator<char>(SavedInput)),
                                               std::istreambuf_iterator<char>());
    REQUIRE(SavedBytes.size() > 44);
    CHECK(std::memcmp(SavedBytes.data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(SavedBytes.data() + 8, "WAVE", 4) == 0);
    CHECK(std::memcmp(SavedBytes.data() + 36, "data", 4) == 0);
    REQUIRE(Kernel.Audio().LoadSound(SavedSound.string(), 12, false, false, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 12, &Error); }) == 1);
    CHECK_NOTHROW(Kernel.Audio().PumpRecording());
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_StopRecordingSound, {}, Error));
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_RecordSound,
                              {BuiltinValue::FromNumber(13), BuiltinValue::FromNumber(0)}, Error));
    CHECK(Error == "RECORD SOUND duration must be positive");
    std::error_code SavedError;
    std::filesystem::remove(SavedSound, SavedError);

    REQUIRE(Kernel.Audio().LoopSound(3, -1, -1, -1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPlaying(Output, 3, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundLooping(Output, 3, &Error); }) == 1);
    REQUIRE(Kernel.Audio().PauseSound(3, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPaused(Output, 3, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPlaying(Output, 3, &Error); }) == 1);
    REQUIRE(Kernel.Audio().ResumeSound(3, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPaused(Output, 3, &Error); }) == 0);
    REQUIRE(Kernel.Audio().StopSound(3, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPlaying(Output, 3, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundLooping(Output, 3, &Error); }) == 0);

    REQUIRE(Kernel.Audio().Load3dsound(SoundPath, 4, false, false, &AudioError));
    REQUIRE(Kernel.Audio().PositionSound(4, 100.0f, 200.0f, 300.0f, &AudioError));
    REQUIRE(Kernel.Audio().PositionListener(10.0f, 20.0f, 30.0f, &AudioError));
    REQUIRE(Kernel.Audio().RotateListener(5.0f, 15.0f, 25.0f, &AudioError));
    REQUIRE(Kernel.Audio().ScaleListener(0.5f, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundType(Output, 4, &Error); }) == 1);
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Audio().SoundPositionX(Output, 4, &Error); }) ==
          doctest::Approx(100.0f));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Audio().SoundPositionY(Output, 4, &Error); }) ==
          doctest::Approx(200.0f));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Audio().SoundPositionZ(Output, 4, &Error); }) ==
          doctest::Approx(300.0f));
    CHECK(Kernel.Audio().ListenerPositionX() == doctest::Approx(10.0f));
    CHECK(Kernel.Audio().ListenerAngleZ() == doctest::Approx(25.0f));

    REQUIRE(Kernel.Audio().LoadSound("missing-sound.wav", 5, true, false, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundExist(Output, 5, &Error); }) == 0);
    CHECK_FALSE(Kernel.Audio().LoadSound("missing-sound.wav", 5, false, false, &AudioError));
    CHECK(AudioError.find("LOAD SOUND failed") != std::string::npos);
    CHECK_FALSE(Kernel.Audio().SetSoundPan(4, 10001, &AudioError));
    CHECK(AudioError.find("between -10000 and 10000") != std::string::npos);

    REQUIRE(Kernel.Audio().LoadSound(SoundPath, 6, false, false, &AudioError));
    REQUIRE(Kernel.Audio().LoadSound(SoundPath, 7, false, true, &AudioError));
    REQUIRE(Kernel.Audio().LoopSound(6, -1, -1, -1, &AudioError));
    REQUIRE(Kernel.Audio().LoopSound(7, -1, -1, -1, &AudioError));
    Kernel.Core().SetSuspended(true);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPaused(Output, 6, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPaused(Output, 7, &Error); }) == 0);
    Kernel.Core().SetSuspended(false);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().SoundPaused(Output, 6, &Error); }) == 0);

    REQUIRE(Kernel.Audio().LoadMusic(MusicPath, 1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicExist(Output, 1, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicVolume(Output, 1, &Error); }) == 100);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicSpeed(Output, 1, &Error); }) == 100);
    REQUIRE(Kernel.Audio().SetMusicVolume(1, 140, &AudioError));
    REQUIRE(Kernel.Audio().SetMusicSpeed(1, 75, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicVolume(Output, 1, &Error); }) == 100);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicSpeed(Output, 1, &Error); }) == 75);
    REQUIRE(Kernel.Audio().PlayMusic(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicPlaying(Output, 1, &Error); }) == 1);
    REQUIRE(Kernel.Audio().PauseMusic(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicPaused(Output, 1, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicPlaying(Output, 1, &Error); }) == 0);
    REQUIRE(Kernel.Audio().ResumeMusic(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicPaused(Output, 1, &Error); }) == 0);
    REQUIRE(Kernel.Audio().LoopMusic(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicLooping(Output, 1, &Error); }) == 1);
    REQUIRE(Kernel.Audio().StopMusic(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicPlaying(Output, 1, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicLooping(Output, 1, &Error); }) == 1);
    REQUIRE(Kernel.Audio().DeleteMusic(1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicExist(Output, 1, &Error); }) == 0);
}

TEST_CASE("DarkBASIC scan codes retain the complete DirectInput key layout")
{
    using Pair = std::pair<int, SDL_Scancode>;
    const Pair Historical[] = {
        {1, SDL_SCANCODE_ESCAPE},          {41, SDL_SCANCODE_GRAVE},        {55, SDL_SCANCODE_KP_MULTIPLY},
        {58, SDL_SCANCODE_CAPSLOCK},       {69, SDL_SCANCODE_NUMLOCKCLEAR}, {70, SDL_SCANCODE_SCROLLLOCK},
        {74, SDL_SCANCODE_KP_MINUS},       {78, SDL_SCANCODE_KP_PLUS},      {83, SDL_SCANCODE_KP_PERIOD},
        {86, SDL_SCANCODE_NONUSBACKSLASH}, {87, SDL_SCANCODE_F11},          {88, SDL_SCANCODE_F12},
        {100, SDL_SCANCODE_F13},           {101, SDL_SCANCODE_F14},         {102, SDL_SCANCODE_F15},
        {156, SDL_SCANCODE_KP_ENTER},      {157, SDL_SCANCODE_RCTRL},       {174, SDL_SCANCODE_VOLUMEDOWN},
        {176, SDL_SCANCODE_VOLUMEUP},      {181, SDL_SCANCODE_KP_DIVIDE},   {183, SDL_SCANCODE_PRINTSCREEN},
        {184, SDL_SCANCODE_RALT},          {197, SDL_SCANCODE_PAUSE},       {219, SDL_SCANCODE_LGUI},
        {220, SDL_SCANCODE_RGUI},          {221, SDL_SCANCODE_APPLICATION}, {222, SDL_SCANCODE_POWER},
        {223, SDL_SCANCODE_SLEEP},         {227, SDL_SCANCODE_WAKE},        {229, SDL_SCANCODE_AC_SEARCH},
        {230, SDL_SCANCODE_AC_BOOKMARKS},  {231, SDL_SCANCODE_AC_REFRESH},  {232, SDL_SCANCODE_AC_STOP},
        {233, SDL_SCANCODE_AC_FORWARD},    {234, SDL_SCANCODE_AC_BACK},     {237, SDL_SCANCODE_MEDIA_SELECT},
    };
    for (const auto& [DarkBasic, Sdl] : Historical)
    {
        CAPTURE(DarkBasic);
        CHECK(dbc::InputSystem::SdlScancodeForDarkBasic(DarkBasic) == static_cast<int>(Sdl));
    }
    CHECK(dbc::InputSystem::SdlScancodeForDarkBasic(0) == static_cast<int>(SDL_SCANCODE_UNKNOWN));
}

TEST_CASE("ALWAYS ACTIVE commands control execution while the host is suspended")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_AlwaysActiveOff, {}, Error));
    CHECK_FALSE(Kernel.Core().AlwaysActive());

    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "suspension.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources.push_back({Request.RootLogicalPath, "value = 1\n"});
    dbc::interpreter::ExecutionService Service(Kernel);
    REQUIRE(Service.Prepare(std::move(Request)));
    Kernel.Core().SetSuspended(true);
    const dbc::interpreter::ExecutionTick Suspended = Service.Tick(32);
    CHECK(Suspended.State == dbc::interpreter::ExecutionState::Suspended);
    CHECK(Suspended.Instructions == 0);
    Kernel.Core().SetSuspended(false);
    CHECK(Service.Tick(32).State == dbc::interpreter::ExecutionState::Completed);

    Kernel.Core().SetSuspended(true);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_AlwaysActiveOn, {}, Error));
    CHECK(Kernel.Core().AlwaysActive());
    CHECK_FALSE(Kernel.Core().Suspended());
}

TEST_CASE("SLEEP and key waits suspend resume and cancel cooperatively")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    EngineBuiltinHost Host(Context);

    for (const auto Symbol : {dbc::language::SymbolId::Command_Sleep, dbc::language::SymbolId::Command_Wait})
    {
        const std::array Arguments{dbc::vm::Value::Integer(std::numeric_limits<std::int64_t>::max())};
        const auto Result = Host.Invoke({99, Symbol, Arguments, {dbc::language::TypeKind::Void, {}}, false});
        CHECK(Result.State == dbc::vm::BuiltinState::Failed);
        CHECK(Result.Error.find("clock range") != std::string::npos);
    }

    const std::array SleepArguments{dbc::vm::Value::Integer(1)};
    const dbc::vm::BuiltinInvocation Sleep{
        1, dbc::language::SymbolId::Command_Sleep, SleepArguments, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(Sleep).State == dbc::vm::BuiltinState::Pending);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    dbc::vm::BuiltinInvocation SleepRetry = Sleep;
    SleepRetry.IsRetry = true;
    CHECK(Host.Invoke(SleepRetry).State == dbc::vm::BuiltinState::Completed);

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    const std::array DbProSleepArguments{dbc::vm::Value::Integer(0)};
    const dbc::vm::BuiltinInvocation DbProSleep{
        4, dbc::language::SymbolId::Command_Sleep, DbProSleepArguments, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(DbProSleep).State == dbc::vm::BuiltinState::Completed);
    const dbc::vm::BuiltinInvocation WaitKey{
        2, dbc::language::SymbolId::Command_WaitKey, {}, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(WaitKey).State == dbc::vm::BuiltinState::Pending);
    SDL_Event KeyDown{};
    KeyDown.type = SDL_EVENT_KEY_DOWN;
    KeyDown.key.scancode = SDL_SCANCODE_A;
    KeyDown.key.key = SDLK_A;
    Kernel.Input().HandleEvent(KeyDown);
    dbc::vm::BuiltinInvocation WaitKeyRetry = WaitKey;
    WaitKeyRetry.IsRetry = true;
    CHECK(Host.Invoke(WaitKeyRetry).State == dbc::vm::BuiltinState::Completed);

    const dbc::vm::BuiltinInvocation DbProSuspend{
        5, dbc::language::SymbolId::Command_SuspendForKey, {}, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(DbProSuspend).State == dbc::vm::BuiltinState::Pending);
    Kernel.Input().HandleEvent(KeyDown);
    dbc::vm::BuiltinInvocation DbProSuspendRetry = DbProSuspend;
    DbProSuspendRetry.IsRetry = true;
    CHECK(Host.Invoke(DbProSuspendRetry).State == dbc::vm::BuiltinState::Completed);

    Context.Profile = dbc::language::LanguageProfile::Dbc;
    const dbc::vm::BuiltinInvocation SuspendInvocation{
        3, dbc::language::SymbolId::Command_SuspendForKey, {}, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(SuspendInvocation).State == dbc::vm::BuiltinState::Pending);
    std::string CancelError;
    REQUIRE(Host.Cancel(SuspendInvocation.Id, CancelError));
    dbc::vm::BuiltinInvocation CancelledRetry = SuspendInvocation;
    CancelledRetry.IsRetry = true;
    CHECK(Host.Invoke(CancelledRetry).State == dbc::vm::BuiltinState::Failed);

    const auto MouseButton = [&](Uint32 Type)
    {
        SDL_Event Event{};
        Event.type = Type;
        Event.button.button = SDL_BUTTON_LEFT;
        Kernel.Input().HandleEvent(Event);
    };
    const dbc::vm::BuiltinInvocation ClassicMouse{
        6, dbc::language::SymbolId::Command_SuspendForMouse, {}, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(ClassicMouse).State == dbc::vm::BuiltinState::Pending);
    dbc::vm::BuiltinInvocation ClassicMouseRetry = ClassicMouse;
    ClassicMouseRetry.IsRetry = true;
    CHECK(Host.Invoke(ClassicMouseRetry).State == dbc::vm::BuiltinState::Pending);
    MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN);
    CHECK(Host.Invoke(ClassicMouseRetry).State == dbc::vm::BuiltinState::Completed);

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    const dbc::vm::BuiltinInvocation WaitMouse{
        7, dbc::language::SymbolId::Command_WaitMouse, {}, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(WaitMouse).State == dbc::vm::BuiltinState::Pending);
    dbc::vm::BuiltinInvocation WaitMouseRetry = WaitMouse;
    WaitMouseRetry.IsRetry = true;
    CHECK(Host.Invoke(WaitMouseRetry).State == dbc::vm::BuiltinState::Pending);
    MouseButton(SDL_EVENT_MOUSE_BUTTON_UP);
    CHECK(Host.Invoke(WaitMouseRetry).State == dbc::vm::BuiltinState::Pending);
    MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN);
    CHECK(Host.Invoke(WaitMouseRetry).State == dbc::vm::BuiltinState::Completed);

    MouseButton(SDL_EVENT_MOUSE_BUTTON_UP);
    const dbc::vm::BuiltinInvocation DbProMouse{
        8, dbc::language::SymbolId::Command_SuspendForMouse, {}, {dbc::language::TypeKind::Void, {}}, false};
    CHECK(Host.Invoke(DbProMouse).State == dbc::vm::BuiltinState::Pending);
    MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN);
    dbc::vm::BuiltinInvocation DbProMouseRetry = DbProMouse;
    DbProMouseRetry.IsRetry = true;
    CHECK(Host.Invoke(DbProMouseRetry).State == dbc::vm::BuiltinState::Completed);
}

TEST_CASE("Classic input functions preserve held keys absolute mouse state and button masks")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    std::string Error;

    SDL_Event Motion{};
    Motion.type = SDL_EVENT_MOUSE_MOTION;
    Motion.motion.x = 27.0f;
    Motion.motion.y = 31.0f;
    Motion.motion.xrel = 4.0f;
    Motion.motion.yrel = -2.0f;
    Kernel.Input().HandleEvent(Motion);
    for (const Uint8 Button : {Uint8{SDL_BUTTON_LEFT}, Uint8{SDL_BUTTON_RIGHT}, Uint8{SDL_BUTTON_MIDDLE}})
    {
        SDL_Event Down{};
        Down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        Down.button.button = Button;
        Kernel.Input().HandleEvent(Down);
    }

    SDL_Event Letter{};
    Letter.type = SDL_EVENT_KEY_DOWN;
    Letter.key.scancode = SDL_SCANCODE_A;
    Letter.key.key = SDLK_A;
    Letter.key.mod = SDL_KMOD_NONE;
    Kernel.Input().HandleEvent(Letter);
    CHECK(Kernel.Input().Inkey() == "a");
    CHECK(Kernel.Input().Inkey() == "a");
    SDL_Event LetterUp{};
    LetterUp.type = SDL_EVENT_KEY_UP;
    LetterUp.key.scancode = SDL_SCANCODE_A;
    LetterUp.key.key = SDLK_A;
    Kernel.Input().HandleEvent(LetterUp);
    CHECK(Kernel.Input().Inkey().empty());

    Letter.key.mod = SDL_KMOD_SHIFT;
    Kernel.Input().HandleEvent(Letter);
    CHECK(Kernel.Input().Inkey() == "A");
    CHECK(Kernel.Input().Inkey() == "A");
    Kernel.Input().HandleEvent(LetterUp);
    CHECK(Kernel.Input().Inkey().empty());
    Letter.key.mod = SDL_KMOD_CAPS;
    Kernel.Input().HandleEvent(Letter);
    CHECK(Kernel.Input().Inkey() == "A");
    Kernel.Input().HandleEvent(LetterUp);
    Letter.key.mod = SDL_KMOD_SHIFT | SDL_KMOD_CAPS;
    Kernel.Input().HandleEvent(Letter);
    CHECK(Kernel.Input().Inkey() == "a");
    Kernel.Input().HandleEvent(LetterUp);
    Letter.key.mod = SDL_KMOD_SHIFT;

    const std::pair<SDL_Scancode, SDL_Keycode> Keys[] = {
        {SDL_SCANCODE_UP, SDLK_UP},       {SDL_SCANCODE_DOWN, SDLK_DOWN},   {SDL_SCANCODE_LEFT, SDLK_LEFT},
        {SDL_SCANCODE_RIGHT, SDLK_RIGHT}, {SDL_SCANCODE_SPACE, SDLK_SPACE}, {SDL_SCANCODE_RETURN, SDLK_RETURN},
    };
    for (const auto& [Scancode, Keycode] : Keys)
    {
        SDL_Event Down{};
        Down.type = SDL_EVENT_KEY_DOWN;
        Down.key.scancode = Scancode;
        Down.key.key = Keycode;
        Kernel.Input().HandleEvent(Down);
    }
    Kernel.Input().HandleEvent(Letter);

    for (const auto Profile : {dbc::language::LanguageProfile::Dbc, dbc::language::LanguageProfile::DbPro})
    {
        Context.Profile = Profile;
        const auto Number = [&](dbc::language::SymbolId Symbol)
        {
            BuiltinValue Result;
            Error.clear();
            REQUIRE(InvokeFunction(Context, Symbol, {}, Result, Error));
            return ReadBuiltinInteger(Result);
        };
        CHECK(Number(dbc::language::SymbolId::Function_Mousex) == 27);
        CHECK(Number(dbc::language::SymbolId::Function_Mousey) == 31);
        CHECK(Number(dbc::language::SymbolId::Function_Mouseclick) == 7);
        CHECK(Number(dbc::language::SymbolId::Function_Upkey) == 1);
        CHECK(Number(dbc::language::SymbolId::Function_Downkey) == 1);
        CHECK(Number(dbc::language::SymbolId::Function_Leftkey) == 1);
        CHECK(Number(dbc::language::SymbolId::Function_Rightkey) == 1);
        CHECK(Number(dbc::language::SymbolId::Function_Spacekey) == 1);
        CHECK(Number(dbc::language::SymbolId::Function_Returnkey) == 1);
        BuiltinValue InkeyResult;
        REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_InkeyString, {}, InkeyResult, Error));
        CHECK(InkeyResult.AsString() == "A");
    }
    Kernel.Input().HandleEvent(LetterUp);
    CHECK(Kernel.Input().Inkey().empty());
}

TEST_CASE("DBPro text keyboard mouse and escape controls preserve their historical state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    const auto Text = [&](const char* Value)
    {
        SDL_Event Event{};
        Event.type = SDL_EVENT_TEXT_INPUT;
        Event.text.text = Value;
        Kernel.Input().HandleEvent(Event);
    };
    const auto Key = [&](Uint32 Type, SDL_Scancode Scancode, SDL_Keycode Keycode, SDL_Keymod Mod = SDL_KMOD_NONE)
    {
        SDL_Event Event{};
        Event.type = Type;
        Event.key.scancode = Scancode;
        Event.key.key = Keycode;
        Event.key.mod = Mod;
        Kernel.Input().HandleEvent(Event);
    };

    std::string EntryText;
    REQUIRE(Kernel.Input().Entry(EntryText, 0, &Error));
    CHECK(EntryText.empty());
    Text("ab");
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
    Text("c");
    std::string RawEntry;
    REQUIRE(Kernel.Input().Entry(RawEntry, 0, &Error));
    CHECK(RawEntry == std::string("ab\bc", 4));
    REQUIRE(Kernel.Input().Entry(EntryText, 1, &Error));
    CHECK(EntryText == "ac");
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ClearEntryBuffer, {}, Error));
    REQUIRE(Kernel.Input().Entry(EntryText, 0, &Error));
    CHECK(EntryText.empty());

    REQUIRE(Kernel.Input().BeginTextInput());
    Text("xy");
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
    CHECK(Kernel.Input().TakeTextInput().empty());
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(Kernel.Input().TakeTextInput() == "x");
    std::string StopError;
    REQUIRE(Kernel.Input().EndTextInput(&StopError));

    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LCTRL, SDLK_LCTRL);
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT);
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_A, SDLK_A);
    CHECK(Kernel.Input().Controlkey() == 1);
    CHECK(Kernel.Input().Shiftkey() == 1);
    CHECK(Kernel.Input().Keystate(30) == 1);
    CHECK(Kernel.Input().GetKeyState(0x41) < 0);
    Key(SDL_EVENT_KEY_UP, SDL_SCANCODE_A, SDLK_A);
    Key(SDL_EVENT_KEY_UP, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT);
    Key(SDL_EVENT_KEY_UP, SDL_SCANCODE_LCTRL, SDLK_LCTRL);

    const SDL_Keymod OriginalModifiers = SDL_GetModState();
    SDL_SetModState(SDL_KMOD_NONE);
    REQUIRE(Kernel.Input().SetKeyStateToggle(0x14, 1, &Error));
    CHECK((Kernel.Input().GetKeyState(0x14) & 1) == 1);
    REQUIRE(Kernel.Input().SetKeyStateToggle(0x14, 0, &Error));
    CHECK((Kernel.Input().GetKeyState(0x14) & 1) == 0);
    SDL_SetModState(OriginalModifiers);

    SDL_Event Motion{};
    Motion.type = SDL_EVENT_MOUSE_MOTION;
    Motion.motion.x = 12.0f;
    Motion.motion.y = 13.0f;
    Motion.motion.xrel = 5.0f;
    Motion.motion.yrel = -3.0f;
    Kernel.Input().HandleEvent(Motion);
    SDL_Event Wheel{};
    Wheel.type = SDL_EVENT_MOUSE_WHEEL;
    Wheel.wheel.y = 2.0f;
    Kernel.Input().HandleEvent(Wheel);
    CHECK(Kernel.Input().Mousemovex() == 5);
    CHECK(Kernel.Input().Mousemovex() == 0);
    CHECK(Kernel.Input().Mousemovey() == -3);
    CHECK(Kernel.Input().Mousemovez() == 240);
    CHECK(Kernel.Input().Mousez() == 240);
    REQUIRE(Kernel.Input().PositionMouse(7, 8, &Error));
    CHECK(Kernel.Input().Mousex() == 7);
    CHECK(Kernel.Input().Mousey() == 8);
    REQUIRE(Kernel.Input().ChangeMouse(0, 1, &Error));
    CHECK_FALSE(Kernel.Input().ChangeMouse(2, &Error));
    CHECK(Error.find("cursor assets") != std::string::npos);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_DisableEscapekey, {}, Error));
    Kernel.Core().Running() = true;
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    CHECK(Kernel.Core().Running());
    CHECK(Kernel.Input().Escapekey() == 1);
    Key(SDL_EVENT_KEY_UP, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_EnableEscapekey, {}, Error));
    Kernel.Core().Running() = true;
    Key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    CHECK_FALSE(Kernel.Core().Running());

    CHECK(dbc::InputSystem::IsDirectTouchDevice(SDL_TOUCH_DEVICE_DIRECT));
    CHECK_FALSE(dbc::InputSystem::IsDirectTouchDevice(SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE));
    CHECK_FALSE(dbc::InputSystem::IsDirectTouchDevice(SDL_TOUCH_DEVICE_INDIRECT_RELATIVE));
    SDL_Event IndirectTouch{};
    IndirectTouch.type = SDL_EVENT_FINGER_DOWN;
    IndirectTouch.tfinger.touchID = SDL_MOUSE_TOUCHID;
    IndirectTouch.tfinger.fingerID = 1;
    IndirectTouch.tfinger.x = 0.5f;
    IndirectTouch.tfinger.y = 0.1f;
    Kernel.Input().HandleEvent(IndirectTouch);
    CHECK(Kernel.Input().Upkey() == 0);
}

TEST_CASE("SDL virtual joysticks expose the DBPro control device model")
{
    SDL_JoystickID Identifier = 0;
    {
        dbc::EngineKernel Kernel;
        REQUIRE(Kernel.Initialize("test", 32, 24));
        SDL_VirtualJoystickDesc Description{};
        SDL_INIT_INTERFACE(&Description);
        Description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        Description.naxes = 10;
        Description.nbuttons = 8;
        Description.nhats = 2;
        Description.name = "OpenDBC Virtual Controller";
        Identifier = SDL_AttachVirtualJoystick(&Description);
        REQUIRE(Identifier != 0);
        BuiltinContext Context;
        Context.Kernel = &Kernel;
        Context.Profile = dbc::language::LanguageProfile::DbPro;
        std::string Error;
        std::string InputError;
        REQUIRE(Kernel.Input().SetControlDevice("OpenDBC Virtual Controller", 0, &InputError));
        SDL_Joystick* Device = SDL_GetJoystickFromID(Identifier);
        REQUIRE(Device != nullptr);
        const std::array<int16_t, 10> Axes = {-20000, 16384, 8192, -8192, 4096, -4096, 12000, -12000, 6000, -6000};
        for (uint8_t Axis = 0; Axis < Axes.size(); ++Axis)
        {
            SDL_Event Event{};
            Event.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
            Event.jaxis.which = Identifier;
            Event.jaxis.axis = Axis;
            Event.jaxis.value = Axes[Axis];
            Kernel.Input().HandleEvent(Event);
        }
        for (uint8_t Button : {uint8_t{0}, uint8_t{7}})
        {
            SDL_Event Event{};
            Event.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
            Event.jbutton.which = Identifier;
            Event.jbutton.button = Button;
            Event.jbutton.down = true;
            Kernel.Input().HandleEvent(Event);
        }
        SDL_Event Hat{};
        Hat.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
        Hat.jhat.which = Identifier;
        Hat.jhat.hat = 0;
        Hat.jhat.value = SDL_HAT_RIGHTUP;
        Kernel.Input().HandleEvent(Hat);

        CHECK(Kernel.Input().ControlDeviceName() == "OpenDBC Virtual Controller");
        CHECK(Kernel.Input().JoystickX() == doctest::Approx(-610).epsilon(0.01));
        CHECK(Kernel.Input().JoystickY() == doctest::Approx(500).epsilon(0.01));
        CHECK(Kernel.Input().JoystickZ() == doctest::Approx(250).epsilon(0.01));
        CHECK(Kernel.Input().JoystickTwistX() == doctest::Approx(-250).epsilon(0.01));
        CHECK(Kernel.Input().JoystickTwistY() == doctest::Approx(125).epsilon(0.02));
        CHECK(Kernel.Input().JoystickTwistZ() == doctest::Approx(-125).epsilon(0.02));
        CHECK(Kernel.Input().JoystickSliderA() == doctest::Approx(366).epsilon(0.01));
        CHECK(Kernel.Input().JoystickSliderB() == doctest::Approx(-366).epsilon(0.01));
        CHECK(Kernel.Input().JoystickSliderC() == doctest::Approx(183).epsilon(0.02));
        CHECK(Kernel.Input().JoystickSliderD() == doctest::Approx(-183).epsilon(0.02));
        CHECK(Kernel.Input().JoystickFireA() == 1);
        CHECK(Kernel.Input().JoystickFireX(7) == 1);
        CHECK(Kernel.Input().JoystickFireX(8) == 0);
        CHECK(Kernel.Input().JoystickHatAngle(0) == 4500);
        CHECK(Kernel.Input().JoystickHatAngle(2) == -1);
        Kernel.Input().SetJoystickDeadzone(70);
        CHECK(Kernel.Input().JoystickLeft() == 0);
        Kernel.Input().SetJoystickDeadzone(50);
        CHECK(Kernel.Input().JoystickLeft() == 1);
        CHECK(Kernel.Input().JoystickInformation(0) == 8);
        CHECK(Kernel.Input().JoystickInformation(2) == 1);
        CHECK(Kernel.Input().JoystickInformation(5) == 1);
        CHECK(Kernel.Input().JoystickInformation(6) == 2);
        CHECK(Kernel.Input().JoystickInformation(7) == 2);
        CHECK(Kernel.Input().JoystickInformation(8) == 50);

        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForControlDevices, {}, Error));
        const auto Iterator = std::find_if(Context.Checklist.begin(), Context.Checklist.end(),
                                           [](const auto& Item) { return Item.Name == "OpenDBC Virtual Controller"; });
        REQUIRE(Iterator != Context.Checklist.end());
        CHECK(Iterator->A == 0);
        CHECK(Iterator->B == 0);
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ForceShoot,
                              {BuiltinValue::FromNumber(100), BuiltinValue::FromNumber(100)}, Error));
    }
    CHECK(SDL_DetachVirtualJoystick(Identifier));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("no-haptic", 16, 16));
    CHECK_NOTHROW(Kernel.Input().ForceShoot(100, 100));
}

TEST_CASE("Every implemented value builtin has one typed engine handler")
{
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (Descriptor.Kind != dbc::language::SymbolKind::Command &&
            Descriptor.Kind != dbc::language::SymbolKind::Function)
            continue;
        const dbc::language::BuiltinConvention Convention = dbc::language::ConventionFor(Descriptor.Id);
        const bool NeedsHandler =
            Descriptor.ImplementedProfiles != 0 && (Convention == dbc::language::BuiltinConvention::Values ||
                                                    Convention == dbc::language::BuiltinConvention::Print);
        CAPTURE(Descriptor.CanonicalName);
        CHECK(HasBuiltinHandler(Descriptor.Id) == NeedsHandler);
    }
}

TEST_CASE("Typed builtin ids dispatch implemented handlers and profile-isolated compatibility stubs")
{
    dbc::EngineKernel Kernel;
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    EngineBuiltinHost Host(Context);

    const std::vector<dbc::vm::Value> Arguments{dbc::vm::Value::Real(-3.5)};
    const dbc::vm::BuiltinResult Absolute =
        Host.Invoke({1, dbc::language::SymbolId::Function_Abs, Arguments, {dbc::language::TypeKind::Real, {}}, false});
    REQUIRE(Absolute.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(Absolute.ReturnValue);
    CHECK(Absolute.ReturnValue->AsReal() == doctest::Approx(3.5));

    const dbc::vm::BuiltinResult FtpStatus = Host.Invoke(
        {2, dbc::language::SymbolId::Function_GetFtpStatus, {}, {dbc::language::TypeKind::Real, {}}, false});
    REQUIRE(FtpStatus.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(FtpStatus.ReturnValue);
    CHECK(FtpStatus.ReturnValue->AsReal() == doctest::Approx(0.0));

    const dbc::vm::BuiltinResult FtpFailure = Host.Invoke(
        {3, dbc::language::SymbolId::Function_GetFtpFailure, {}, {dbc::language::TypeKind::Real, {}}, false});
    REQUIRE(FtpFailure.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(FtpFailure.ReturnValue);
    CHECK(FtpFailure.ReturnValue->AsReal() == doctest::Approx(1.0));

    const dbc::vm::BuiltinResult FtpProgress = Host.Invoke(
        {4, dbc::language::SymbolId::Function_GetFtpProgress, {}, {dbc::language::TypeKind::Real, {}}, false});
    REQUIRE(FtpProgress.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(FtpProgress.ReturnValue);
    CHECK(FtpProgress.ReturnValue->AsReal() == doctest::Approx(-1.0));

    const dbc::vm::BuiltinResult FtpType = Host.Invoke(
        {5, dbc::language::SymbolId::Function_GetFtpFileType, {}, {dbc::language::TypeKind::Real, {}}, false});
    REQUIRE(FtpType.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(FtpType.ReturnValue);
    CHECK(FtpType.ReturnValue->AsReal() == doctest::Approx(-1.0));

    const dbc::vm::BuiltinResult EmptyString = Host.Invoke(
        {6, dbc::language::SymbolId::Function_GetFtpErrorString, {}, {dbc::language::TypeKind::String, {}}, false});
    REQUIRE(EmptyString.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(EmptyString.ReturnValue);
    CHECK(EmptyString.ReturnValue->AsString().empty());

    const std::vector<dbc::vm::Value> CdArguments{dbc::vm::Value::Integer(1), dbc::vm::Value::Integer(17)};
    CHECK(Host.Invoke({10,
                       dbc::language::SymbolId::Command_LoadCdmusic,
                       CdArguments,
                       {dbc::language::TypeKind::Void, {}},
                       false})
              .State == dbc::vm::BuiltinState::Completed);
    CHECK(Context.InertMusicIds.contains(17));
    const std::vector<dbc::vm::Value> MusicId{dbc::vm::Value::Integer(17)};
    for (const dbc::language::SymbolId Command : {
             dbc::language::SymbolId::Command_PlayMusic,
             dbc::language::SymbolId::Command_LoopMusic,
             dbc::language::SymbolId::Command_StopMusic,
             dbc::language::SymbolId::Command_PauseMusic,
             dbc::language::SymbolId::Command_ResumeMusic,
         })
    {
        CHECK(Host.Invoke({11, Command, MusicId, {dbc::language::TypeKind::Void, {}}, false}).State ==
              dbc::vm::BuiltinState::Completed);
    }
    for (const dbc::language::SymbolId Function : {
             dbc::language::SymbolId::Function_MusicExist,
             dbc::language::SymbolId::Function_MusicPlaying,
             dbc::language::SymbolId::Function_MusicLooping,
             dbc::language::SymbolId::Function_MusicPaused,
         })
    {
        const dbc::vm::BuiltinResult Query =
            Host.Invoke({14, Function, MusicId, {dbc::language::TypeKind::Real, {}}, false});
        REQUIRE(Query.State == dbc::vm::BuiltinState::Completed);
        REQUIRE(Query.ReturnValue);
        CHECK(Query.ReturnValue->AsReal() == doctest::Approx(0.0));
    }
    CHECK(
        Host.Invoke(
                {15, dbc::language::SymbolId::Command_DeleteMusic, MusicId, {dbc::language::TypeKind::Void, {}}, false})
            .State == dbc::vm::BuiltinState::Completed);
    CHECK_FALSE(Context.InertMusicIds.contains(17));

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    const dbc::vm::BuiltinResult Unsupported = Host.Invoke(
        {16, dbc::language::SymbolId::Function_GetFtpStatus, {}, {dbc::language::TypeKind::Integer, {}}, false});
    CHECK(Unsupported.State == dbc::vm::BuiltinState::Failed);
    CHECK(Unsupported.Error == "Unsupported function: GET FTP STATUS");
}

TEST_CASE("Every permanent Classic compatibility stub dispatches its typed neutral contract")
{
    using dbc::language::BuiltinParameterMode;
    using dbc::language::BuiltinValueType;
    using dbc::language::CompatibilityStubBehavior;
    using dbc::language::CompatibilityStubPolicy;
    using dbc::language::LanguageProfile;
    using dbc::language::SymbolId;
    using dbc::language::SymbolKind;
    using dbc::language::Type;
    using dbc::language::TypeKind;

    const auto RuntimeType = [](BuiltinValueType Value)
    {
        switch (Value)
        {
            case BuiltinValueType::Integer:
                return Type{TypeKind::Integer, {}};
            case BuiltinValueType::String:
                return Type{TypeKind::String, {}};
            case BuiltinValueType::Void:
                return Type{TypeKind::Void, {}};
            case BuiltinValueType::Any:
            case BuiltinValueType::Numeric:
            case BuiltinValueType::Real:
                return Type{TypeKind::Real, {}};
        }
        return Type{TypeKind::Error, {}};
    };
    const auto ArgumentFor = [](BuiltinValueType Value)
    {
        switch (Value)
        {
            case BuiltinValueType::String:
                return dbc::vm::Value::String("stub");
            case BuiltinValueType::Integer:
                return dbc::vm::Value::Integer(0);
            case BuiltinValueType::Any:
            case BuiltinValueType::Numeric:
            case BuiltinValueType::Real:
                return dbc::vm::Value::Real(0.0);
            case BuiltinValueType::Void:
                break;
        }
        return dbc::vm::Value{};
    };

    dbc::EngineKernel Kernel;
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = LanguageProfile::Dbc;
    EngineBuiltinHost Host(Context);
    std::set<SymbolId> Exercised;
    std::uint64_t InvocationId = 1;

    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (dbc::language::StubPolicyFor(Descriptor, LanguageProfile::Dbc) != CompatibilityStubPolicy::Permanent)
        {
            continue;
        }
        CAPTURE(Descriptor.CanonicalName);
        const CompatibilityStubBehavior Behavior = dbc::language::StubBehaviorFor(Descriptor, LanguageProfile::Dbc);
        REQUIRE(Behavior != CompatibilityStubBehavior::None);
        const auto Overloads = dbc::language::Overloads(Descriptor.Id, LanguageProfile::Dbc);
        REQUIRE_FALSE(Overloads.empty());
        Exercised.insert(Descriptor.Id);

        for (const dbc::language::BuiltinOverload* Overload : Overloads)
        {
            REQUIRE(Overload != nullptr);
            std::vector<dbc::vm::Value> Arguments;
            Arguments.reserve(Overload->Parameters.size());
            for (const dbc::language::BuiltinParameter& Parameter : Overload->Parameters)
            {
                CHECK(Parameter.Mode == BuiltinParameterMode::Value);
                Arguments.push_back(ArgumentFor(Parameter.ValueType));
            }

            Context.Checklist = {{"sentinel"}};
            const dbc::vm::BuiltinResult Result =
                Host.Invoke({InvocationId++, Descriptor.Id, Arguments, RuntimeType(Overload->ReturnType), false});
            REQUIRE(Result.State == dbc::vm::BuiltinState::Completed);
            if (Descriptor.Kind == SymbolKind::Command)
            {
                CHECK_FALSE(Result.ReturnValue.has_value());
                REQUIRE(Context.Checklist.size() == 1);
                CHECK(Context.Checklist.front().Name == "sentinel");
                continue;
            }

            REQUIRE(Result.ReturnValue.has_value());
            if (Behavior == CompatibilityStubBehavior::EmptyString)
            {
                CHECK(Result.ReturnValue->Type().Kind == TypeKind::String);
                CHECK(Result.ReturnValue->AsString().empty());
                continue;
            }
            const double Expected = Behavior == CompatibilityStubBehavior::NumericOne
                                        ? 1.0
                                        : (Behavior == CompatibilityStubBehavior::NumericNegativeOne ? -1.0 : 0.0);
            if (Result.ReturnValue->Type().Kind == TypeKind::Integer)
                CHECK(Result.ReturnValue->AsInteger() == static_cast<std::int64_t>(Expected));
            else
                CHECK(Result.ReturnValue->AsReal() == doctest::Approx(Expected));
        }
    }
    CHECK(Exercised.size() == 29);
}

TEST_CASE("Compatibility stub logging is debug-only and once per symbol per execution host")
{
    ScopedStubLogger Log;
    dbc::EngineKernel Kernel;
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;

    {
        EngineBuiltinHost Host(Context);
        const auto InvokeStatus = [&]
        {
            return Host.Invoke(
                {1, dbc::language::SymbolId::Function_GetFtpStatus, {}, {dbc::language::TypeKind::Real, {}}, false});
        };
        CHECK(InvokeStatus().State == dbc::vm::BuiltinState::Completed);
        CHECK(InvokeStatus().State == dbc::vm::BuiltinState::Completed);
        CHECK(Host.Invoke({2,
                           dbc::language::SymbolId::Function_GetFtpFailure,
                           {},
                           {dbc::language::TypeKind::Real, {}},
                           false})
                  .State == dbc::vm::BuiltinState::Completed);
    }
    {
        EngineBuiltinHost NextExecution(Context);
        CHECK(
            NextExecution
                .Invoke(
                    {3, dbc::language::SymbolId::Function_GetFtpStatus, {}, {dbc::language::TypeKind::Real, {}}, false})
                .State == dbc::vm::BuiltinState::Completed);
    }

    std::size_t StatusLogs = 0;
    std::size_t FailureLogs = 0;
    for (const StubLogSink::Entry& Entry : Log.Entries())
    {
        CAPTURE(Entry.Message);
        CHECK(Entry.Level == spdlog::level::debug);
        if (Entry.Message == "Compatibility stub: GET FTP STATUS")
            ++StatusLogs;
        else if (Entry.Message == "Compatibility stub: GET FTP FAILURE")
            ++FailureLogs;
        else
            FAIL_CHECK("Unexpected compatibility-stub log entry");
    }
    CHECK(StatusLogs == 2);
    CHECK(FailureLogs == 1);
}

TEST_CASE("DBPro vector state is owned by the builtin context")
{
    BuiltinContext Context;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.Vectors[1] = {2.0f, 3.0f, 4.0f};
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MultiplyVector3,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2.5)}, Error));
    CHECK(Context.Vectors[1][0] == doctest::Approx(5.0f));
    CHECK(Context.Vectors[1][1] == doctest::Approx(7.5f));
    CHECK(Context.Vectors[1][2] == doctest::Approx(10.0f));
}

TEST_CASE("DBPro vector and matrix values share ids and reject missing operands")
{
    BuiltinContext Context;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeVector2, {BuiltinValue::FromNumber(7)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetVector2,
                          {BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(4)},
                          Error));
    REQUIRE(Context.Vectors2.contains(7));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeVector4, {BuiltinValue::FromNumber(7)}, Error));
    CHECK_FALSE(Context.Vectors2.contains(7));
    CHECK(Context.Vectors4.contains(7));

    BuiltinValue Result;
    CHECK_FALSE(InvokeFunction(Context, dbc::language::SymbolId::Function_XVector2, {BuiltinValue::FromNumber(7)},
                               Result, Error));
    CHECK(Error.find("requires existing vector 7") != std::string::npos);

    Error.clear();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeVector4, {BuiltinValue::FromNumber(8)}, Error));
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_AddVector4,
                              {BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(9)},
                              Error));
    CHECK(Error.find("requires existing vector 9") != std::string::npos);

    Error.clear();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeMatrix4, {BuiltinValue::FromNumber(7)}, Error));
    CHECK_FALSE(Context.Vectors4.contains(7));
    CHECK(Context.Matrices4.contains(7));
    CHECK(Context.Matrices4.at(7) == std::array<float, 16>{});

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetIdentityMatrix4,
                              {BuiltinValue::FromNumber(8)}, Error));
    CHECK(Error.find("requires existing matrix 8") != std::string::npos);
}

TEST_CASE("DBPro object render flags drive portable renderer state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    Kernel.Objects().MakeObjectCube(1, 1.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    const auto SetFlag = [&](dbc::language::SymbolId Id, int Value)
    { REQUIRE(InvokeCommand(Context, Id, {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(Value)}, Error)); };
    SetFlag(dbc::language::SymbolId::Command_SetObjectWireframe, 1);
    SetFlag(dbc::language::SymbolId::Command_SetObjectCull, 1);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectFilter,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0)},
                          Error));
    SetFlag(dbc::language::SymbolId::Command_SetObjectLight, 0);
    SetFlag(dbc::language::SymbolId::Command_SetObjectFog, 0);
    SetFlag(dbc::language::SymbolId::Command_SetObjectAmbient, 0);

    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    CHECK(Object->Wireframe);
    CHECK(Object->Cull);
    CHECK_FALSE(Object->TextureFilter);
    CHECK_FALSE(Object->UseLighting);
    CHECK_FALSE(Object->UseFog);
    CHECK_FALSE(Object->UseAmbient);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectCull,
                              {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Error.find("SET OBJECT CULL requires existing object 99") != std::string::npos);
}

TEST_CASE("DBPro object materials drive the stock shader")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    Kernel.Lights().SetAmbientLight(0);
    {
        std::string LightError;
        REQUIRE(Kernel.Lights().HideLight(0, &LightError));
    }

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Material = [&](dbc::language::SymbolId Id, double Value)
    { REQUIRE(InvokeCommand(Context, Id, {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(Value)}, Error)); };

    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_SetObjectDiffuse,
        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0xFFFFFF), BuiltinValue::FromNumber(1)}, Error));
    Material(dbc::language::SymbolId::Command_SetObjectAmbience, 0xFFFFFF);
    Material(dbc::language::SymbolId::Command_SetObjectSpecular, 0xFFFFFF);
    Material(dbc::language::SymbolId::Command_SetObjectSpecularPower, 7.5);
    Material(dbc::language::SymbolId::Command_SetObjectEmissive, 0xFF0000);

    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    CHECK(Object->DiffuseColor == 0xFFFFFFFFu);
    CHECK(Object->AmbientColor == 0xFFFFFFu);
    CHECK(Object->SpecularColor == 0xFFFFFFu);
    CHECK(Object->SpecularPower == doctest::Approx(7.5f));
    CHECK(Object->EmissiveColor == 0xFF0000u);
    REQUIRE_FALSE(Object->Meshes.empty());
    REQUIRE_FALSE(Object->Meshes.front().Geometry().DiffuseColors.empty());
    CHECK(Object->Meshes.front().Geometry().DiffuseColors.front() == 0xFFFFFFFFu);
    REQUIRE(Kernel.Core().EnsureInitialized());

    const auto CaptureCenter = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        Uint8 Red = 0;
        Uint8 Green = 0;
        Uint8 Blue = 0;
        Uint8 Alpha = 0;
        REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
        SDL_DestroySurface(Capture);
        return std::array<Uint8, 3>{Red, Green, Blue};
    };

    const auto RedEmissive = CaptureCenter();
    // Drawn from GPU buffers, with the material in its per-draw record.
    REQUIRE(Kernel.Renderer().CachedMeshDraws().size() == 1);
    CHECK(Kernel.Renderer().CachedMeshDraws().front().Style.EmissiveColor == 0xFF0000u);
    CHECK(RedEmissive[0] >= 240);
    CHECK(RedEmissive[1] <= 16);
    CHECK(RedEmissive[2] <= 16);

    Material(dbc::language::SymbolId::Command_SetObjectEmissive, 0x00FF00);
    const auto GreenEmissive = CaptureCenter();
    CHECK(GreenEmissive[0] <= 16);
    CHECK(GreenEmissive[1] >= 240);
    CHECK(GreenEmissive[2] <= 16);

    Material(dbc::language::SymbolId::Command_SetObjectEmissive, 0x000000);
    Material(dbc::language::SymbolId::Command_SetObjectDiffuse, 0x0000FF);
    Material(dbc::language::SymbolId::Command_SetObjectAmbience, 0xFFFFFF);
    Kernel.Lights().SetAmbientLight(100);
    const auto BlueDiffuse = CaptureCenter();
    CHECK(BlueDiffuse[0] <= 16);
    CHECK(BlueDiffuse[1] <= 16);
    CHECK(BlueDiffuse[2] >= 240);

    Material(dbc::language::SymbolId::Command_SetObjectDiffuse, 0xFFFFFF);
    Material(dbc::language::SymbolId::Command_SetObjectAmbience, 0xFF0000);
    const auto RedAmbient = CaptureCenter();
    CHECK(RedAmbient[0] >= 240);
    CHECK(RedAmbient[1] <= 16);
    CHECK(RedAmbient[2] <= 16);

    Kernel.Lights().SetAmbientLight(0);
    {
        std::string LightError;
        REQUIRE(Kernel.Lights().ShowLight(0, &LightError));
    }
    {
        std::string LightError;
        REQUIRE(Kernel.Lights().SetDirectionalLight(0, 0.0f, 0.0f, 1.0f, &LightError));
    }
    Material(dbc::language::SymbolId::Command_SetObjectDiffuse, 0x000000);
    Material(dbc::language::SymbolId::Command_SetObjectSpecularPower, 1.0);
    Material(dbc::language::SymbolId::Command_SetObjectSpecular, 0xFFFFFF);
    const auto WhiteSpecular = CaptureCenter();
    CHECK(WhiteSpecular[0] >= 90);
    CHECK(WhiteSpecular[1] >= 90);
    CHECK(WhiteSpecular[2] >= 90);
    Material(dbc::language::SymbolId::Command_SetObjectSpecular, 0x000000);
    const auto BlackSpecular = CaptureCenter();
    CHECK(BlackSpecular[0] <= 16);
    CHECK(BlackSpecular[1] <= 16);
    CHECK(BlackSpecular[2] <= 16);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectSpecular,
                              {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(0xFFFFFF)}, Error));
    CHECK(Error.find("SET OBJECT SPECULAR requires existing object 99") != std::string::npos);
}

TEST_CASE("DBPro transparency modes preserve render phases depth writes and alpha testing")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 4.0f, 4.0f));
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    REQUIRE(Kernel.Core().EnsureInitialized());

    SDL_Surface* Surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, 255, 0, 0, 128)));
    Kernel.ImageStoreRef().StoreImageFromSurface(1, Surface);
    SDL_DestroySurface(Surface);
    Kernel.Objects().TextureObject(1, 1);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    const auto SetMode = [&](int Mode)
    {
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTransparency,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(Mode)}, Error));
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        Uint8 Red = 0;
        Uint8 Green = 0;
        Uint8 Blue = 0;
        Uint8 Alpha = 0;
        REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
        SDL_DestroySurface(Capture);
        REQUIRE_FALSE(Kernel.Renderer().CachedWorldTris().empty());
        return std::pair<Uint8, dbc::Triangle>{Red, Kernel.Renderer().CachedWorldTris().front()};
    };

    const auto [OpaqueRed, Mode0] = SetMode(0);
    CHECK(OpaqueRed >= 240);
    CHECK_FALSE(Mode0.Transparent);
    CHECK_FALSE(Mode0.AlphaDepthWrite);
    CHECK_FALSE(Mode0.VeryEarly);

    const auto [AlphaRed, Mode1] = SetMode(1);
    CHECK(AlphaRed > 0);
    CHECK(AlphaRed < OpaqueRed);
    CHECK_FALSE(Mode1.Transparent);
    CHECK(Mode1.AlphaDepthWrite);

    const auto [Mode2Red, Mode2] = SetMode(2);
    CHECK(Mode2Red > 0);
    CHECK(Mode2.Transparent);
    CHECK_FALSE(Mode2.AlphaDepthWrite);

    const auto [Mode4Red, Mode4] = SetMode(4);
    CHECK(Mode4Red == 0);
    CHECK_FALSE(Mode4.Transparent);
    CHECK(Mode4.AlphaDepthWrite);
    CHECK(Mode4.Style.AlphaTestThreshold == doctest::Approx(207.0f / 255.0f));

    const auto [Mode6Red, Mode6] = SetMode(6);
    CHECK(Mode6Red == 0);
    CHECK(Mode6.Transparent);
    CHECK_FALSE(Mode6.AlphaDepthWrite);
    CHECK(Mode6.Style.AlphaTestThreshold == doctest::Approx(207.0f / 255.0f));

    const auto [Mode7Red, Mode7] = SetMode(7);
    CHECK(Mode7Red >= 240);
    CHECK_FALSE(Mode7.Transparent);
    CHECK_FALSE(Mode7.AlphaDepthWrite);
    CHECK(Mode7.VeryEarly);

    const auto [FallbackRed, Fallback] = SetMode(8);
    CHECK(FallbackRed > 0);
    CHECK_FALSE(Fallback.Transparent);
    CHECK(Fallback.AlphaDepthWrite);

    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 8.0f);
    const auto [WaterRed, Water] = SetMode(5);
    CHECK(WaterRed > 0);
    CHECK(Water.Transparent);
    CHECK(Water.Depth > Kernel.Camera().ClampedFarPlane());

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(3),
                           BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0)},
                          Error));
    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    CHECK(Object->Wireframe);
    CHECK(Object->TransparencyMode == 3);
    CHECK(Object->TransparentBlack);
    CHECK(Object->Cull);
    CHECK_FALSE(Object->TextureFilter);
    CHECK_FALSE(Object->UseLighting);
    CHECK_FALSE(Object->UseFog);
    CHECK_FALSE(Object->UseAmbient);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTransparency,
                              {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Error.find("SET OBJECT TRANSPARENCY requires existing object 99") != std::string::npos);
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObject,
                              {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0),
                               BuiltinValue::FromNumber(1)},
                              Error));
    CHECK(Error.find("SET OBJECT requires existing object 99") != std::string::npos);
}

TEST_CASE("Portable normal normalization and DBPro auxiliary texture stages reach the stock shader")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 4.0f, 4.0f));
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));

    const auto StoreColor = [&](int ImageId, Uint8 Red, Uint8 Green, Uint8 Blue)
    {
        SDL_Surface* Surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(Surface != nullptr);
        REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, Red, Green, Blue, 255)));
        Kernel.ImageStoreRef().StoreImageFromSurface(ImageId, Surface);
        SDL_DestroySurface(Surface);
    };
    StoreColor(1, 255, 0, 0);
    StoreColor(2, 0, 255, 0);
    StoreColor(3, 0, 0, 255);
    StoreColor(4, 255, 255, 255);
    Kernel.Objects().TextureObject(1, 1);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto CaptureCenter = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::array<Uint8, 4> Pixel{};
        REQUIRE(
            SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };

    const auto Base = CaptureCenter();
    CHECK(Base[0] >= 240);
    CHECK(Base[1] <= 16);
    CHECK(Base[2] <= 16);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetLightMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    REQUIRE_FALSE(Object->Meshes.empty());
    CHECK(Object->Meshes.front().TextureStageBlendModes.at(1) == 4);
    CHECK(Object->Meshes.front().TextureStageCoordinateModes.at(1) == 3);
    const auto LightMapped = CaptureCenter();
    CHECK(LightMapped[0] <= 16);
    CHECK(LightMapped[1] <= 16);
    CHECK(LightMapped[2] <= 16);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetDetailMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    CHECK(Object->Meshes.front().TextureStageBlendModes.at(1) == 7);
    const auto DetailMapped = CaptureCenter();
    CHECK(DetailMapped[0] >= 240);
    CHECK(DetailMapped[1] >= 240);
    CHECK(DetailMapped[2] <= 16);

    Kernel.Objects().TextureObject(1, 4);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 1));
    {
        std::string LightError;
        REQUIRE(Kernel.Lights().HideLight(0, &LightError));
    }
    Kernel.Lights().SetAmbientLight(100);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetSphereMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}));
    CHECK(Object->Meshes.front().TextureStageBlendModes.at(1) == 4);
    CHECK(Object->Meshes.front().TextureStageCoordinateModes.at(1) == 1);
    const auto SphereMapped = CaptureCenter();
    CHECK(SphereMapped[0] <= 16);
    CHECK(SphereMapped[1] <= 16);
    CHECK(SphereMapped[2] >= 240);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetLightMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(1)}));
    CHECK(Object->Meshes.front().TextureStageBlendModes.at(1) == 27);

    Context.Profile = dbc::language::LanguageProfile::Dbc;
    REQUIRE(Command(dbc::language::SymbolId::Command_SetNormalizationOff));
    CHECK_FALSE(Kernel.Renderer().NormalizeNormals());
    REQUIRE(Command(dbc::language::SymbolId::Command_SetNormalizationOn));
    CHECK(Kernel.Renderer().NormalizeNormals());
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(Command(dbc::language::SymbolId::Command_SetNormalizationOff));
    CHECK_FALSE(Kernel.Renderer().NormalizeNormals());
    REQUIRE(Command(dbc::language::SymbolId::Command_SetNormalizationOn));
    CHECK(Kernel.Renderer().NormalizeNormals());

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetLightMappingOn,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(99)}, Error));
    CHECK(Error.find("requires existing image 99") != std::string::npos);
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetLightMappingOn,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(2)},
                              Error));
    CHECK(Error.find("reverse stage mode must be zero or one") != std::string::npos);
}

TEST_CASE("DBPro blend operations and cube faces reach the portable stock shader")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });

    const auto StoreColor = [&](int ImageId, Uint8 Red, Uint8 Green, Uint8 Blue)
    {
        SDL_Surface* Surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(Surface != nullptr);
        REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, Red, Green, Blue, 255)));
        Kernel.ImageStoreRef().StoreImageFromSurface(ImageId, Surface);
        SDL_DestroySurface(Surface);
    };
    StoreColor(1, 255, 255, 0);
    StoreColor(2, 0, 255, 0);
    StoreColor(3, 0, 0, 255);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto CaptureCenter = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::array<Uint8, 4> Pixel{};
        REQUIRE(
            SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };

    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 4.0f, 4.0f));
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().TextureObject(1, 1);
    const auto Base = CaptureCenter();
    CHECK(Base[0] >= 240);
    CHECK(Base[1] >= 240);
    CHECK(Base[2] <= 16);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetBlendMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10)}));
    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    REQUIRE_FALSE(Object->Meshes.empty());
    CHECK(Object->Meshes.front().TextureStageBlendModes.at(1) == 10);
    CHECK(Object->Meshes.front().TextureStageCoordinateModes.at(1) == 3);
    const auto Subtracted = CaptureCenter();
    CHECK(Subtracted[0] <= 16);
    CHECK(Subtracted[1] <= 16);
    CHECK(Subtracted[2] <= 16);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetBlendMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(3),
                     BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(2)}));
    CHECK(Object->Meshes.front().TextureStageBlendModes.at(7) == 2);
    CHECK(Object->Meshes.front().TextureStageCoordinateModes.at(7) == 3);
    const auto StageSeven = CaptureCenter();
    CHECK(StageSeven[0] <= 16);
    CHECK(StageSeven[1] <= 16);
    CHECK(StageSeven[2] >= 240);

    Kernel.Objects().DeleteObject(1);
    REQUIRE(Kernel.Objects().MakeObjectPlain(2, 4.0f, 4.0f));
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(2, 0));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCubeMappingOn,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(3),
                     BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(3),
                     BuiltinValue::FromNumber(3)}));
    Object = Kernel.Objects().GetObject(2);
    REQUIRE(Object != nullptr);
    CHECK(Object->CubeGpuTexture != nullptr);
    CHECK(Object->Meshes.front().TextureStageCubeModes.at(1));
    const auto CubeMapped = CaptureCenter();
    CHECK(CubeMapped[0] <= 16);
    CHECK(CubeMapped[1] <= 16);
    CHECK(CubeMapped[2] >= 240);

    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetBlendMappingOn,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(3),
                         BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(4)}));
    CHECK(Error.find("stage must be between zero and seven") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetBlendMappingOn,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3),
                         BuiltinValue::FromNumber(9), BuiltinValue::FromNumber(4)}));
    CHECK(Error.find("texture coordinate mode is invalid") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetBlendMappingOn,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(27)}));
    CHECK(Error.find("blend mode must be between one and twenty-six") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetCubeMappingOn,
                        {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(3),
                         BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(3),
                         BuiltinValue::FromNumber(3)}));
    CHECK(Error.find("requires existing image 99") != std::string::npos);
    CHECK(Object->CubeGpuTexture != nullptr);
}

TEST_CASE("DBPro bump cartoon and rainbow effects reach the portable stock shader")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });

    const auto StoreColor = [&](int ImageId, Uint8 Red, Uint8 Green, Uint8 Blue)
    {
        SDL_Surface* Surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(Surface != nullptr);
        REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, Red, Green, Blue, 255)));
        Kernel.ImageStoreRef().StoreImageFromSurface(ImageId, Surface);
        SDL_DestroySurface(Surface);
    };
    StoreColor(1, 255, 255, 255);
    StoreColor(2, 128, 128, 255);
    StoreColor(3, 255, 128, 128);
    StoreColor(4, 255, 0, 0);
    StoreColor(5, 255, 255, 255);
    StoreColor(6, 0, 0, 255);

    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 4.0f, 4.0f));
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    Kernel.Objects().TextureObject(1, 1);
    Kernel.Lights().SetAmbientLight(0);
    {
        std::string LightError;
        REQUIRE(Kernel.Lights().SetDirectionalLight(0, 0.0f, 0.0f, 1.0f, &LightError));
    }

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto CaptureCenter = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::array<Uint8, 4> Pixel{};
        REQUIRE(
            SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_SetBumpMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    REQUIRE_FALSE(Object->Meshes.empty());
    CHECK(Object->Meshes.front().BuiltInEffect == 1);
    CHECK(Object->Meshes.front().TextureStageImageIds.at(1) == 2);
    const auto FlatNormal = CaptureCenter();
    CHECK(FlatNormal[0] >= 240);
    CHECK(FlatNormal[1] >= 240);
    CHECK(FlatNormal[2] >= 240);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetBumpMappingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}));
    const auto TangentNormal = CaptureCenter();
    CHECK(TangentNormal[0] <= 32);
    CHECK(TangentNormal[1] <= 32);
    CHECK(TangentNormal[2] <= 32);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetCartoonShadingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(5)}));
    CHECK(Object->Meshes.front().BuiltInEffect == 2);
    CHECK(Object->Meshes.front().TextureStageImageIds.at(0) == 4);
    CHECK(Object->Meshes.front().TextureStageImageIds.at(1) == 5);
    const auto Cartoon = CaptureCenter();
    CHECK(Cartoon[0] >= 240);
    CHECK(Cartoon[1] <= 16);
    CHECK(Cartoon[2] <= 16);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetRainbowShadingOn,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(6)}));
    CHECK(Object->Meshes.front().BuiltInEffect == 3);
    CHECK(Object->Meshes.front().TextureStageImageIds.at(0) == 6);
    CHECK(Object->Meshes.front().TextureStageImageIds.at(1) == 6);
    const auto Rainbow = CaptureCenter();
    CHECK(Rainbow[0] >= 176);
    CHECK(Rainbow[0] <= 196);
    CHECK(Rainbow[1] >= 176);
    CHECK(Rainbow[1] <= 196);
    CHECK(Rainbow[2] >= 240);

    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetBumpMappingOn,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(99)}));
    CHECK(Error.find("requires existing image 99") != std::string::npos);
    CHECK(Object->Meshes.front().BuiltInEffect == 3);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetCartoonShadingOn,
                        {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(99)}));
    CHECK(Error.find("requires existing edge image 99") != std::string::npos);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetRainbowShadingOn,
                        {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(6)}));
    CHECK(Error.find("requires existing object 99") != std::string::npos);
}

TEST_CASE("DBPro object and limb normal commands rewrite rendered geometry")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Camera().SetAutoCamOff();
    Kernel.Objects().MakeObjectCube(1, 1.0f);
    Kernel.Objects().PositionObject(1, -0.5f, -0.5f, 5.0f);

    dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    Object->Cull = false;
    REQUIRE_FALSE(Object->Meshes.empty());
    dbc::Mesh& Mesh = Object->Meshes.front();
    Mesh.EditGeometry().Vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                    {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    Mesh.EditGeometry().Indices = {0, 1, 2, 3, 4, 5};
    Mesh.EditGeometry().Uvs.assign(Mesh.Geometry().Vertices.size(), SDL_FPoint{});
    const std::vector<dbc::Vec3> Faceted = {
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
    };
    Mesh.EditGeometry().Normals = Faceted;

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto ObjectCommand = [&](dbc::language::SymbolId Id, double Percentage)
    {
        REQUIRE(InvokeCommand(Context, Id, {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(Percentage)}, Error));
    };
    const auto LimbCommand = [&](dbc::language::SymbolId Id, double Percentage)
    {
        REQUIRE(InvokeCommand(
            Context, Id,
            {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(Percentage)}, Error));
    };

    ObjectCommand(dbc::language::SymbolId::Command_SetObjectSmoothing, 50.0);
    CHECK(Mesh.Geometry().Normals[0].X == doctest::Approx(0.0f));
    CHECK(Mesh.Geometry().Normals[0].Z == doctest::Approx(1.0f));
    ObjectCommand(dbc::language::SymbolId::Command_SetObjectSmoothing, 100.0);
    CHECK(Mesh.Geometry().Normals[0].X == doctest::Approx(0.5f));
    CHECK(Mesh.Geometry().Normals[0].Z == doctest::Approx(0.5f));
    CHECK(Mesh.Geometry().Normals[3].X == doctest::Approx(0.5f));
    CHECK(Mesh.Geometry().Normals[3].Z == doctest::Approx(0.5f));

    Mesh.EditGeometry().Normals.assign(Mesh.Geometry().Vertices.size(), dbc::Vec3{});
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectNormals, {BuiltinValue::FromNumber(1)},
                          Error));
    CHECK(Mesh.Geometry().Normals[0].X == doctest::Approx(0.0f));
    CHECK(Mesh.Geometry().Normals[0].Z == doctest::Approx(1.0f));
    CHECK(Mesh.Geometry().Normals[3].X == doctest::Approx(1.0f));
    CHECK(Mesh.Geometry().Normals[3].Z == doctest::Approx(0.0f));

    Mesh.EditGeometry().Normals = Faceted;
    LimbCommand(dbc::language::SymbolId::Command_SetLimbSmoothing, 200.0);
    CHECK(Mesh.Geometry().Normals[0].X == doctest::Approx(0.5f));
    CHECK(Mesh.Geometry().Normals[0].Z == doctest::Approx(0.5f));
    Mesh.EditGeometry().Normals.assign(Mesh.Geometry().Vertices.size(), dbc::Vec3{});
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetLimbNormals,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}, Error));
    CHECK(Mesh.Geometry().Normals[0].Z == doctest::Approx(1.0f));
    CHECK(Mesh.Geometry().Normals[3].X == doctest::Approx(1.0f));

    REQUIRE(Kernel.Core().EnsureInitialized());
    // The CPU path shows the normals each triangle takes.
    Kernel.Renderer().SetGpuMeshesEnabled(false);
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    REQUIRE(Kernel.Renderer().CachedWorldTris().size() >= 2);
    CHECK(Kernel.Renderer().CachedWorldTris()[0].Normal[0][2] == doctest::Approx(1.0f));
    CHECK(Kernel.Renderer().CachedWorldTris()[1].Normal[0][0] == doctest::Approx(1.0f));

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetLimbNormals,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(9)}, Error));
    CHECK(Error.find("SET LIMB NORMALS requires existing limb 9 on object 1") != std::string::npos);
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectNormals,
                              {BuiltinValue::FromNumber(99)}, Error));
    CHECK(Error.find("SET OBJECT NORMALS requires existing object 99") != std::string::npos);
}

TEST_CASE("Object animation overloads use catalogue-validated DBPro forms")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    Kernel.Objects().MakeObjectCube(1, 1.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PlayObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}, Error));
    const auto* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    CHECK(Object->Playing);
    CHECK_FALSE(Object->Looping);
    CHECK(Object->PlayStartFrame == 3);
    CHECK(Object->PlayEndFrame == 3);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoopObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4)}, Error));
    CHECK(Object->Playing);
    CHECK(Object->Looping);
    CHECK(Object->PlayStartFrame == 4);
    CHECK(Object->PlayEndFrame == 4);
}

TEST_CASE("Portable object state animation and limbs preserve hierarchy and overload semantics")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(7, 1));
    Kernel.Objects().MakeObject(2, 7, 0);
    Kernel.Objects().MakeObjectCube(3, 1.0f);
    Kernel.Objects().MakeObjectCube(4, 1.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_AddLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(7)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_AddLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(7)},
                          Error));
    CHECK(Kernel.Objects().LimbExist(2, 2) == 1);
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_AddLimb,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(7)},
                              Error));
    CHECK(Error.find("next consecutive limb 3") != std::string::npos);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LinkLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)},
                          Error));
    CHECK(Kernel.Objects().CheckLimbLink(2, 2) == 1);
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_LinkLimb,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)},
                              Error));
    CHECK(Error.find("hierarchy cycle") != std::string::npos);

    REQUIRE(Kernel.Objects().OffsetLimb(2, 1, 3.0f, 0.0f, 0.0f));
    REQUIRE(Kernel.Objects().RotateLimb(2, 1, 0.0f, 0.0f, 90.0f));
    REQUIRE(Kernel.Objects().OffsetLimb(2, 2, 4.0f, 0.0f, 0.0f));
    const dbc::Vec3 LinkedOrigin = Kernel.Renderer().WorldLimbOrigin(*Kernel.Objects().GetObject(2), 2);
    CHECK(LinkedOrigin.X == doctest::Approx(3.0f).epsilon(0.001));
    CHECK(LinkedOrigin.Y == doctest::Approx(4.0f).epsilon(0.001));
    CHECK(LinkedOrigin.Z == doctest::Approx(0.0f).epsilon(0.001));
    REQUIRE(Kernel.Objects().OffsetLimb(2, 1, 0.0f, 0.0f, 0.0f));
    REQUIRE(Kernel.Objects().RotateLimb(2, 1, 0.0f, 0.0f, 0.0f));
    REQUIRE(Kernel.Objects().OffsetLimb(2, 2, 0.0f, 0.0f, 0.0f));

    Kernel.ImageStoreRef().CreateBitmap(4, 2, 2);
    Kernel.ImageStoreRef().GetImage(9, 0, 0, 2, 2);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_TextureLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                           BuiltinValue::FromNumber(9)},
                          Error));
    const dbc::Object3D* Target = Kernel.Objects().GetObject(2);
    REQUIRE(Target != nullptr);
    REQUIRE(Target->Meshes.size() == 3);
    CHECK(Target->Meshes[1].TextureStageImageIds.at(2) == 9);
    BuiltinValue StageTextureName;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_LimbTextureNameDbPro,
                           {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)},
                           StageTextureName, Error));
    CHECK(StageTextureName.AsString().empty());

    const float RadiusBefore = Target->BoundingRadius;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_OffsetLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10),
                           BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RotateLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10),
                           BuiltinValue::FromNumber(20), BuiltinValue::FromNumber(30), BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(200),
                           BuiltinValue::FromNumber(100), BuiltinValue::FromNumber(50), BuiltinValue::FromNumber(1)},
                          Error));
    CHECK(Target->BoundingRadius > RadiusBefore);
    CHECK(Kernel.Objects().LimbOffsetX(2, 1) == doctest::Approx(10.0f));
    CHECK(Kernel.Objects().LimbAngleY(2, 1) == doctest::Approx(20.0f));
    CHECK(Kernel.Objects().LimbScaleX(2, 1) == doctest::Approx(200.0f));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_HideLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Kernel.Objects().LimbVisible(2, 1) == 0);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ShowLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Kernel.Objects().LimbVisible(2, 1) == 1);

    Kernel.Objects().RotateObject(3, 10.0f, 20.0f, 30.0f);
    REQUIRE(Kernel.Objects().FixObjectPivot(3));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectToObjectOrientation,
                          {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1)},
                          Error));
    const dbc::Object3D* Oriented = Kernel.Objects().GetObject(4);
    REQUIRE(Oriented != nullptr);
    CHECK(Oriented->ApplyPivot);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectSpeed,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(125)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectInterpolation,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(75)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PlayObject, {BuiltinValue::FromNumber(3)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_StopObject, {BuiltinValue::FromNumber(3)}, Error));
    CHECK(Kernel.Objects().ObjectSpeed(3) == doctest::Approx(125.0f));
    CHECK(Kernel.Objects().ObjectInterpolation(3) == 75);
    CHECK(Kernel.Objects().ObjectPlaying(3) == 0);
    CHECK(Kernel.Objects().ObjectLooping(3) == 0);

    Kernel.Objects().PositionObject(2, 10.0f, 0.0f, 0.0f);
    Kernel.Objects().PositionObject(3, 5.0f, 0.0f, 0.0f);
    REQUIRE(Kernel.Objects().OffsetLimb(2, 1, 2.0f, 0.0f, 0.0f));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_GlueObjectToLimb,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1),
                           BuiltinValue::FromNumber(2)},
                          Error));
    CHECK(Kernel.Renderer().WorldObjectOrigin(*Kernel.Objects().GetObject(3)).X == doctest::Approx(12.0f));
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_UnglueObject, {BuiltinValue::FromNumber(3)}, Error));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RemoveLimb,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(2)}, Error));
    CHECK(Kernel.Objects().LimbExist(2, 2) == 0);
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_RemoveLimb,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0)}, Error));
    CHECK(Error.find("cannot remove limb zero") != std::string::npos);
}

TEST_CASE("Portable limb checklist colour and texture transforms preserve stage semantics")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(7, 1));
    Kernel.Objects().MakeObject(2, 7, 0);
    REQUIRE(Kernel.Objects().AddLimb(2, 1, 7));
    REQUIRE(Kernel.Objects().AddLimb(2, 2, 7));
    REQUIRE(Kernel.Objects().LinkLimb(2, 1, 2));

    dbc::Object3D* Target = Kernel.Objects().GetObject(2);
    REQUIRE(Target != nullptr);
    REQUIRE(Target->Meshes.size() == 3);
    REQUIRE_FALSE(Target->Meshes[1].Geometry().Uvs.empty());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    std::string Error;
    for (const auto Profile : {dbc::language::LanguageProfile::Dbc, dbc::language::LanguageProfile::DbPro})
    {
        Context.Profile = Profile;
        REQUIRE(InvokeCommand(
            Context, dbc::language::SymbolId::Command_ColorLimb,
            {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0x123456)}, Error));
        CHECK(Target->Meshes[1].LimbTint.r == doctest::Approx(0x12 / 255.0f));
        CHECK(Target->Meshes[1].LimbTint.g == doctest::Approx(0x34 / 255.0f));
        CHECK(Target->Meshes[1].LimbTint.b == doctest::Approx(0x56 / 255.0f));

        Target->Meshes[1].EditGeometry().Uvs.front() = SDL_FPoint{1.0f, 2.0f};
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollLimbTexture,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0.25),
                               BuiltinValue::FromNumber(0.5)},
                              Error));
        CHECK(Target->Meshes[1].Geometry().Uvs.front().x == doctest::Approx(1.25f));
        CHECK(Target->Meshes[1].Geometry().Uvs.front().y == doctest::Approx(2.5f));

        Target->Meshes[1].EditGeometry().Uvs.front() = SDL_FPoint{1.0f, 2.0f};
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleLimbTexture,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2.0),
                               BuiltinValue::FromNumber(3.0)},
                              Error));
        CHECK(Target->Meshes[1].Geometry().Uvs.front().x == doctest::Approx(2.0f));
        CHECK(Target->Meshes[1].Geometry().Uvs.front().y == doctest::Approx(6.0f));

        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForObjectLimbs,
                              {BuiltinValue::FromNumber(2)}, Error));
        REQUIRE(Context.Checklist.size() == 3);
        CHECK(Context.Checklist[0].Name.empty());
        CHECK(Context.Checklist[0].A == 0);
        CHECK(Context.Checklist[0].B == -1);
        CHECK(Context.Checklist[0].C == 1);
        CHECK(Context.Checklist[0].D == -1);
        CHECK(Context.Checklist[1].A == 1);
        CHECK(Context.Checklist[1].B == -1);
        CHECK(Context.Checklist[1].C == -1);
        CHECK(Context.Checklist[1].D == 2);
        CHECK(Context.Checklist[2].A == 2);
        CHECK(Context.Checklist[2].B == 1);
        CHECK(Context.Checklist[2].C == -1);
        CHECK(Context.Checklist[2].D == -1);
    }

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Target->Meshes[1].EditGeometry().UvStages[2] = Target->Meshes[1].Geometry().Uvs;
    Target->Meshes[1].EditGeometry().UvStages[2].front() = SDL_FPoint{1.0f, 2.0f};
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollLimbTexture,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                           BuiltinValue::FromNumber(0.5), BuiltinValue::FromNumber(0.25)},
                          Error));
    CHECK(Target->Meshes[1].EditGeometry().UvStages[2].front().x == doctest::Approx(1.5f));
    CHECK(Target->Meshes[1].EditGeometry().UvStages[2].front().y == doctest::Approx(2.25f));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleLimbTexture,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                           BuiltinValue::FromNumber(2.0), BuiltinValue::FromNumber(4.0)},
                          Error));
    CHECK(Target->Meshes[1].EditGeometry().UvStages[2].front().x == doctest::Approx(3.0f));
    CHECK(Target->Meshes[1].EditGeometry().UvStages[2].front().y == doctest::Approx(9.0f));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollLimbTexture,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3),
                           BuiltinValue::FromNumber(1.0), BuiltinValue::FromNumber(1.0)},
                          Error));
    CHECK_FALSE(Target->Meshes[1].Geometry().UvStages.contains(3));
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleLimbTexture,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-1),
                               BuiltinValue::FromNumber(1.0), BuiltinValue::FromNumber(1.0)},
                              Error));
    CHECK(Error.find("stage must be non-negative") != std::string::npos);
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorLimb,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(9), BuiltinValue::FromNumber(0)},
                              Error));
    CHECK(Error.find("requires existing limb 9") != std::string::npos);
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForObjectLimbs,
                              {BuiltinValue::FromNumber(99)}, Error));
    CHECK(Error.find("requires existing object 99") != std::string::npos);

    BuiltinValue TimerValue;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_Timer, {}, TimerValue, Error));
    CHECK_FALSE(TimerValue.IsString);
    CHECK(TimerValue.Number >= 0.0);
}

TEST_CASE("Classic memblock reads return neutral values outside the allocation while DBPro stays checked")
{
    std::string MemblockError;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-memblock-reads", 8, 8));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Memblocks().MakeMemblock(1, 4, &MemblockError));
    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 0, 0x01020304, &MemblockError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockByte(Output, 1, 4, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockByte(Output, 1, -1, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockWord(Output, 1, 3, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 1, &Error); }) == 0);
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Memblocks().ReadMemblockFloat(Output, 1, 1, &Error); }) ==
          doctest::Approx(0.0f));

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    EngineBuiltinHost Host(Context);
    const std::vector<dbc::vm::Value> Arguments{dbc::vm::Value::Integer(1), dbc::vm::Value::Integer(4)};
    const dbc::vm::BuiltinResult Classic = Host.Invoke(
        {1, dbc::language::SymbolId::Function_MemblockByte, Arguments, {dbc::language::TypeKind::Integer, {}}, false});
    REQUIRE(Classic.State == dbc::vm::BuiltinState::Completed);
    REQUIRE(Classic.ReturnValue);
    CHECK(Classic.ReturnValue->AsInteger() == 0);

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    const dbc::vm::BuiltinResult Professional = Host.Invoke(
        {2, dbc::language::SymbolId::Function_MemblockByte, Arguments, {dbc::language::TypeKind::Integer, {}}, false});
    CHECK(Professional.State == dbc::vm::BuiltinState::Failed);
    CHECK(Professional.Error == "Memblock access is outside the allocated range");
}

TEST_CASE("Classic mesh memblocks use the documented 32-byte indexed layout without widening DBPro")
{
    std::string MemblockError;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-mesh-memblock", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(10, 1));
    const std::vector<dbc::Mesh>* SourceMeshes = Kernel.Objects().GetMeshes(10);
    REQUIRE(SourceMeshes != nullptr);
    REQUIRE_FALSE(SourceMeshes->empty());
    const dbc::Mesh& Source = SourceMeshes->front();
    REQUIRE_FALSE(Source.Geometry().Indices.empty());

    REQUIRE(Kernel.Memblocks().MakeMemblockFromMesh(1, 10, &MemblockError));
    const int VertexCount = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 0, &Error); });
    const int VertexOffset = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 4, &Error); });
    const int NormalCount = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 8, &Error); });
    const int NormalOffset = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 12, &Error); });
    const int FaceCount = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 16, &Error); });
    const int FaceOffset = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 20, &Error); });
    const int FaceDataSize = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 24, &Error); });
    const int TextureOffset = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 28, &Error); });
    CHECK(VertexCount == static_cast<int>(Source.Geometry().Indices.size()));
    CHECK(VertexOffset == 32);
    CHECK(NormalCount == VertexCount);
    CHECK(NormalOffset == VertexOffset + VertexCount * 12);
    CHECK(FaceCount == VertexCount / 3);
    CHECK(FaceOffset == NormalOffset + NormalCount * 12);
    CHECK(FaceDataSize == FaceCount * 14);
    CHECK(TextureOffset == FaceOffset + FaceDataSize);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().MemblockSize(Output, 1, &Error); }) ==
          TextureOffset + VertexCount * 8);
    CHECK(std::isfinite(
        ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                { return Kernel.Memblocks().ReadMemblockFloat(Output, 1, VertexOffset, &Error); })));
    CHECK(std::isfinite(
        ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                { return Kernel.Memblocks().ReadMemblockFloat(Output, 1, NormalOffset, &Error); })));
    CHECK(std::isfinite(
        ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                { return Kernel.Memblocks().ReadMemblockFloat(Output, 1, TextureOffset, &Error); })));

    const float MutatedX =
        ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                { return Kernel.Memblocks().ReadMemblockFloat(Output, 1, VertexOffset, &Error); }) +
        3.0f;
    REQUIRE(Kernel.Memblocks().WriteMemblockFloat(1, VertexOffset, MutatedX, &MemblockError));
    CHECK(Kernel.Memblocks().MakeMeshFromMemblock(11, 1, &MemblockError));
    const std::vector<dbc::Mesh>* DecodedMeshes = Kernel.Objects().GetMeshes(11);
    REQUIRE(DecodedMeshes != nullptr);
    REQUIRE_FALSE(DecodedMeshes->empty());
    REQUIRE_FALSE(DecodedMeshes->front().Geometry().Vertices.empty());
    CHECK(DecodedMeshes->front().Geometry().Vertices.front().X == doctest::Approx(MutatedX));
    CHECK(DecodedMeshes->front().Geometry().Indices.size() == static_cast<std::size_t>(VertexCount));
    CHECK(Kernel.Memblocks().ChangeMeshFromMemblock(11, 1, &MemblockError));

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(Kernel.Memblocks().MakeMemblockFromMesh(2, 10, &MemblockError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 0, &Error); }) == 0x152);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 4, &Error); }) == 36);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().MemblockSize(Output, 2, &Error); }) ==
          12 + static_cast<int>(Source.Geometry().Indices.size()) * 36);
}

TEST_CASE("DBPro vertex diffuse takes DWORD colours and keeps opaque objects opaque")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("vertex-diffuse", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(Kernel.Core().EnsureInitialized());
    std::string Error;
    REQUIRE_MESSAGE(Kernel.Core().SetWindowSize(32, 32, &Error), Error);
    Kernel.Camera().SetAutoCamOff();
    REQUIRE_MESSAGE(Kernel.Camera().SetBackdropColor(0, &Error), Error);
    Kernel.Objects().MakeObjectCube(1, 4.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    EngineBuiltinHost Host(Context);
    REQUIRE(Kernel.Objects().LockVertexdataForLimb(1, 0));
    const int Vertices = ReadEngineNumber<int>([&](std::string& ReadError, int& Output)
                                               { return Kernel.Objects().VertexdataVertexCount(Output, &ReadError); });
    REQUIRE(Vertices > 0);
    const auto SetDiffuse = [&](int Vertex, dbc::vm::Value Colour)
    {
        const std::vector<dbc::vm::Value> Arguments{dbc::vm::Value::Integer(Vertex), Colour};
        return Host.Invoke({1,
                            dbc::language::SymbolId::Command_SetVertexdataDiffuse,
                            Arguments,
                            {dbc::language::TypeKind::Void, {}},
                            false});
    };
    REQUIRE(SetDiffuse(0, dbc::vm::Value::Real(4278255360.0)).State == dbc::vm::BuiltinState::Completed);
    CHECK(static_cast<uint32_t>(ReadEngineNumber<int>(
              [&](std::string& ReadError, int& Output)
              { return Kernel.Objects().VertexdataDiffuse(Output, 0, &ReadError); })) == 0xFF00FF00u);
    CHECK(SetDiffuse(0, dbc::vm::Value::Real(4294967296.0)).State == dbc::vm::BuiltinState::Failed);
    // RGB() leaves the alpha byte at zero.
    for (int Vertex = 0; Vertex < Vertices; ++Vertex)
        REQUIRE(SetDiffuse(Vertex, dbc::vm::Value::Integer(0x00FF00)).State == dbc::vm::BuiltinState::Completed);
    Kernel.Objects().UnlockVertexdata();

    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> Frame(Kernel.Renderer().CaptureScreen(),
                                                                            SDL_DestroySurface);
    REQUIRE(Frame != nullptr);
    std::array<Uint8, 4> Pixel{};
    REQUIRE(SDL_ReadSurfacePixel(Frame.get(), Frame->w / 2, Frame->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
    CHECK(Pixel[0] < 20);
    CHECK(Pixel[1] > 200);
    CHECK(Pixel[2] < 20);
}

TEST_CASE("Portable mesh vertex data memblocks and DBO preserve mutable geometry")
{
    std::string MemblockError;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(10, 1));
    REQUIRE(Kernel.Objects().MeshExist(10) == 1);

    REQUIRE(Kernel.Objects().LockVertexdataForMesh(10));
    const int OriginalVertices = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Objects().VertexdataVertexCount(Output, &Error); });
    const int OriginalIndices = ReadEngineNumber<int>(
        [&](std::string& Error, int& Output) { return Kernel.Objects().VertexdataIndexCount(Output, &Error); });
    REQUIRE(OriginalVertices > 0);
    REQUIRE(OriginalIndices > 0);
    REQUIRE(Kernel.Objects().SetVertexdataPosition(0, 4.0f, 5.0f, 6.0f));
    REQUIRE(Kernel.Objects().SetVertexdataNormals(0, 0.0f, 1.0f, 0.0f));
    REQUIRE(Kernel.Objects().SetVertexdataDiffuse(0, 0x80402010u));
    REQUIRE(Kernel.Objects().SetVertexdataUv(0, 3, 0.25f, 0.75f));
    REQUIRE(Kernel.Objects().SetIndexdata(0, 1));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Objects().VertexdataPositionX(Output, 0, &Error); }) ==
          doctest::Approx(4.0f));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Objects().VertexdataNormalsY(Output, 0, &Error); }) ==
          doctest::Approx(1.0f));
    CHECK(static_cast<uint32_t>(
              ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                    { return Kernel.Objects().VertexdataDiffuse(Output, 0, &Error); })) == 0x80402010u);
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Objects().VertexdataU(Output, 0, 3, &Error); }) ==
          doctest::Approx(0.25f));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().Indexdata(Output, 0, &Error); }) == 1);
    Kernel.Objects().UnlockVertexdata();
    int VertexCount = 99;
    std::string VertexError;
    CHECK_FALSE(Kernel.Objects().VertexdataVertexCount(VertexCount, &VertexError));
    CHECK(VertexCount == 99);
    CHECK(VertexError.find("requires locked vertex data") != std::string::npos);

    REQUIRE(Kernel.Memblocks().MakeMemblockFromMesh(1, 10, &MemblockError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 0, &Error); }) == 0x152);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 1, 4, &Error); }) == 36);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().MemblockSize(Output, 1, &Error); }) ==
          12 + OriginalIndices * 36);
    REQUIRE(Kernel.Memblocks().MakeMeshFromMemblock(11, 1, &MemblockError));
    REQUIRE(Kernel.Objects().LockVertexdataForMesh(11));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataVertexCount(Output, &Error); }) == OriginalIndices);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataIndexCount(Output, &Error); }) == OriginalIndices);
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Objects().VertexdataPositionX(Output, 0, &Error); }) ==
          doctest::Approx(Kernel.Objects().GetMeshes(10)->front().Geometry().Vertices[1].X));
    Kernel.Objects().UnlockVertexdata();

    REQUIRE(Kernel.Memblocks().ChangeMeshFromMemblock(10, 1, &MemblockError));
    REQUIRE(Kernel.Objects().ConvertMeshToVertexdata(10));
    REQUIRE(Kernel.Objects().LockVertexdataForMesh(10));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataVertexCount(Output, &Error); }) == OriginalIndices);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataIndexCount(Output, &Error); }) == OriginalIndices);
    REQUIRE(Kernel.Objects().AddMeshToVertexdata(11));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataVertexCount(Output, &Error); }) ==
          OriginalIndices * 2);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataIndexCount(Output, &Error); }) ==
          OriginalIndices * 2);
    REQUIRE(Kernel.Objects().DeleteMeshFromVertexdata(OriginalIndices, OriginalIndices * 2 - 1, OriginalIndices,
                                                      OriginalIndices * 2 - 1));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataVertexCount(Output, &Error); }) == OriginalIndices);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().VertexdataIndexCount(Output, &Error); }) == OriginalIndices);
    Kernel.Objects().UnlockVertexdata();

    Kernel.Objects().MakeObject(2, 10, 0);
    REQUIRE(Kernel.Objects().MakeObjectFromLimb(3, 2, 0));
    Kernel.Objects().InstanceObject(4, 3);
    REQUIRE(Kernel.Objects().GetObject(4) != nullptr);

    const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path MeshPath =
        std::filesystem::temp_directory_path() / ("opendbc-lot22-" + std::to_string(Stamp) + ".x");
    const std::filesystem::path ObjectPath =
        std::filesystem::temp_directory_path() / ("opendbc-lot22-" + std::to_string(Stamp) + ".dbo");
    REQUIRE(Kernel.Objects().SaveMesh(MeshPath.string(), 10));
    REQUIRE(Kernel.Objects().LoadMesh(MeshPath.string(), 12));
    CHECK(Kernel.Objects().MeshExist(12) == 1);
    REQUIRE(Kernel.Objects().SaveObject(ObjectPath.string(), 2));
    {
        std::ifstream Input(ObjectPath, std::ios::binary);
        REQUIRE(Input.good());
        uint32_t MagicSize = 0;
        Input.read(reinterpret_cast<char*>(&MagicSize), sizeof(MagicSize));
        CHECK(MagicSize == 8);
        std::array<char, 8> Magic{};
        Input.read(Magic.data(), static_cast<std::streamsize>(Magic.size()));
        CHECK(std::string(Magic.data(), Magic.size()) == "MAGICDBO");
    }
    REQUIRE(Kernel.Objects().LoadObject(ObjectPath.string(), 5));
    const dbc::Object3D* RoundTrip = Kernel.Objects().GetObject(5);
    REQUIRE(RoundTrip != nullptr);
    REQUIRE(RoundTrip->Meshes.size() == Kernel.Objects().GetObject(2)->Meshes.size());
    CHECK(RoundTrip->Meshes.front().Geometry().Vertices.size() ==
          Kernel.Objects().GetObject(2)->Meshes.front().Geometry().Vertices.size());
    CHECK(RoundTrip->Meshes.front().Geometry().DiffuseColors.front() == 0xFFFFFFFFu);
    {
        std::string Error;
        CHECK_FALSE(Kernel.Objects().SaveObject((ObjectPath.string() + ".x"), 2, &Error));
        CHECK(Error.find("requires a .dbo filename") != std::string::npos);
    };

    REQUIRE(Kernel.Objects().DeleteMesh(12));
    CHECK(Kernel.Objects().MeshExist(12) == 0);
    std::filesystem::remove(MeshPath);
    std::filesystem::remove(ObjectPath);
}

TEST_CASE("Object texture scale and SET OBJECT defaults match BASIC semantics")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    Kernel.Objects().MakeObjectCube(1, 1.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleObjectTexture,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1.0), BuiltinValue::FromNumber(1.0)},
                          Error));
    const auto* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    CHECK(Object->TexScaleU == doctest::Approx(1.0f));
    CHECK(Object->TexScaleV == doctest::Approx(1.0f));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(1)},
                          Error));
    CHECK(Object->TextureFilter);
    CHECK(Object->TransparentBlack);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(125), BuiltinValue::FromNumber(75),
                           BuiltinValue::FromNumber(50)},
                          Error));
    CHECK(Object->ScaleX == doctest::Approx(1.25f));
    CHECK(Object->ScaleY == doctest::Approx(0.75f));
    CHECK(Object->ScaleZ == doctest::Approx(0.5f));

    Kernel.ImageStoreRef().CreateBitmap(4, 2, 2);
    Kernel.ImageStoreRef().GetImage(9, 0, 0, 2, 2);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_TextureObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(9)}, Error));
    CHECK(Object->TextureImageId == 9);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTexture,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)},
                          Error));
    for (const dbc::Mesh& MeshValue : Object->Meshes)
    {
        CHECK(MeshValue.TextureStageAddressModes.at(0) == 2);
        CHECK(MeshValue.TextureStageMipModes.at(0) == 1);
    }

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(1)},
                          Error));
    CHECK_FALSE(Object->TransparentBlack);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTexture,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(5),
                           BuiltinValue::FromNumber(7)},
                          Error));
    for (const dbc::Mesh& MeshValue : Object->Meshes)
    {
        CHECK(MeshValue.TextureStageAddressModes.at(3) == 5);
        CHECK(MeshValue.TextureStageMipModes.at(3) == 7);
    }

    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, -3.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PointCamera(0.0f, 0.0f, 0.0f, &Error); });
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    SDL_DestroySurface(Capture);
    REQUIRE_FALSE(Kernel.Renderer().CachedWorldTris().empty());
    CHECK(Kernel.Renderer().CachedWorldTris().front().Style.TextureAddressModes[0] == 1);
    CHECK(Kernel.Renderer().CachedWorldTris().front().Style.TextureMipModes[0] == 1);
    CHECK(Kernel.Gpu().SurfaceSampler(1, true, 1) != nullptr);

    REQUIRE_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTexture,
                                {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(1),
                                 BuiltinValue::FromNumber(1)},
                                Error));
    CHECK(Error.find("stage must not be negative") != std::string::npos);
    REQUIRE_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTexture,
                                {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(1)},
                                Error));
    CHECK(Error.find("address mode must be between 0 and 5") != std::string::npos);
    REQUIRE_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTexture,
                                {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(5)},
                                Error));
    CHECK(Error.find("mip mode must be 0, 1, 2, 3, 6 or 7") != std::string::npos);
    REQUIRE_FALSE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectTexture,
                      {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Error.find("requires existing object 99") != std::string::npos);
}

TEST_CASE("Object material ghost and rotation commands update render state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    Kernel.Objects().MakeObjectCube(1, 1.0f);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0x204080)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FadeObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(50)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_GhostObjectOn,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetObjectRotationZyx, {BuiltinValue::FromNumber(1)},
                          Error));

    const dbc::Object3D* Object = Kernel.Objects().GetObject(1);
    REQUIRE(Object != nullptr);
    CHECK(Object->TintR == doctest::Approx(32.0f / 255.0f));
    CHECK(Object->TintG == doctest::Approx(64.0f / 255.0f));
    CHECK(Object->TintB == doctest::Approx(128.0f / 255.0f));
    CHECK(Object->FadeNormalScale == doctest::Approx(0.5f));
    CHECK(Object->Alpha == doctest::Approx(0.5f));
    CHECK(Object->Ghosted);
    CHECK(Object->GhostMode == 1);
    CHECK(Object->UseZyxRotation);

    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_GhostObjectOff, {BuiltinValue::FromNumber(1)}, Error));
    CHECK_FALSE(Object->Ghosted);
}

TEST_CASE("DBPro object loading primitives transforms and size queries preserve historical behavior")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 32, 32));

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.AssetRoot = TEST_SOURCE_DIR;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectPlane,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(20), BuiltinValue::FromNumber(10),
                           BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectPlain,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(6)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectSphere,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(8),
                           BuiltinValue::FromNumber(12)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectTriangle,
                          {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(3),
                           BuiltinValue::FromNumber(0)},
                          Error));

    const dbc::Object3D* Plane = Kernel.Objects().GetObject(1);
    const dbc::Object3D* Plain = Kernel.Objects().GetObject(2);
    REQUIRE(Plane != nullptr);
    REQUIRE(Plain != nullptr);
    CHECK(Plane->RotY == doctest::Approx(0.0f));
    CHECK(Plain->RotY == doctest::Approx(180.0f));
    CHECK(Kernel.Objects().ObjectSize(3) == doctest::Approx(5.0f));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_XrotateObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(25)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ZrotateObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(35)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScaleObject,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(200), BuiltinValue::FromNumber(50),
                           BuiltinValue::FromNumber(300)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MoveObjectLeft,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MoveObjectRight,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Plane->RotX == doctest::Approx(25.0f));
    CHECK(Plane->RotZ == doctest::Approx(35.0f));
    CHECK(Plane->PosX == doctest::Approx(-3.0f));
    CHECK(Kernel.Objects().ObjectSizeX(1) == doctest::Approx(20.0f));
    CHECK(Kernel.Objects().ObjectSizeX(1, 1) == doctest::Approx(40.0f));
    CHECK(Kernel.Objects().ObjectSizeY(1, 1) == doctest::Approx(5.0f));
    CHECK(Kernel.Objects().ObjectSize(1, 1) == doctest::Approx(13.75f));

    BuiltinValue SizeResult;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ObjectSize,
                           {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, SizeResult, Error));
    CHECK(SizeResult.AsNumber() == doctest::Approx(13.75));
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ObjectSizeZ,
                           {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, SizeResult, Error));
    CHECK(SizeResult.AsNumber() == doctest::Approx(0.0));

    const std::string Model = "examples/dbpro/tutorials/sound/models/notes/notes.x";
    const int DividedTextureId = Kernel.ImageStoreRef().LoadImageInternalFromPath(
        (std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/sound/models/notes/notes_0.png").string(),
        2);
    REQUIRE(DividedTextureId < 0);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString(Model), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(4),
                           BuiltinValue::FromNumber(2)},
                          Error));
    const dbc::Object3D* Loaded = Kernel.Objects().GetObject(10);
    REQUIRE(Loaded != nullptr);
    REQUIRE_FALSE(Loaded->Meshes.empty());
    const auto TexturedMesh = std::find_if(Loaded->Meshes.begin(), Loaded->Meshes.end(),
                                           [](const dbc::Mesh& Mesh) { return Mesh.TextureImageId < 0; });
    REQUIRE(TexturedMesh != Loaded->Meshes.end());
    const int TextureId = TexturedMesh->TextureImageId;
    REQUIRE(TextureId < 0);
    const dbc::Image* Texture = Kernel.ImageStoreRef().GetImage(TextureId);
    REQUIRE(Texture != nullptr);
    CHECK(Texture->Width == 8);
    CHECK(Texture->Height == 8);
    CHECK(TexturedMesh->TextureUsesMaterialColor);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString(Model), BuiltinValue::FromNumber(11)}, Error));
    const dbc::Object3D* DefaultLoaded = Kernel.Objects().GetObject(11);
    REQUIRE(DefaultLoaded != nullptr);
    REQUIRE_FALSE(DefaultLoaded->Meshes.empty());
    const auto DefaultTexturedMesh = std::find_if(DefaultLoaded->Meshes.begin(), DefaultLoaded->Meshes.end(),
                                                  [](const dbc::Mesh& Mesh) { return Mesh.TextureImageId < 0; });
    REQUIRE(DefaultTexturedMesh != DefaultLoaded->Meshes.end());
    CHECK_FALSE(DefaultTexturedMesh->TextureUsesMaterialColor);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                              {BuiltinValue::FromString("missing-model.x"), BuiltinValue::FromNumber(12)}, Error));
    CHECK(Error.find("LOAD OBJECT failed") != std::string::npos);
    CHECK(Kernel.Objects().ObjectExist(12) == 0);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_DeleteObjects,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4)}, Error));
    CHECK(Kernel.Objects().ObjectExist(1) == 1);
    CHECK(Kernel.Objects().ObjectExist(2) == 0);
    CHECK(Kernel.Objects().ObjectExist(3) == 0);
    CHECK(Kernel.Objects().ObjectExist(4) == 0);
}

TEST_CASE("Classic backdrop and fog commands update draw state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorBackdrop, {BuiltinValue::FromNumber(0x102030)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOff, {}, Error));
    CHECK(Kernel.Draw().BackdropColor() == 0x102030);
    CHECK_FALSE(Kernel.Draw().BackdropOn());
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOn, {}, Error));
    CHECK(Kernel.Draw().BackdropOn());

    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Backdrop = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Backdrop != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Backdrop, nullptr, SDL_MapSurfaceRGBA(Backdrop, 255, 255, 255, 255)));
    Kernel.ImageStoreRef().StoreImageFromSurface(17, Backdrop);
    SDL_DestroySurface(Backdrop);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_TextureBackdrop, {BuiltinValue::FromNumber(17)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollBackdrop,
                          {BuiltinValue::FromNumber(25.0), BuiltinValue::FromNumber(-50.0)}, Error));
    CHECK(Kernel.Draw().BackdropImageId() == 17);
    CHECK(Kernel.Draw().BackdropScrollX() == doctest::Approx(25.0f));
    CHECK(Kernel.Draw().BackdropScrollY() == doctest::Approx(-50.0f));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollBackdrop,
                          {BuiltinValue::FromNumber(-10.0), BuiltinValue::FromNumber(75.0)}, Error));
    CHECK(Kernel.Draw().BackdropScrollX() == doctest::Approx(-10.0f));
    CHECK(Kernel.Draw().BackdropScrollY() == doctest::Approx(75.0f));
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_TextureBackdrop, {BuiltinValue::FromNumber(99)},
                              Error));
    CHECK(Error.find("requires existing image 99") != std::string::npos);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Ink,
                          {BuiltinValue::FromNumber(0xFF0000), BuiltinValue::FromNumber(0)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Dot,
                          {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0)}, Error));
    SDL_Surface* BackdropCapture = Kernel.Renderer().CaptureScreen();
    REQUIRE(BackdropCapture != nullptr);
    Uint8 BackdropRed = 0;
    Uint8 BackdropGreen = 0;
    Uint8 BackdropBlue = 0;
    Uint8 BackdropAlpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(BackdropCapture, 0, 0, &BackdropRed, &BackdropGreen, &BackdropBlue, &BackdropAlpha));
    SDL_DestroySurface(BackdropCapture);
    CHECK(BackdropRed >= 240);
    CHECK(BackdropGreen <= 16);
    CHECK(BackdropBlue <= 16);

    BuiltinValue AlphaAvailable;
    REQUIRE(
        InvokeFunction(Context, dbc::language::SymbolId::Function_AlphablendingAvailable, {}, AlphaAvailable, Error));
    CHECK(ReadBuiltinInteger(AlphaAvailable) == (Kernel.Gpu().GpuPipelineTransparent() != nullptr ? 1 : 0));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogOn, {}, Error));
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_FogColor, {BuiltinValue::FromNumber(0x405060)}, Error));
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_FogDistance, {BuiltinValue::FromNumber(750)}, Error));
    CHECK(Kernel.Draw().FogOn());
    CHECK(Kernel.Draw().FogColor() == 0x405060);
    CHECK(Kernel.Draw().FogStartDistance() == doctest::Approx(0.0f));
    CHECK(Kernel.Draw().FogDistance() == doctest::Approx(750.0f));
    CHECK(Kernel.Renderer().FogFactor(375.0f) == doctest::Approx(0.5f));
    BuiltinValue FogAvailable;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_FogAvailable, {}, FogAvailable, Error));
    CHECK(ReadBuiltinInteger(FogAvailable) == 1);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogOff, {}, Error));
    CHECK_FALSE(Kernel.Draw().FogOn());
}

TEST_CASE("DBPro backdrop and ink overloads preserve their profile-specific call shape")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorBackdrop,
                          {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0x203040)}, Error));
    CHECK(Kernel.Draw().BackdropColor() == 0x203040);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Ink,
                          {BuiltinValue::FromNumber(0x506070), BuiltinValue::FromNumber(0)}, Error));
    CHECK(Kernel.Text().InkForeColor() == 0x506070);
}

TEST_CASE("Portable backdrops and linear fog preserve profile semantics")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().MakeCamera(1, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Camera().BackdropColor(Output, 1, &Error); }) == 0x008000);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCurrentCamera(1, &Error); });
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorBackdrop, {BuiltinValue::FromNumber(0x102030)},
                          Error));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Camera().BackdropColor(Output, 0, &Error); }) == 0x102030);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Camera().BackdropColor(Output, 1, &Error); }) == 0x008000);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorBackdrop,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0x203040)}, Error));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Camera().BackdropColor(Output, 1, &Error); }) == 0x203040);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCurrentCamera(0, &Error); });
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOff, {BuiltinValue::FromNumber(1)}, Error));
    CHECK(Kernel.Camera().BackdropEnabled());
    CHECK_FALSE(ReadEngineNumber<bool>([&](std::string& Error, bool& Output)
                                       { return Kernel.Camera().BackdropEnabled(Output, 1, &Error); }));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOff, {}, Error));
    CHECK_FALSE(Kernel.Camera().BackdropEnabled());
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOn, {}, Error));
    CHECK(Kernel.Camera().BackdropEnabled());
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOn, {BuiltinValue::FromNumber(1)}, Error));
    CHECK(ReadEngineNumber<bool>([&](std::string& Error, bool& Output)
                                 { return Kernel.Camera().BackdropEnabled(Output, 1, &Error); }));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorBackdrop, {BuiltinValue::FromNumber(0x203040)},
                          Error));
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_FogColor, {BuiltinValue::FromNumber(0x405060)}, Error));
    CHECK(Kernel.Draw().FogColor() == 0x405060);
    CHECK(Kernel.Draw().BackdropColor() == 0x203040);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Camera().BackdropColor(Output, 0, &Error); }) == 0x203040);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogColor,
                          {BuiltinValue::FromNumber(112), BuiltinValue::FromNumber(128), BuiltinValue::FromNumber(144)},
                          Error));
    CHECK(Kernel.Draw().FogColor() == 0x708090);
    CHECK(Kernel.Draw().BackdropColor() == 0x203040);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Camera().BackdropColor(Output, 0, &Error); }) == 0x203040);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogDistance,
                          {BuiltinValue::FromNumber(100), BuiltinValue::FromNumber(750)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogOn, {}, Error));
    CHECK(Kernel.Draw().FogStartDistance() == doctest::Approx(100.0f));
    CHECK(Kernel.Draw().FogDistance() == doctest::Approx(750.0f));
    CHECK(Kernel.Renderer().FogFactor(100.0f) == doctest::Approx(0.0f));
    CHECK(Kernel.Renderer().FogFactor(425.0f) == doctest::Approx(0.5f));
    CHECK(Kernel.Renderer().FogFactor(750.0f) == doctest::Approx(1.0f));
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_FogDistance, {BuiltinValue::FromNumber(900)}, Error));
    CHECK(Kernel.Draw().FogStartDistance() == doctest::Approx(100.0f));
    CHECK(Kernel.Draw().FogDistance() == doctest::Approx(900.0f));

    BuiltinValue FogAvailable;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_FogAvailable, {}, FogAvailable, Error));
    CHECK(ReadBuiltinInteger(FogAvailable) == 1);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogOff, {}, Error));
    CHECK_FALSE(Kernel.Draw().FogOn());

    Error.clear();
    CHECK_FALSE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_BackdropOn, {BuiltinValue::FromNumber(99)}, Error));
    CHECK(Error.find("requires existing camera 99") != std::string::npos);

    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObject(1, 0, 0, 1, 1, 0, 1, 0));
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_FogColor, {BuiltinValue::FromNumber(0xFF0000)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogDistance,
                          {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogOn, {}, Error));
    REQUIRE(Kernel.Core().EnsureInitialized());

    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    Uint8 Red = 0;
    Uint8 Green = 0;
    Uint8 Blue = 0;
    Uint8 Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red >= 240);
    CHECK(Green <= 16);
    CHECK(Blue <= 16);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogColor,
                          {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(255), BuiltinValue::FromNumber(0)},
                          Error));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red <= 16);
    CHECK(Green >= 240);
    CHECK(Blue <= 16);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogDistance,
                          {BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20)}, Error));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red >= 240);
    CHECK(Green >= 240);
    CHECK(Blue >= 240);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogDistance,
                          {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_FogOff, {}, Error));
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red >= 240);
    CHECK(Green >= 240);
    CHECK(Blue >= 240);
}

TEST_CASE("Classic primitive and text commands queue observable frame state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Ink,
                          {BuiltinValue::FromNumber(0x112233), BuiltinValue::FromNumber(0)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Dot,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Line,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(5),
                           BuiltinValue::FromNumber(6)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Circle,
                          {BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(9)},
                          Error));

    std::vector<dbc::Primitive2D> Primitives;
    Kernel.Draw().ForEachPrimitive2D([&](const dbc::Primitive2D& Value) { Primitives.push_back(Value); });
    REQUIRE(Primitives.size() == 3);
    CHECK(Primitives[0].Type == dbc::Primitive2DType::Dot);
    CHECK(Primitives[1].Type == dbc::Primitive2DType::Line);
    CHECK(Primitives[2].Type == dbc::Primitive2DType::Circle);
    for (const dbc::Primitive2D& Primitive : Primitives)
        CHECK(Primitive.Color == 0x112233);

    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_Text,
        {BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(11), BuiltinValue::FromString("left")}, Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_CenterText,
        {BuiltinValue::FromNumber(20), BuiltinValue::FromNumber(21), BuiltinValue::FromString("center")}, Error));
    std::vector<dbc::DebugTextLine> Lines;
    Kernel.Text().ForEachDebugTextLine([&](const dbc::DebugTextLine& Value) { Lines.push_back(Value); });
    REQUIRE(Lines.size() == 2);
    CHECK(Lines[0].X == doctest::Approx(10.0f));
    CHECK(Lines[0].Y == doctest::Approx(11.0f));
    CHECK(Lines[0].Value == "left");
    CHECK(Lines[1].Value == "center");
}

TEST_CASE("Classic screen text expires per frame while bitmap and Professional text persist")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("screen-text-lifetime", 192, 80));
    REQUIRE(Kernel.Core().EnsureInitialized());
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    CHECK(Kernel.RuntimeCompatibility().ScreenText() == dbc::ScreenTextLifetime::CurrentFrame);
    Kernel.Text().State().Size = 24;

    Kernel.Text().DrawText(4, 4, "MMMMMMMM");
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);
    SDL_Surface* ClassicLong = Kernel.Renderer().CaptureScreen();
    REQUIRE(ClassicLong != nullptr);
    Kernel.Renderer().RenderScene();
    std::vector<dbc::DebugTextLine> Lines;
    Kernel.Text().ForEachDebugTextLine([&](const dbc::DebugTextLine& Line) { Lines.push_back(Line); });
    CHECK(Lines.empty());

    const int ShortWidth = Kernel.Text().TextWidthPx("I");
    Kernel.Text().DrawText(4, 4, "I");
    SDL_Surface* ClassicShort = Kernel.Renderer().CaptureScreen();
    REQUIRE(ClassicShort != nullptr);
    Kernel.Renderer().RenderScene();
    int ExpiredPixels = 0;
    for (int Y = 4; Y < 40; ++Y)
    {
        for (int X = 8 + ShortWidth; X < 150; ++X)
        {
            Uint8 LongRed = 0;
            Uint8 LongGreen = 0;
            Uint8 LongBlue = 0;
            Uint8 LongAlpha = 0;
            Uint8 ShortRed = 0;
            Uint8 ShortGreen = 0;
            Uint8 ShortBlue = 0;
            Uint8 ShortAlpha = 0;
            REQUIRE(SDL_ReadSurfacePixel(ClassicLong, X, Y, &LongRed, &LongGreen, &LongBlue, &LongAlpha));
            REQUIRE(SDL_ReadSurfacePixel(ClassicShort, X, Y, &ShortRed, &ShortGreen, &ShortBlue, &ShortAlpha));
            const int LongLuminance = static_cast<int>(LongRed) + LongGreen + LongBlue;
            const int ShortLuminance = static_cast<int>(ShortRed) + ShortGreen + ShortBlue;
            ExpiredPixels += LongLuminance > 48 && ShortLuminance <= 24 ? 1 : 0;
        }
    }
    CHECK(ExpiredPixels > 20);
    SDL_DestroySurface(ClassicShort);
    SDL_DestroySurface(ClassicLong);

    Kernel.ImageStoreRef().CreateBitmap(1, 96, 40);
    Kernel.Text().DrawText(4, 4, "BITMAP");
    SDL_Surface* Offscreen = Kernel.ImageStoreRef().GetBitmapSurface(1);
    REQUIRE(Offscreen != nullptr);
    int OffscreenTextPixels = 0;
    for (int Y = 0; Y < Offscreen->h; ++Y)
    {
        for (int X = 0; X < Offscreen->w; ++X)
        {
            Uint8 Red = 0;
            Uint8 Green = 0;
            Uint8 Blue = 0;
            Uint8 Alpha = 0;
            REQUIRE(SDL_ReadSurfacePixel(Offscreen, X, Y, &Red, &Green, &Blue, &Alpha));
            OffscreenTextPixels += static_cast<int>(Red) + Green + Blue > 48 ? 1 : 0;
        }
    }
    CHECK(OffscreenTextPixels > 20);
    Kernel.ImageStoreRef().SetCurrentBitmap(0);
    Kernel.ImageStoreRef().DeleteBitmap(1);

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    CHECK(Kernel.RuntimeCompatibility().ScreenText() == dbc::ScreenTextLifetime::PersistentBitmap);
    Kernel.Draw().ClearScreen(0);
    Kernel.Text().DrawText(4, 4, "MMMMMMMM");
    REQUIRE(Kernel.ImageStoreRef().GetBitmapSurface(0) != nullptr);
    SDL_Surface* ProfessionalLong = Kernel.Renderer().CaptureScreen();
    REQUIRE(ProfessionalLong != nullptr);
    Kernel.Text().DrawText(4, 4, "I");
    SDL_Surface* ProfessionalShort = Kernel.Renderer().CaptureScreen();
    REQUIRE(ProfessionalShort != nullptr);
    int PersistentPixels = 0;
    for (int Y = 4; Y < 40; ++Y)
    {
        for (int X = 8 + ShortWidth; X < 150; ++X)
        {
            Uint8 LongRed = 0;
            Uint8 LongGreen = 0;
            Uint8 LongBlue = 0;
            Uint8 LongAlpha = 0;
            Uint8 ShortRed = 0;
            Uint8 ShortGreen = 0;
            Uint8 ShortBlue = 0;
            Uint8 ShortAlpha = 0;
            REQUIRE(SDL_ReadSurfacePixel(ProfessionalLong, X, Y, &LongRed, &LongGreen, &LongBlue, &LongAlpha));
            REQUIRE(SDL_ReadSurfacePixel(ProfessionalShort, X, Y, &ShortRed, &ShortGreen, &ShortBlue, &ShortAlpha));
            const int LongLuminance = static_cast<int>(LongRed) + LongGreen + LongBlue;
            const int ShortLuminance = static_cast<int>(ShortRed) + ShortGreen + ShortBlue;
            PersistentPixels += LongLuminance > 48 && ShortLuminance > 48 ? 1 : 0;
        }
    }
    CHECK(PersistentPixels > 20);
    SDL_DestroySurface(ProfessionalShort);
    SDL_DestroySurface(ProfessionalLong);
}

TEST_CASE("SYNC RATE paces whole frames and a SYNC ON wait redraws nothing")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 320, 240));
    CHECK(Kernel.Core().RefreshesWhileWaiting());
    Kernel.Core().DoSyncOn();
    CHECK_FALSE(Kernel.Core().RefreshesWhileWaiting());

    // A dense sphere makes the render take long enough to tell where the period starts.
    Kernel.Objects().MakeObjectSphere(1, 10.0f, 200, 200);
    REQUIRE(Kernel.Camera().PositionCamera(0.0f, 0.0f, -30.0f));
    Kernel.Core().DoSyncRate(1);
    Kernel.Core().Sync();
    const auto Before = std::chrono::steady_clock::now();
    Kernel.Core().Sync();
    const auto Rendered = std::chrono::steady_clock::now() - Before;
    const auto Deadline = Kernel.Core().NextSyncDeadline();
    REQUIRE(Deadline);
    // The one-second period starts when SYNC begins, so the render time is part of it.
    CHECK(*Deadline - (Before + std::chrono::seconds(1)) < Rendered / 2);
}

TEST_CASE("Text compatibility commands and functions expose observable state in both profiles")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 80, 60));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    std::string Error;

    for (const auto Profile : {dbc::language::LanguageProfile::Dbc, dbc::language::LanguageProfile::DbPro})
    {
        Context.Profile = Profile;
        Kernel.Text().State() = dbc::BasicTextState{};
        Kernel.Text().ClearDebugTextLines();
        Kernel.Core().DoSyncOn();
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SyncOff, {}, Error));
        CHECK_FALSE(Kernel.Core().SyncOn());
        Kernel.Core().Fps() = 61;
        BuiltinValue ScreenFps;
        REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ScreenFps, {}, ScreenFps, Error));
        CHECK(ReadBuiltinInteger(ScreenFps) == 61);
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetCursor,
                              {BuiltinValue::FromNumber(12), BuiltinValue::FromNumber(14)}, Error));
        CHECK(Kernel.Text().State().CursorX == 12);
        CHECK(Kernel.Text().State().CursorY == 14);
        Kernel.Text().State().Opaque = true;
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetTextTransparent, {}, Error));
        CHECK_FALSE(Kernel.Text().State().Opaque);
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_HideMouse, {}, Error));
        CHECK_FALSE(SDL_CursorVisible());
        CHECK_FALSE(SDL_GetWindowRelativeMouseMode(Kernel.Core().Window()));
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ShowMouse, {}, Error));
        CHECK(SDL_CursorVisible());
        CHECK_FALSE(SDL_GetWindowRelativeMouseMode(Kernel.Core().Window()));
        REQUIRE(InvokeCommand(
            Context, dbc::language::SymbolId::Command_Text,
            {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(5), BuiltinValue::FromString("profile")}, Error));
        REQUIRE(InvokeCommand(
            Context, dbc::language::SymbolId::Command_CenterText,
            {BuiltinValue::FromNumber(40), BuiltinValue::FromNumber(7), BuiltinValue::FromString("center")}, Error));

        std::vector<dbc::DebugTextLine> Lines;
        Kernel.Text().ForEachDebugTextLine([&](const dbc::DebugTextLine& Line) { Lines.push_back(Line); });
        REQUIRE(Lines.size() == 2);
        CHECK(Lines[0].Value == "profile");
        CHECK(Lines[1].Value == "center");
    }

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    REQUIRE(
        InvokeCommand(Context, dbc::language::SymbolId::Command_SetTextSize, {BuiltinValue::FromNumber(22)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetTextFont, {BuiltinValue::FromString("Arial")},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetTextToBold, {}, Error));
    CHECK(Kernel.Text().State().Size == 22);
    CHECK(Kernel.Text().State().Font == "Arial");
    CHECK(Kernel.Text().State().Style == 1);

    const auto StringFunction = [&](dbc::language::SymbolId Symbol, std::vector<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, Arguments, Result, Error));
        return Result.AsString();
    };
    const auto NumberFunction = [&](dbc::language::SymbolId Symbol, std::vector<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, Arguments, Result, Error));
        return Result.AsNumber();
    };
    CHECK(StringFunction(dbc::language::SymbolId::Function_StrString,
                         {BuiltinValue::FromNumber(3.5), BuiltinValue::FromNumber(2)}) == "3.50");
    CHECK(StringFunction(dbc::language::SymbolId::Function_StrString, {BuiltinValue::FromNumber(3.5)}) == "3.5");
    CHECK(StringFunction(dbc::language::SymbolId::Function_MidString,
                         {BuiltinValue::FromString("abc"), BuiltinValue::FromNumber(2)}) == "b");
    CHECK(StringFunction(dbc::language::SymbolId::Function_UpperString, {BuiltinValue::FromString("Abc")}) == "ABC");
    CHECK(StringFunction(dbc::language::SymbolId::Function_SpaceString, {BuiltinValue::FromNumber(3)}) == "   ");
    CHECK(NumberFunction(dbc::language::SymbolId::Function_Asc, {BuiltinValue::FromString("A")}) == 65.0);
    CHECK(NumberFunction(dbc::language::SymbolId::Function_Len, {BuiltinValue::FromString("abc")}) == 3.0);
    CHECK(NumberFunction(dbc::language::SymbolId::Function_TextWidth, {BuiltinValue::FromString("abc")}) > 0.0);
}

TEST_CASE("DBPro math and screen compatibility functions retain deterministic values")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 96, 72));
    std::string ModeError;
    REQUIRE(Kernel.Core().SetDisplayMode(96, 72, 32, 1, 0, 0, &ModeError));
    SDL_Event Resize{};
    Resize.type = SDL_EVENT_WINDOW_RESIZED;
    Resize.window.data1 = 111;
    Resize.window.data2 = 83;
    Kernel.Input().HandleEvent(Resize);
    Kernel.Core().Fps() = 73;
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    const auto Number = [&](dbc::language::SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, Arguments, Result, Error));
        return Result.AsNumber();
    };
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Randomize, {BuiltinValue::FromNumber(123)}, Error));
    const double FirstRandom = Number(dbc::language::SymbolId::Function_Rnd, {BuiltinValue::FromNumber(100)});
    const double SecondRandom = Number(dbc::language::SymbolId::Function_Rnd, {BuiltinValue::FromNumber(100)});
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Randomize, {BuiltinValue::FromNumber(123)}, Error));
    CHECK(Number(dbc::language::SymbolId::Function_Rnd, {BuiltinValue::FromNumber(100)}) == FirstRandom);
    CHECK(Number(dbc::language::SymbolId::Function_Rnd, {BuiltinValue::FromNumber(100)}) == SecondRandom);
    CHECK(Number(dbc::language::SymbolId::Function_Wrapvalue, {BuiltinValue::FromNumber(370)}) ==
          doctest::Approx(10.0));
    CHECK(Number(dbc::language::SymbolId::Function_Cos, {BuiltinValue::FromNumber(60)}) == doctest::Approx(0.5));
    CHECK(Number(dbc::language::SymbolId::Function_Sin, {BuiltinValue::FromNumber(30)}) == doctest::Approx(0.5));
    CHECK(Number(dbc::language::SymbolId::Function_ScreenWidth) == 111.0);
    CHECK(Number(dbc::language::SymbolId::Function_ScreenHeight) == 83.0);
    CHECK(Number(dbc::language::SymbolId::Function_ScreenFps) == 73.0);
}

TEST_CASE("Classic host core clipboard and measurable system queries expose observable state")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-classic-host-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directories(Root / "working"));
    {
        std::ofstream Document(Root / "document.txt");
        Document << "launch target";
    }

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-host-core", 96, 72));
    REQUIRE(Kernel.Core().EnsureInitialized());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = Root;
    Context.CommandLine = "--classic-oracle value";
    dbc::interpreter::HostFileLaunchRequest CapturedLaunch;
    int LaunchCount = 0;
    Context.LaunchFile = [&](const dbc::interpreter::HostFileLaunchRequest& Request, std::string&)
    {
        CapturedLaunch = Request;
        ++LaunchCount;
        return true;
    };
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Number = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };
    const auto String = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsString();
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_ExecuteFile,
                    {BuiltinValue::FromString("document.txt"), BuiltinValue::FromString("--page 2"),
                     BuiltinValue::FromString("working"), BuiltinValue::FromNumber(1)}));
    CHECK(LaunchCount == 1);
    CHECK(CapturedLaunch.FileName == Root / "document.txt");
    CHECK(CapturedLaunch.CommandLine == "--page 2");
    CHECK(std::filesystem::equivalent(CapturedLaunch.WorkingDirectory, Root / "working"));
    CHECK(CapturedLaunch.WaitForTermination);

    Context.LaunchFile = [](const dbc::interpreter::HostFileLaunchRequest&, std::string& LaunchError)
    {
        LaunchError = "host target is unavailable";
        return false;
    };
    CHECK(Command(dbc::language::SymbolId::Command_ExecuteFile,
                  {BuiltinValue::FromString("C:/Windows/NOTEPAD.EXE"), BuiltinValue::FromString("document.txt"),
                   BuiltinValue::FromString("")}));
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    CHECK_FALSE(Command(
        dbc::language::SymbolId::Command_ExecuteFile,
        {BuiltinValue::FromString("missing-tool"), BuiltinValue::FromString(""), BuiltinValue::FromString("")}));
    CHECK(Error == "Unsupported command: EXECUTE FILE");
    Context.Profile = dbc::language::LanguageProfile::Dbc;

    REQUIRE(Command(dbc::language::SymbolId::Command_ExitPrompt,
                    {BuiltinValue::FromString("Complete"), BuiltinValue::FromString("Finished safely")}));
    REQUIRE(Context.PendingExitPrompt.has_value());
    CHECK(Context.PendingExitPrompt->first == "Complete");
    CHECK(Context.PendingExitPrompt->second == "Finished safely");
    CHECK_FALSE(Context.ExitPromptPresented);

    const std::string ClipboardText = "OpenDBC Classic clipboard " + std::to_string(Suffix);
    REQUIRE(Command(dbc::language::SymbolId::Command_WriteToClipboard, {BuiltinValue::FromString(ClipboardText)}));
    CHECK(String(dbc::language::SymbolId::Function_GetClipboardString) == ClipboardText);
    CHECK(String(dbc::language::SymbolId::Function_ClString) == Context.CommandLine);
    CHECK_FALSE(String(dbc::language::SymbolId::Function_AppnameString).empty());
    CHECK_FALSE(String(dbc::language::SymbolId::Function_GetDateString).empty());
    CHECK_FALSE(String(dbc::language::SymbolId::Function_GetTimeString).empty());

    CHECK(Number(dbc::language::SymbolId::Function_Hcos, {BuiltinValue::FromNumber(0)}) == doctest::Approx(1.0));
    CHECK(Number(dbc::language::SymbolId::Function_Hsin, {BuiltinValue::FromNumber(0)}) == doctest::Approx(0.0));
    CHECK(Number(dbc::language::SymbolId::Function_Htan, {BuiltinValue::FromNumber(45)}) ==
          doctest::Approx(std::tanh(45.0 * 3.14159265358979323846 / 180.0)));
    CHECK(Number(dbc::language::SymbolId::Function_GetBackbufferWidth) == Kernel.Core().WindowWidth());
    CHECK(Number(dbc::language::SymbolId::Function_GetBackbufferHeight) == Kernel.Core().WindowHeight());
    CHECK(Number(dbc::language::SymbolId::Function_GetBackbufferDepth) == 32.0);
    CHECK(Number(dbc::language::SymbolId::Function_GetBackbufferPitch) == Kernel.Core().WindowWidth() * 4.0);
    CHECK(Number(dbc::language::SymbolId::Function_FilteringAvailable) == 1.0);
    CHECK(Number(dbc::language::SymbolId::Function_Statistic, {BuiltinValue::FromNumber(1)}) >= 0.0);
    CHECK(Number(dbc::language::SymbolId::Function_SystemDmemAvailable) > 0.0);
    CHECK(Number(dbc::language::SymbolId::Function_SystemSmemAvailable) > 0.0);
    CHECK(Number(dbc::language::SymbolId::Function_SystemTmemAvailable) > 0.0);

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Classic registry commands persist values in the user's registry file")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-classic-registry-" + std::to_string(Suffix));
    BuiltinContext Context;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.RegistryPath = Root / "registry.json";
    std::string Error;
    using dbc::language::SymbolId;
    const auto Write = [&](std::string_view Folder, std::string_view Key, double Value)
    {
        return InvokeCommand(Context, SymbolId::Command_WriteToRegistry,
                             {BuiltinValue::FromString(std::string(Folder)), BuiltinValue::FromString(std::string(Key)),
                              BuiltinValue::FromNumber(Value)},
                             Error);
    };
    const auto Read = [&](std::string_view Folder, std::string_view Key)
    {
        BuiltinValue Result;
        REQUIRE_MESSAGE(
            InvokeFunction(Context, SymbolId::Function_GetRegistry,
                           {BuiltinValue::FromString(std::string(Folder)), BuiltinValue::FromString(std::string(Key))},
                           Result, Error),
            Error);
        return Result.AsNumber();
    };

    CHECK(Read("Software\\OpenDBC", "Score") == 0.0);
    CHECK_FALSE(std::filesystem::exists(Context.RegistryPath));
    REQUIRE_MESSAGE(Write("Software\\OpenDBC", "Score", 10), Error);
    REQUIRE_MESSAGE(Write("SOFTWARE\\opendbc", "score", 42), Error);
    REQUIRE_MESSAGE(Write("Software\\OpenDBC", "Lives", 4294967295.0), Error);
    CHECK(Read("software\\OPENDBC", "SCORE") == 42.0);
    CHECK(Read("Software\\OpenDBC", "Lives") == -1.0);
    CHECK(Read("Software\\OpenDBC", "Missing") == 0.0);
    CHECK(Read("Software\\Other", "Score") == 0.0);
    CHECK_FALSE(Write("Software\\OpenDBC", "Score", 4294967296.0));

    // The first spelling of a folder and a key is kept, as in the registry.
    {
        std::ifstream Input(Context.RegistryPath);
        const std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
        CHECK(Text.find("\"Software\\\\OpenDBC\"") != std::string::npos);
        CHECK(Text.find("\"Score\": 42") != std::string::npos);
        CHECK(Text.find("SOFTWARE") == std::string::npos);
    }

    // A damaged file is reported and never overwritten.
    {
        std::ofstream Output(Context.RegistryPath, std::ios::trunc);
        Output << "{\"Software\\\\OpenDBC\": {\"Score\": \"high\"}}";
    }
    BuiltinValue Unused;
    CHECK_FALSE(InvokeFunction(Context, SymbolId::Function_GetRegistry,
                               {BuiltinValue::FromString("Software\\OpenDBC"), BuiltinValue::FromString("Score")},
                               Unused, Error));
    CHECK(Error.find("must hold an object of folders") != std::string::npos);
    CHECK_FALSE(Write("Software\\OpenDBC", "Score", 1));
    {
        std::ifstream Input(Context.RegistryPath);
        const std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
        CHECK(Text.find("\"high\"") != std::string::npos);
    }

    BuiltinValue Blit;
    REQUIRE(InvokeFunction(Context, SymbolId::Function_N3dblitAvailable, {}, Blit, Error));
    CHECK(Blit.AsNumber() == 1.0);

    Context.Profile = dbc::language::LanguageProfile::DbPro;
    CHECK_FALSE(InvokeFunction(Context, SymbolId::Function_GetRegistry,
                               {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}, Unused, Error));
    std::filesystem::remove_all(Root);
}

TEST_CASE("DBPro text tokens and checklists are isolated from other executions")
{
    dbc::EngineKernel FirstKernel;
    dbc::EngineKernel SecondKernel;
    REQUIRE(FirstKernel.Initialize("first", 64, 48));
    REQUIRE(SecondKernel.Initialize("second", 64, 48));
    BuiltinContext First;
    First.Kernel = &FirstKernel;
    First.Profile = dbc::language::LanguageProfile::DbPro;
    BuiltinContext Second;
    Second.Kernel = &SecondKernel;
    Second.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_SetTextSize, {BuiltinValue::FromNumber(24)}, Error));
    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_SetTextOpaque, {}, Error));
    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_SetTextToItalic, {}, Error));
    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_SetTextToBolditalic, {}, Error));
    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_Printc, {BuiltinValue::FromString("queued")}, Error));
    CHECK(FirstKernel.Text().State().Size == 24);
    CHECK(FirstKernel.Text().State().Opaque);
    CHECK(FirstKernel.Text().State().Style == 3);
    CHECK(FirstKernel.Text().State().CursorX > 0);
    CHECK(SecondKernel.Text().State().Size == 16);
    CHECK_FALSE(SecondKernel.Text().State().Opaque);
    CHECK(SecondKernel.Text().State().Style == 0);

    const auto String =
        [&](BuiltinContext& Context, dbc::language::SymbolId Symbol, std::vector<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, Arguments, Result, Error));
        return Result.AsString();
    };
    const auto Number =
        [&](BuiltinContext& Context, dbc::language::SymbolId Symbol, std::vector<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, Arguments, Result, Error));
        return ReadBuiltinInteger(Result);
    };
    CHECK(String(First, dbc::language::SymbolId::Function_Append,
                 {BuiltinValue::FromString("Open"), BuiltinValue::FromString("DBC")}) == "OpenDBC");
    CHECK(String(First, dbc::language::SymbolId::Function_Reverse, {BuiltinValue::FromString("abc")}) == "cba");
    CHECK(Number(First, dbc::language::SymbolId::Function_CompareCase,
                 {BuiltinValue::FromString("Case"), BuiltinValue::FromString("Case")}) == 1);
    CHECK(Number(First, dbc::language::SymbolId::Function_FindFirstChar,
                 {BuiltinValue::FromString("banana"), BuiltinValue::FromString("a")}) == 2);
    CHECK(Number(First, dbc::language::SymbolId::Function_FindLastChar,
                 {BuiltinValue::FromString("banana"), BuiltinValue::FromString("a")}) == 6);
    CHECK(Number(First, dbc::language::SymbolId::Function_FindSubString,
                 {BuiltinValue::FromString("banana"), BuiltinValue::FromString("nan")}) == 3);
    CHECK(String(First, dbc::language::SymbolId::Function_FirstToken,
                 {BuiltinValue::FromString("one,two"), BuiltinValue::FromString(",")}) == "one");
    CHECK(String(Second, dbc::language::SymbolId::Function_NextToken, {BuiltinValue::FromString(",")}).empty());
    CHECK(String(First, dbc::language::SymbolId::Function_NextToken, {BuiltinValue::FromString(",")}) == "two");

    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_PerformChecklistForGraphicsCards, {}, Error));
    REQUIRE(First.Checklist.size() == 1);
    CHECK_FALSE(First.Checklist.front().Name.empty());
    CHECK(Second.Checklist.empty());
    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_PerformChecklistForDisplayModes, {}, Error));
    REQUIRE_FALSE(First.Checklist.empty());
    for (const auto& Item : First.Checklist)
    {
        CHECK(Item.A > 0);
        CHECK(Item.B > 0);
        CHECK(Item.C >= 16);
    }
    REQUIRE(InvokeCommand(First, dbc::language::SymbolId::Command_PerformChecklistForFonts, {}, Error));
    REQUIRE_FALSE(First.Checklist.empty());
    CHECK(std::is_sorted(First.Checklist.begin(), First.Checklist.end(),
                         [](const auto& Left, const auto& Right) { return Left.Name < Right.Name; }));
}

TEST_CASE("Portable display and window commands update SDL state and reject unavailable modes")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("window-test", 96, 72));
    REQUIRE(Kernel.Core().EnsureInitialized());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                          {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72)}, Error));
    CHECK(Kernel.Core().ScreenDepth() == 32);
    Context.Profile = dbc::language::LanguageProfile::DbPro;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                          {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72), BuiltinValue::FromNumber(16)},
                          Error));
    CHECK(Kernel.Core().ScreenDepth() == 16);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                          {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72), BuiltinValue::FromNumber(32)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                          {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72), BuiltinValue::FromNumber(32),
                           BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                          {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72), BuiltinValue::FromNumber(32),
                           BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0)},
                          Error));
    CHECK(Kernel.Core().ScreenDepth() == 32);
    CHECK(Kernel.Core().DisplayVSync() == 1);
    CHECK(Kernel.Core().DisplayMultisampling() == 0);
    CHECK(Kernel.Core().DisplayMultimonitor() == 0);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                              {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72), BuiltinValue::FromNumber(8)},
                              Error));
    CHECK(Error.find("16 or 32") != std::string::npos);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDisplayMode,
                              {BuiltinValue::FromNumber(96), BuiltinValue::FromNumber(72), BuiltinValue::FromNumber(32),
                               BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)},
                              Error));
    CHECK(Error.find("multisampling") != std::string::npos);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetWindowTitle,
                          {BuiltinValue::FromString("OpenDBC Window Test")}, Error));
    BuiltinValue Result;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_WindowExist,
                           {BuiltinValue::FromString("OpenDBC Window Test")}, Result, Error));
    CHECK(ReadBuiltinInteger(Result) == 1);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetWindowSize,
                          {BuiltinValue::FromNumber(80), BuiltinValue::FromNumber(60)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetWindowPosition,
                          {BuiltinValue::FromNumber(24), BuiltinValue::FromNumber(32)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetWindowLayout,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_HideWindow, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ShowWindow, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MaximizeWindow, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RestoreWindow, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MinimizeWindow, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RestoreWindow, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_WindowToFront, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_WindowToFront,
                          {BuiltinValue::FromString("OpenDBC Window Test")}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetWindowOff, {}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetWindowOn, {}, Error));

    Kernel.Core().SetSuspended(true);
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ScreenInvalid, {}, Result, Error));
    CHECK(ReadBuiltinInteger(Result) == 1);
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ScreenInvalid, {}, Result, Error));
    CHECK(ReadBuiltinInteger(Result) == 0);
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ScreenType, {}, Result, Error));
    CHECK(ReadBuiltinInteger(Result) == 1);
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_ScreenDepth, {}, Result, Error));
    CHECK(ReadBuiltinInteger(Result) == 32);

    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);
    CHECK_NOTHROW(Kernel.ImageStoreRef().BlurBitmap(0, 1));
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) != nullptr);
    CHECK_NOTHROW(Kernel.ImageStoreRef().DeleteBitmap(0));

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_WindowToFront,
                              {BuiltinValue::FromString("OpenDBC Missing Window")}, Error));
    CHECK(Error.find("cannot find") != std::string::npos);
}

TEST_CASE("Image memblock pixels are ARGB dwords and Classic ignores their alpha byte")
{
    std::string Error;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("argb-memblock", 32, 32));
    const auto Pixel = [&](int ImageId)
    {
        std::array<Uint8, 4> Value{};
        const dbc::Image* Img = Kernel.ImageStoreRef().GetImage(ImageId);
        REQUIRE(Img != nullptr);
        REQUIRE(Img->Surface != nullptr);
        REQUIRE(SDL_ReadSurfacePixel(Img->Surface, 0, 0, &Value[0], &Value[1], &Value[2], &Value[3]));
        return Value;
    };
    // One pixel written the DarkBASIC way: RGB(255, 0, 0) as a dword, with no alpha byte.
    const auto MakeRedImage = [&](int ImageId)
    {
        const int Block = 10 + ImageId;
        REQUIRE(Kernel.Memblocks().MakeMemblock(Block, 16, &Error));
        REQUIRE(Kernel.Memblocks().WriteMemblockDword(Block, 0, 1, &Error));
        REQUIRE(Kernel.Memblocks().WriteMemblockDword(Block, 4, 1, &Error));
        REQUIRE(Kernel.Memblocks().WriteMemblockDword(Block, 8, 32, &Error));
        REQUIRE(Kernel.Memblocks().WriteMemblockDword(Block, 12, 0x00FF0000, &Error));
        REQUIRE(Kernel.Memblocks().MakeImageFromMemblock(ImageId, Block, &Error));
    };

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    MakeRedImage(1);
    CHECK(Pixel(1) == std::array<Uint8, 4>{255, 0, 0, 255});
    // Back to a memblock: blue, green, red, alpha in memory.
    REQUIRE(Kernel.Memblocks().MakeMemblockFromImage(2, 1, &Error));
    const auto Byte = [&](int Offset)
    {
        return ReadEngineNumber<int>([&](std::string& ReadError, int& Output)
                                     { return Kernel.Memblocks().ReadMemblockByte(Output, 2, Offset, &ReadError); });
    };
    CHECK(Byte(12) == 0);
    CHECK(Byte(13) == 0);
    CHECK(Byte(14) == 255);
    CHECK(Byte(15) == 255);

    // DBPro images keep the alpha byte, so the same dword is a transparent red.
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    MakeRedImage(2);
    CHECK(Pixel(2) == std::array<Uint8, 4>{255, 0, 0, 0});
}

TEST_CASE("16-bit screen bitmap memblocks use RGB565 and can replace bitmap zero")
{
    std::string MemblockError;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("rgb565-test", 96, 72));
    std::string ModeError;
    REQUIRE(Kernel.Core().SetDisplayMode(96, 72, 16, 1, 0, 0, &ModeError));

    std::vector<uint8_t> Data(16u, 0u);
    const uint32_t Width = 2;
    const uint32_t Height = 1;
    const uint32_t Depth = 16;
    std::memcpy(Data.data() + 0, &Width, 4);
    std::memcpy(Data.data() + 4, &Height, 4);
    std::memcpy(Data.data() + 8, &Depth, 4);
    Data[12] = 0x00;
    Data[13] = 0xf8;
    Data[14] = 0xe0;
    Data[15] = 0x07;
    REQUIRE(Kernel.Memblocks().ReplaceMemblock(1, std::move(Data), &MemblockError));

    CHECK(Kernel.Memblocks().MakeBitmapFromMemblock(0, 1, &MemblockError));
    CHECK(Kernel.ImageStoreRef().BitmapDepth(0) == 16);
    SDL_Surface* Bitmap = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(Bitmap != nullptr);
    SDL_Surface* Rgba = SDL_ConvertSurface(Bitmap, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Rgba != nullptr);
    REQUIRE(SDL_LockSurface(Rgba));
    const auto* Pixels = static_cast<const uint8_t*>(Rgba->pixels);
    CHECK(Pixels[0] == 255);
    CHECK(Pixels[1] == 0);
    CHECK(Pixels[2] == 0);
    CHECK(Pixels[4] == 0);
    CHECK(Pixels[5] == 255);
    CHECK(Pixels[6] == 0);
    SDL_UnlockSurface(Rgba);
    SDL_DestroySurface(Rgba);

    REQUIRE(Kernel.Memblocks().MakeMemblockFromBitmap(2, 0, &MemblockError));
    const std::vector<uint8_t>& RoundTrip = ReadMemblockData(Kernel.Memblocks(), 2);
    REQUIRE(RoundTrip.size() == 16u);
    uint32_t RoundTripDepth = 0;
    std::memcpy(&RoundTripDepth, RoundTrip.data() + 8, 4);
    CHECK(RoundTripDepth == 16u);
    CHECK(RoundTrip[12] == 0x00);
    CHECK(RoundTrip[13] == 0xf8);
    CHECK(RoundTrip[14] == 0xe0);
    CHECK(RoundTrip[15] == 0x07);
}

TEST_CASE("COPY FILE never overwrites and reports source and destination failures")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-copy-file-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));
    {
        std::ofstream Source(Root / "source.txt");
        Source << "source";
    }

    dbc::EngineKernel Kernel;
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.AssetRoot = Root;
    std::string Error;
    for (const auto Profile : {dbc::language::LanguageProfile::Dbc, dbc::language::LanguageProfile::DbPro})
    {
        Context.Profile = Profile;
        const std::string Destination = Profile == dbc::language::LanguageProfile::Dbc ? "classic.txt" : "pro.txt";
        REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_CopyFile,
                              {BuiltinValue::FromString("source.txt"), BuiltinValue::FromString(Destination)}, Error));
        std::ifstream Copy(Root / Destination);
        std::string Contents;
        Copy >> Contents;
        CHECK(Contents == "source");
        Error.clear();
        CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_CopyFile,
                                  {BuiltinValue::FromString("source.txt"), BuiltinValue::FromString(Destination)},
                                  Error));
        CHECK(Error.find("refuses to overwrite") != std::string::npos);
    }
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_CopyFile,
                              {BuiltinValue::FromString("missing.txt"), BuiltinValue::FromString("copy.txt")}, Error));
    CHECK(Error.find("source is not a readable file") != std::string::npos);
    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Classic portable files memblocks and DIRBLOCKs round-trip inside the asset root")
{
    std::string MemblockError;
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-classic-files-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directories(Root / "source/nested"));
    {
        std::ofstream Data(Root / "source/nested/data.bin", std::ios::binary);
        const char Bytes[] = {1, 2, 3, 4};
        Data.write(Bytes, sizeof(Bytes));
    }

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-portable-files", 16, 16));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = Root;
    std::vector<std::string> OutputLines;
    Context.Output = [&](std::string_view Text) { OutputLines.emplace_back(Text); };
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Number = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return ReadBuiltinInteger(Result);
    };
    const auto String = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments = {})
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsString();
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeDirectory, {BuiltinValue::FromString("created")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeFile, {BuiltinValue::FromString("created/empty.bin")}));
    CHECK(Number(dbc::language::SymbolId::Function_FileSize, {BuiltinValue::FromString("created/empty.bin")}) == 0);
    REQUIRE(Command(dbc::language::SymbolId::Command_RenameFile,
                    {BuiltinValue::FromString("created/empty.bin"), BuiltinValue::FromString("created/renamed.bin")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MoveFile,
                    {BuiltinValue::FromString("created/renamed.bin"), BuiltinValue::FromString("moved.bin")}));
    CHECK(std::filesystem::is_regular_file(Root / "moved.bin"));
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteDirectory, {BuiltinValue::FromString("created")}));

    CHECK(Number(dbc::language::SymbolId::Function_PathExist, {BuiltinValue::FromString("source\\nested")}) == 1);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetDir, {BuiltinValue::FromString("source\\nested")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_Cd, {BuiltinValue::FromString("../..")}));
    CHECK(std::filesystem::equivalent(std::filesystem::path(String(dbc::language::SymbolId::Function_GetDirString)),
                                      Root));

    REQUIRE(Command(dbc::language::SymbolId::Command_FindFirst, {}));
    const std::string FirstName = String(dbc::language::SymbolId::Function_GetFileNameString);
    CHECK_FALSE(FirstName.empty());
    CHECK(Number(dbc::language::SymbolId::Function_GetFileType, {}) >= 0);
    CHECK_FALSE(String(dbc::language::SymbolId::Function_GetFileDateString).empty());
    REQUIRE(Command(dbc::language::SymbolId::Command_FindNext, {}));
    CHECK_FALSE(String(dbc::language::SymbolId::Function_GetFileNameString).empty());
    REQUIRE(Command(dbc::language::SymbolId::Command_Dir, {}));
    CHECK_FALSE(OutputLines.empty());
    const std::size_t DirectoryLineCount = OutputLines.size();
    REQUIRE(Command(dbc::language::SymbolId::Command_Drivelist, {}));
    CHECK(OutputLines.size() > DirectoryLineCount);
    REQUIRE(Command(dbc::language::SymbolId::Command_PerformChecklistForDrives, {}));
    REQUIRE_FALSE(Context.Checklist.empty());
    REQUIRE(Command(dbc::language::SymbolId::Command_EmptyChecklist, {}));
    CHECK(Context.Checklist.empty());

    REQUIRE(Kernel.Memblocks().MakeMemblock(1, 4, &MemblockError));
    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 0, 0x04030201, &MemblockError));
    REQUIRE(Kernel.Memblocks().MakeMemblock(2, 4, &MemblockError));
    REQUIRE(Command(dbc::language::SymbolId::Command_CopyMemblock,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(4)}));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Memblocks().ReadMemblockDword(Output, 2, 0, &Error); }) == 0x04030201);

    REQUIRE(Command(dbc::language::SymbolId::Command_OpenToWrite,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromString("memblock.pack")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_WriteMemblock,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_OpenToRead,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromString("memblock.pack")}));
    CHECK(Number(dbc::language::SymbolId::Function_FileOpen, {BuiltinValue::FromNumber(1)}) == 1);
    CHECK(Number(dbc::language::SymbolId::Function_FileEnd, {BuiltinValue::FromNumber(1)}) == 0);
    REQUIRE(Command(dbc::language::SymbolId::Command_ReadMemblock,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(1)}));
    CHECK(ReadMemblockData(Kernel.Memblocks(), 3) == ReadMemblockData(Kernel.Memblocks(), 2));

    REQUIRE(Command(dbc::language::SymbolId::Command_OpenToRead,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromString("source/nested/data.bin")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeMemblockFromFile,
                    {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(2)}));
    CHECK(ReadMemblockData(Kernel.Memblocks(), 4) == (std::vector<std::uint8_t>{1, 2, 3, 4}));
    CHECK(Number(dbc::language::SymbolId::Function_FileSize, {BuiltinValue::FromString("source/nested/data.bin")}) ==
          4);

    REQUIRE(Command(dbc::language::SymbolId::Command_OpenToWrite,
                    {BuiltinValue::FromNumber(3), BuiltinValue::FromString("directory.pack")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_WriteDirblock,
                    {BuiltinValue::FromNumber(3), BuiltinValue::FromString("source")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_OpenToRead,
                    {BuiltinValue::FromNumber(3), BuiltinValue::FromString("directory.pack")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ReadDirblock,
                    {BuiltinValue::FromNumber(3), BuiltinValue::FromString("restored")}));
    REQUIRE(Command(dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(3)}));
    std::ifstream Restored(Root / "restored/nested/data.bin", std::ios::binary);
    const std::vector<std::uint8_t> RestoredBytes((std::istreambuf_iterator<char>(Restored)),
                                                  std::istreambuf_iterator<char>());
    CHECK(RestoredBytes == (std::vector<std::uint8_t>{1, 2, 3, 4}));

    {
        std::ofstream Unsafe(Root / "unsafe.pack", std::ios::binary);
        const auto WriteLong = [&](std::int32_t Value)
        {
            for (int Shift = 0; Shift < 32; Shift += 8)
                Unsafe.put(static_cast<char>((static_cast<std::uint32_t>(Value) >> Shift) & 0xffu));
        };
        Unsafe.write("ODBDIR01", 8);
        WriteLong(1);
        const std::string UnsafeName = "../escape-" + std::to_string(Suffix) + ".bin";
        WriteLong(static_cast<std::int32_t>(UnsafeName.size()));
        Unsafe.write(UnsafeName.data(), static_cast<std::streamsize>(UnsafeName.size()));
        WriteLong(0);
        WriteLong(1);
        Unsafe.put('x');
    }
    REQUIRE(Command(dbc::language::SymbolId::Command_OpenToRead,
                    {BuiltinValue::FromNumber(4), BuiltinValue::FromString("unsafe.pack")}));
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_ReadDirblock,
                        {BuiltinValue::FromNumber(4), BuiltinValue::FromString("unsafe-output")}));
    CHECK(Error.find("unsafe entry path") != std::string::npos);
    REQUIRE(Command(dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(4)}));
    CHECK_FALSE(std::filesystem::exists(Root.parent_path() / ("escape-" + std::to_string(Suffix) + ".bin")));

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("File and memblock compatibility failures return typed runtime errors")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-file-errors-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.AssetRoot = Root;
    std::string Error;

    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_OpenToRead,
                              {BuiltinValue::FromNumber(0), BuiltinValue::FromString("missing.bin")}, Error));
    CHECK(Error.find("between 1 and 64") != std::string::npos);
    Error.clear();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_OpenToWrite,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromString("data.bin")}, Error));
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_OpenToWrite,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromString("other.bin")}, Error));
    CHECK(Error.find("already open") != std::string::npos);
    Error.clear();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(1)}, Error));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_OpenToWrite,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromString("pack.bin")}, Error));
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_WriteFileblock,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromString("missing-source.bin")}, Error));
    CHECK(Error.find("WRITE FILEBLOCK cannot read") != std::string::npos);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(2)}, Error));

    {
        std::ofstream Truncated(Root / "truncated.pack", std::ios::binary);
        const char Header[2]{1, 0};
        Truncated.write(Header, 2);
    }
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_OpenToRead,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromString("truncated.pack")}, Error));
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_ReadFileblock,
                              {BuiltinValue::FromNumber(2), BuiltinValue::FromString("must-not-exist.bin")}, Error));
    CHECK(Error.find("ended before the requested value was complete") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(Root / "must-not-exist.bin"));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_CloseFile, {BuiltinValue::FromNumber(2)}, Error));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeMemblock,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4)}, Error));
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_WriteMemblockDword,
                              {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(42)},
                              Error));
    CHECK(Error.find("outside the allocated range") != std::string::npos);
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_DeleteDirectory,
                              {BuiltinValue::FromString(".")}, Error));
    CHECK(Error.find("invalid or active directory") != std::string::npos);

    const std::filesystem::path Target = Root / "target";
    REQUIRE(std::filesystem::create_directory(Target));
    std::error_code SymlinkError;
    std::filesystem::create_directory_symlink(Target, Root / "alias", SymlinkError);
    if (!SymlinkError)
    {
        Error.clear();
        CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_DeleteDirectory,
                                  {BuiltinValue::FromString("alias")}, Error));
        CHECK(Error.find("symbolic link") != std::string::npos);
        CHECK(std::filesystem::is_directory(Target));
    }

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Both Classic COLOR LIGHT overloads update the renderer light state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeLight, {BuiltinValue::FromNumber(7)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorLight,
                          {BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(0x102030)}, Error));
    int Color = 0;
    Kernel.Lights().ForEachLight(
        [&](int Id, const dbc::Light3D& Light)
        {
            if (Id == 7)
                Color = Light.Color;
        });
    CHECK(Color == 0x102030);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorLight,
                          {BuiltinValue::FromNumber(7), BuiltinValue::FromNumber(12), BuiltinValue::FromNumber(34),
                           BuiltinValue::FromNumber(56)},
                          Error));
    Kernel.Lights().ForEachLight(
        [&](int Id, const dbc::Light3D& Light)
        {
            if (Id == 7)
                Color = Light.Color;
        });
    CHECK(Color == 0x0C2238);
}

TEST_CASE("Classic light commands update visibility direction position and ambient state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ColorAmbientLight,
                          {BuiltinValue::FromNumber(0x112233)}, Error));
    CHECK(Kernel.Lights().AmbientColor() == 0x112233);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeLight, {BuiltinValue::FromNumber(4)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetDirectionalLight,
                          {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-2),
                           BuiltinValue::FromNumber(3)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PositionLight,
                          {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                           BuiltinValue::FromNumber(30)},
                          Error));
    CHECK(Kernel.Lights().LightPositionX(4) == doctest::Approx(10.0f));
    CHECK(Kernel.Lights().LightPositionY(4) == doctest::Approx(20.0f));
    CHECK(Kernel.Lights().LightPositionZ(4) == doctest::Approx(30.0f));
    CHECK(Kernel.Lights().LightType(4) == 3);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_HideLight, {BuiltinValue::FromNumber(4)}, Error));
    CHECK(Kernel.Lights().LightVisible(4) == 0);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ShowLight, {BuiltinValue::FromNumber(4)}, Error));
    CHECK(Kernel.Lights().LightVisible(4) == 1);
}

TEST_CASE("Portable cameras and lights preserve DBPro state and typed failures")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;

    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };

    CHECK(Kernel.Camera().CameraExist(0) == 1);
    REQUIRE(Command(dbc::language::SymbolId::Command_AutocamOff, {}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeObjectCube,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10)}));
    CHECK(Kernel.Camera().PositionZ() == doctest::Approx(3.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_AutocamOn, {}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeObjectSphere,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10)}));
    CHECK(Kernel.Camera().PositionY() == doctest::Approx(5.0f));
    CHECK(Kernel.Camera().PositionZ() == doctest::Approx(-30.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeCamera, {BuiltinValue::FromNumber(1)}));
    CHECK(Function(dbc::language::SymbolId::Function_CameraExist, {BuiltinValue::FromNumber(1)}) == 1.0);
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                     BuiltinValue::FromNumber(30)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_RotateCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(11), BuiltinValue::FromNumber(22),
                     BuiltinValue::FromNumber(33)}));
    CHECK(Function(dbc::language::SymbolId::Function_CameraPositionX, {BuiltinValue::FromNumber(1)}) ==
          doctest::Approx(10.0));
    CHECK(Function(dbc::language::SymbolId::Function_CameraAngleZ, {BuiltinValue::FromNumber(1)}) ==
          doctest::Approx(33.0));

    Context.Vectors[1] = {40.0f, 50.0f, 60.0f};
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    Context.Vectors[1] = {15.0f, 25.0f, 35.0f};
    REQUIRE(Command(dbc::language::SymbolId::Command_RotateCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Camera().PositionY(Output, 1, &Error); }) == doctest::Approx(50.0f));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Camera().AngleY(Output, 1, &Error); }) == doctest::Approx(25.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_SetCurrentCamera, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionCamera,
                    {BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(7)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_XrotateCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_YrotateCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(20)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ZrotateCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(30)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PitchCameraUp,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(5)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PitchCameraDown,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_TurnCameraLeft,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_TurnCameraRight,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_RollCameraLeft,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(8)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_RollCameraRight,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MoveCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PointCamera,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraFov,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(80)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraRange,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(900)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraAspect,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1.5)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraView,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3),
                     BuiltinValue::FromNumber(42), BuiltinValue::FromNumber(43)}));
    CHECK(Kernel.Camera().HasCameraView());
    CHECK(Kernel.Camera().ViewLeft() == 2);
    REQUIRE(Command(dbc::language::SymbolId::Command_ClearCameraView,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0x123456)}));
    CHECK(Kernel.Camera().ClearRequested());
    CHECK(Kernel.Camera().ClearColor() == 0x123456);
    CHECK(Kernel.Camera().HasCameraView());
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraRotationZyx, {BuiltinValue::FromNumber(1)}));
    CHECK(Kernel.Camera().IsZyxRotation());
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraRotationXyz, {BuiltinValue::FromNumber(1)}));
    CHECK_FALSE(Kernel.Camera().IsZyxRotation());

    Kernel.Objects().RotateObject(1, 12.0f, 34.0f, 56.0f);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraToObjectOrientation,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Camera().AngleY(Output, 1, &Error); }) == doctest::Approx(34.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetObjectToCameraOrientation, {BuiltinValue::FromNumber(2)}));
    CHECK(Kernel.Objects().ObjectAngleZ(2) == doctest::Approx(56.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetCameraToFollow,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(20), BuiltinValue::FromNumber(45), BuiltinValue::FromNumber(30),
                     BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ControlCameraUsingArrowkeys,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3)}));
    CHECK(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                  { return Kernel.Camera().AngleX(Output, 1, &Error); }) == doctest::Approx(0.0f));
    Context.Vectors[2] = {};
    Context.Vectors[3] = {};
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToCameraPosition,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToCameraRotation,
                    {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1)}));
    CHECK(Context.Vectors[2][0] ==
          doctest::Approx(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                                  { return Kernel.Camera().PositionX(Output, 1, &Error); })));
    CHECK(Context.Vectors[3][1] ==
          doctest::Approx(ReadEngineNumber<float>([&](std::string& Error, float& Output)
                                                  { return Kernel.Camera().AngleY(Output, 1, &Error); })));

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeLight, {BuiltinValue::FromNumber(2)}));
    CHECK(Function(dbc::language::SymbolId::Function_LightExist, {BuiltinValue::FromNumber(2)}) == 1.0);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetAmbientLight, {BuiltinValue::FromNumber(40)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ColorAmbientLight, {BuiltinValue::FromNumber(0x102030)}));
    CHECK(Function(dbc::language::SymbolId::Function_AmbientLight, {}) == doctest::Approx(40.0));
    REQUIRE(Command(dbc::language::SymbolId::Command_ColorLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(12), BuiltinValue::FromNumber(34),
                     BuiltinValue::FromNumber(56)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(20),
                     BuiltinValue::FromNumber(30)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_RotateLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(15), BuiltinValue::FromNumber(25),
                     BuiltinValue::FromNumber(35)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_PointLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                     BuiltinValue::FromNumber(0)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetLightRange,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(750)}));
    CHECK(Function(dbc::language::SymbolId::Function_LightRange, {BuiltinValue::FromNumber(2)}) ==
          doctest::Approx(750.0));
    CHECK(Function(dbc::language::SymbolId::Function_LightPositionZ, {BuiltinValue::FromNumber(2)}) ==
          doctest::Approx(30.0));
    CHECK(std::abs(Function(dbc::language::SymbolId::Function_LightDirectionX, {BuiltinValue::FromNumber(2)})) > 0.0);
    REQUIRE(Command(dbc::language::SymbolId::Command_HideLight, {BuiltinValue::FromNumber(2)}));
    CHECK(Function(dbc::language::SymbolId::Function_LightVisible, {BuiltinValue::FromNumber(2)}) == 0.0);
    REQUIRE(Command(dbc::language::SymbolId::Command_ShowLight, {BuiltinValue::FromNumber(2)}));

    REQUIRE(Command(dbc::language::SymbolId::Command_SetDirectionalLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-2),
                     BuiltinValue::FromNumber(3)}));
    CHECK(Function(dbc::language::SymbolId::Function_LightType, {BuiltinValue::FromNumber(2)}) == 3.0);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetPointLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(5),
                     BuiltinValue::FromNumber(6)}));
    CHECK(Kernel.Lights().LightType(2) == 1);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetSpotLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(30), BuiltinValue::FromNumber(60)}));
    CHECK(Kernel.Lights().LightType(2) == 2);

    Kernel.Objects().PositionObject(1, 7.0f, 8.0f, 9.0f);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetLightToObjectPosition,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetLightToObjectOrientation,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(1)}));
    CHECK(Kernel.Lights().LightPositionY(2) == doctest::Approx(8.0f));
    Context.Vectors[4] = {3.0f, 4.0f, 5.0f};
    REQUIRE(Command(dbc::language::SymbolId::Command_PositionLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4)}));
    Context.Vectors[4] = {6.0f, 7.0f, 8.0f};
    REQUIRE(Command(dbc::language::SymbolId::Command_RotateLight,
                    {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4)}));
    Context.Vectors[5] = {};
    Context.Vectors[6] = {};
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToLightPosition,
                    {BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(2)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetVector3ToLightRotation,
                    {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(2)}));
    CHECK(Context.Vectors[5] == std::array<float, 3>{3.0f, 4.0f, 5.0f});
    CHECK(Context.Vectors[6] == std::array<float, 3>{6.0f, 7.0f, 8.0f});
    CHECK(Function(dbc::language::SymbolId::Function_GetMaximumLights, {}) == 19.0);

    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteLight, {BuiltinValue::FromNumber(2)}));
    CHECK(Kernel.Lights().LightExist(2) == 0);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_DeleteLight, {BuiltinValue::FromNumber(2)}));
    CHECK(Error.find("requires existing light") != std::string::npos);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCurrentCamera(0, &Error); });
    Kernel.Camera().ClearDirty();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().SetCurrentCamera(1, &Error); });
    Kernel.Camera().ClearDirty();
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteCamera, {BuiltinValue::FromNumber(1)}));
    CHECK(Kernel.Camera().CurrentCameraId() == 0);
    CHECK(Kernel.Camera().IsDirty());
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_DeleteCamera, {BuiltinValue::FromNumber(0)}));
    CHECK(Error.find("cannot delete camera zero") != std::string::npos);
}

TEST_CASE("Portable collision volumes hit events limb bounds and picking preserve DBPro queries")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 640, 480));
    Kernel.Camera().SetAutoCamOff();
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &Error); });
    Kernel.Objects().MakeObjectCube(1, 10.0f);
    Kernel.Objects().MakeObjectCube(2, 10.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 50.0f);
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 54.0f);

    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 1);
    CHECK(Kernel.Objects().ObjectCollision(1, 0) == 2);
    CHECK(Kernel.Objects().ObjectHit(1, 2) == 1);
    CHECK(Kernel.Objects().ObjectHit(1, 2) == 0);
    Kernel.Objects().PositionObject(2, 30.0f, 0.0f, 54.0f);
    CHECK(Kernel.Objects().ObjectHit(1, 2) == 0);
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 54.0f);
    CHECK(Kernel.Objects().ObjectHit(1, 2) == 1);

    CHECK(Kernel.Objects().LimbCollision(1, 0, 2, 0) == 1);
    CHECK(Kernel.Objects().LimbHit(1, 0, 2, 0) == 1);
    CHECK(Kernel.Objects().LimbHit(1, 0, 2, 0) == -1);
    Kernel.Objects().PositionObject(2, 30.0f, 0.0f, 54.0f);
    CHECK(Kernel.Objects().LimbCollision(1, 0, 2, 0) == -1);
    CHECK(Kernel.Objects().LimbHit(1, 0, 2, 0) == -1);
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 54.0f);
    CHECK(Kernel.Objects().LimbHit(1, 0, 2, 0) == 1);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_SetObjectCollisionToBoxes, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MakeObjectCollisionBox,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-4), BuiltinValue::FromNumber(-4),
                     BuiltinValue::FromNumber(-4), BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(4),
                     BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteObjectCollisionBox, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetObjectCollisionToSpheres, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetObjectCollisionOff, {BuiltinValue::FromNumber(2)}));
    CHECK(Function(dbc::language::SymbolId::Function_ObjectCollision,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}) == 0.0);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetObjectCollisionOn, {BuiltinValue::FromNumber(2)}));

    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 100.0f);
    CHECK(Function(dbc::language::SymbolId::Function_PickObject,
                   {BuiltinValue::FromNumber(320), BuiltinValue::FromNumber(240), BuiltinValue::FromNumber(1),
                    BuiltinValue::FromNumber(2)}) == 1.0);
    CHECK(Function(dbc::language::SymbolId::Function_GetPickDistance, {}) == doctest::Approx(45.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetPickVectorX, {}) == doctest::Approx(0.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetPickVectorY, {}) == doctest::Approx(0.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetPickVectorZ, {}) == doctest::Approx(45.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_PickScreen,
                    {BuiltinValue::FromNumber(320), BuiltinValue::FromNumber(240), BuiltinValue::FromNumber(25)}));
    CHECK(Kernel.Objects().GetPickVectorZ() == doctest::Approx(25.0f));
    CHECK(Function(dbc::language::SymbolId::Function_IntersectObject,
                   {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                    BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                    BuiltinValue::FromNumber(200)}) == doctest::Approx(95.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_MakeStaticCollisionBox,
                    {BuiltinValue::FromNumber(-10), BuiltinValue::FromNumber(-10), BuiltinValue::FromNumber(-10),
                     BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(10), BuiltinValue::FromNumber(10)}));
    CHECK(Function(dbc::language::SymbolId::Function_GetStaticCollisionHit,
                   {BuiltinValue::FromNumber(20), BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(-1),
                    BuiltinValue::FromNumber(22), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1),
                    BuiltinValue::FromNumber(9), BuiltinValue::FromNumber(-1), BuiltinValue::FromNumber(-1),
                    BuiltinValue::FromNumber(11), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}) == 1.0);
    CHECK(std::abs(Function(dbc::language::SymbolId::Function_GetStaticCollisionX, {})) > 0.0);
    CHECK(Function(dbc::language::SymbolId::Function_GetStaticCollisionY, {}) == doctest::Approx(0.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetStaticCollisionZ, {}) == doctest::Approx(0.0f));
    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteStaticObjects, {}));

    Context.Profile = dbc::language::LanguageProfile::Dbc;
    CHECK(Function(dbc::language::SymbolId::Function_GetObjectCollisionX, {BuiltinValue::FromNumber(1)}) ==
          doctest::Approx(Kernel.Objects().GetObjectCollisionX()));
    CHECK(Function(dbc::language::SymbolId::Function_GetStaticCollisionZ, {}) == doctest::Approx(0.0f));
}

TEST_CASE("Polygon collision uses current world geometry and preserves DBC query contracts")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));

    REQUIRE(Kernel.Objects().MakeObjectPlain(1, 20.0f, 20.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(1));
    Kernel.Objects().MakeObjectSphere(2, 4.0f, 8, 8);
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 1.0f);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 1);
    CHECK(Kernel.Objects().ObjectCollision(2, 1) == 1);

    Kernel.Objects().MakeObjectBox(3, 4.0f, 4.0f, 4.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionToBoxes(3));
    CHECK(Kernel.Objects().ObjectCollision(2, 3) == 1);
    CHECK(Kernel.Objects().ObjectCollision(3, 2) == 1);
    CHECK(Kernel.Objects().ObjectCollision(1, 3) == 1);
    CHECK(Kernel.Objects().ObjectCollision(3, 1) == 1);

    REQUIRE(Kernel.Objects().MakeObjectPlain(4, 0.0f, 20.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(4));
    CHECK(Kernel.Objects().ObjectCollision(4, 1) == 1);
    CHECK(Kernel.Objects().ObjectCollision(1, 4) == 1);
    Kernel.Objects().PositionObject(4, 30.0f, 0.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(4, 1) == 0);

    Kernel.Objects().HideObject(1);
    Kernel.Objects().PositionObject(4, 0.0f, 0.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(4, 1) == 1);
    REQUIRE(Kernel.Objects().SetObjectCollisionOff(4));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(4));
    CHECK(Kernel.Objects().ObjectCollision(4, 1) == 0);
    REQUIRE(Kernel.Objects().SetObjectCollisionOn(4));

    Kernel.Objects().MakeObjectCube(5, 20.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(5));
    Kernel.Objects().MakeObjectSphere(6, 2.0f, 8, 8);
    CHECK(Kernel.Objects().ObjectCollision(5, 6) == 1);
    CHECK(Kernel.Objects().ObjectCollision(6, 5) == 1);

    REQUIRE(Kernel.Objects().MakeObjectPlain(7, 0.0f, 20.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(7));
    Kernel.Objects().RotateObject(7, 65.0f, 270.0f, 0.0f);
    REQUIRE(Kernel.Objects().SetObjectRotationZyx(7));
    REQUIRE(Kernel.Objects().MakeObjectPlain(8, 100.0f, 200.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(8));
    CHECK(Kernel.Objects().ObjectCollision(7, 8) == 1);

    Kernel.Objects().MakeObjectCube(10, 4.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionOff(10));
    Kernel.Objects().PositionObject(10, 50.0f, 0.0f, 0.0f);
    REQUIRE(Kernel.Objects().MakeObjectPlain(11, 0.0f, 20.0f));
    REQUIRE(Kernel.Objects().GlueObjectToLimb(11, 10, 0));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(11));
    REQUIRE(Kernel.Objects().MakeObjectPlain(12, 100.0f, 100.0f));
    Kernel.Objects().PositionObject(12, 50.0f, 0.0f, 0.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(12));
    CHECK(Kernel.Objects().ObjectCollision(11, 12) == 1);
    Kernel.Objects().PositionObject(10, 150.0f, 0.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(11, 12) == 0);

    REQUIRE(Kernel.Objects().MakeObjectPlain(13, 20.0f, 20.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(13));
    REQUIRE(Kernel.Objects().MakeObjectPlain(14, 0.0f, 10.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(14));
    CHECK(Kernel.Objects().ObjectCollision(13, 14) == 1);
    REQUIRE(Kernel.Objects().GetObject(13));
    for (dbc::Vec3& Vertex : Kernel.Objects().GetObject(13)->Meshes.front().EditGeometry().Vertices)
        Vertex.X += 50.0f;
    CHECK(Kernel.Objects().ObjectCollision(13, 14) == 0);

    REQUIRE(Kernel.Objects().MakeObjectPlain(9, 10.0f, 10.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(9));
    REQUIRE(Kernel.Objects().GetObject(9));
    Kernel.Objects().GetObject(9)->Meshes.clear();
    CHECK(Kernel.Objects().ObjectCollision(9, 1) == 0);
}

TEST_CASE("Object collision feedback is per object and reports attempted minus corrected position")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return Result.AsNumber();
    };
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().MakeObjectCube(2, 2.0f);
    REQUIRE(Kernel.Objects().MakeObjectCollisionBox(1, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0));
    REQUIRE(Kernel.Objects().MakeObjectCollisionBox(2, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1));
    Kernel.Objects().PositionObject(2, 1.5f, 0.0f, 0.0f);

    REQUIRE(Kernel.Objects().ObjectCollision(1, 2) == 1);
    CHECK(Kernel.Objects().GetObjectCollisionX() == 0.5f);
    CHECK(Kernel.Objects().GetObjectCollisionX() == doctest::Approx(0.5f));
    CHECK(Kernel.Objects().GetObjectCollisionX(1) == doctest::Approx(0.5f));
    CHECK(Kernel.Objects().GetObjectCollisionY(1) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionZ(1) == doctest::Approx(0.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetObjectCollisionX, {}) == doctest::Approx(0.5f));
    CHECK(Function(dbc::language::SymbolId::Function_GetObjectCollisionY, {}) == doctest::Approx(0.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetObjectCollisionZ, {}) == doctest::Approx(0.0f));
    CHECK(Function(dbc::language::SymbolId::Function_GetObjectCollisionX, {BuiltinValue::FromNumber(1)}) ==
          doctest::Approx(0.5f));
    REQUIRE(Kernel.Objects().ObjectCollision(1, 0) == 2);
    CHECK(Kernel.Objects().GetObjectCollisionX(1) == doctest::Approx(0.5f));

    REQUIRE(Kernel.Objects().ObjectCollision(2, 1) == 1);
    CHECK(Kernel.Objects().GetObjectCollisionX() == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionY(2) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionZ(2) == doctest::Approx(0.0f));
    CHECK(Kernel.Objects().GetObjectCollisionX(1) == doctest::Approx(0.5f));

    REQUIRE(Kernel.Objects().MakeObjectPlain(3, 20.0f, 20.0f));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(3));
    REQUIRE(Kernel.Objects().ObjectCollision(1, 3) == 1);
    CHECK(std::abs(Kernel.Objects().GetObjectCollisionZ(1)) > 0.0f);

    Kernel.Objects().PositionObject(2, 10.0f, 0.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);
    CHECK(Kernel.Objects().GetObjectCollisionX(1) == doctest::Approx(0.0f));
}

TEST_CASE("Contained collision box exits along polygon geometry rather than the polygon AABB")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().MakeObjectCollisionBox(1, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0));
    Kernel.Objects().MakeObjectTriangle(2, -10.0f, 10.0f, -10.0f, 10.0f, -10.0f, -10.0f, 0.0f, 0.0f, 10.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(2));
    REQUIRE(Kernel.Objects().ObjectCollision(1, 2) == 1);
    const dbc::Vec3 PlaneFeedback{Kernel.Objects().GetObjectCollisionX(1), Kernel.Objects().GetObjectCollisionY(1),
                                  Kernel.Objects().GetObjectCollisionZ(1)};
    CHECK(std::abs(PlaneFeedback.X) > 0.5f);
    CHECK(std::abs(PlaneFeedback.Y) > 0.5f);
    CHECK(std::abs(PlaneFeedback.Z) < 0.01f);
    Kernel.Objects().PositionObject(1, -PlaneFeedback.X, -PlaneFeedback.Y, -PlaneFeedback.Z);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);

    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 0.0f);
    Kernel.Objects().DeleteObject(2);
    Kernel.Objects().MakeObjectCube(2, 10.0f);
    Kernel.Objects().RotateObject(2, 23.0f, 31.0f, 17.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(2));

    REQUIRE(Kernel.Objects().ObjectCollision(1, 2) == 1);
    const dbc::Vec3 Feedback{Kernel.Objects().GetObjectCollisionX(1), Kernel.Objects().GetObjectCollisionY(1),
                             Kernel.Objects().GetObjectCollisionZ(1)};
    const int NonZeroComponents = static_cast<int>(std::abs(Feedback.X) > 0.01f) +
                                  static_cast<int>(std::abs(Feedback.Y) > 0.01f) +
                                  static_cast<int>(std::abs(Feedback.Z) > 0.01f);
    CHECK(NonZeroComponents >= 2);

    Kernel.Objects().PositionObject(1, -Feedback.X, -Feedback.Y, -Feedback.Z);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);
}

TEST_CASE("Collision feedback separates complete multi-surface polygon sets")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().MakeObjectCollisionBox(1, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0));

    Kernel.Objects().MakeObjectTriangle(2, -0.5f, -10.0f, -10.0f, -0.5f, 10.0f, -10.0f, -0.5f, 0.0f, 10.0f);
    dbc::Object3D* ParallelWalls = Kernel.Objects().GetObject(2);
    REQUIRE(ParallelWalls != nullptr);
    REQUIRE(ParallelWalls->Meshes.size() == 1);
    dbc::Mesh SecondParallelWall = ParallelWalls->Meshes.front();
    for (dbc::Vec3& Vertex : SecondParallelWall.EditGeometry().Vertices)
        Vertex.X += 1.0f;
    ParallelWalls->Meshes.push_back(std::move(SecondParallelWall));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(2));

    REQUIRE(Kernel.Objects().ObjectCollision(1, 2) == 1);
    const dbc::Vec3 ParallelFeedback{Kernel.Objects().GetObjectCollisionX(1), Kernel.Objects().GetObjectCollisionY(1),
                                     Kernel.Objects().GetObjectCollisionZ(1)};
    CHECK(std::abs(ParallelFeedback.X) > 1.49f);
    CHECK(std::abs(ParallelFeedback.Y) < 0.01f);
    CHECK(std::abs(ParallelFeedback.Z) < 0.01f);
    Kernel.Objects().PositionObject(1, -ParallelFeedback.X, -ParallelFeedback.Y, -ParallelFeedback.Z);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);

    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 0.0f);
    Kernel.Objects().DeleteObject(2);
    Kernel.Objects().MakeObjectTriangle(2, 0.5f, -10.0f, -10.0f, 0.5f, 10.0f, -10.0f, 0.5f, 0.0f, 10.0f);
    dbc::Object3D* CornerWalls = Kernel.Objects().GetObject(2);
    REQUIRE(CornerWalls != nullptr);
    REQUIRE(CornerWalls->Meshes.size() == 1);
    dbc::Mesh SecondCornerWall = CornerWalls->Meshes.front();
    SecondCornerWall.EditGeometry().Vertices = {{-10.0f, 0.5f, -10.0f}, {10.0f, 0.5f, -10.0f}, {0.0f, 0.5f, 10.0f}};
    CornerWalls->Meshes.push_back(std::move(SecondCornerWall));
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(2));

    REQUIRE(Kernel.Objects().ObjectCollision(1, 2) == 1);
    const dbc::Vec3 CornerFeedback{Kernel.Objects().GetObjectCollisionX(1), Kernel.Objects().GetObjectCollisionY(1),
                                   Kernel.Objects().GetObjectCollisionZ(1)};
    CHECK(std::abs(CornerFeedback.X) > 0.49f);
    CHECK(std::abs(CornerFeedback.Y) > 0.49f);
    CHECK(std::abs(CornerFeedback.Z) < 0.01f);
    Kernel.Objects().PositionObject(1, -CornerFeedback.X, -CornerFeedback.Y, -CornerFeedback.Z);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);
}

TEST_CASE("Closed polygon containment detects spheres inside and around the mesh")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 64));
    Kernel.Objects().MakeObjectCube(1, 10.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionToPolygons(1));
    Kernel.Objects().MakeObjectSphere(2, 2.0f, 8, 8);

    CHECK(Kernel.Objects().ObjectCollision(2, 1) == 1);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 1);
    Kernel.Objects().PositionObject(2, 4.5f, 0.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(2, 1) == 1);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 1);
    Kernel.Objects().PositionObject(2, 7.0f, 0.0f, 0.0f);
    CHECK(Kernel.Objects().ObjectCollision(2, 1) == 0);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);

    Kernel.Objects().MakeObjectSphere(3, 20.0f, 8, 8);
    CHECK(Kernel.Objects().ObjectCollision(3, 1) == 1);
    CHECK(Kernel.Objects().ObjectCollision(1, 3) == 1);
}

TEST_CASE("Sprite priority determines the renderer traversal order")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(1, 0, 0, 101, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(2, 0, 0, 102, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(3, 0, 0, 103, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpritePriority(1, 20, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpritePriority(2, -5, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpritePriority(3, 20, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSprite(1, 0, 0, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().OffsetSprite(1, 7, 8, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SizeSprite(1, 21, 22, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PasteSprite(1, 31, 32, &Error); });

    std::vector<int> RenderOrder;
    Kernel.Sprites().ForEachVisibleSprite([&](const dbc::SpriteData& Sprite)
                                          { RenderOrder.push_back(Sprite.ImageId); });
    CHECK(RenderOrder == std::vector<int>{102, 101, 103});
    const dbc::SpriteData* First = nullptr;
    Kernel.Sprites().ForEachVisibleSprite(
        [&](const dbc::SpriteData& Sprite)
        {
            if (Sprite.ImageId == 101)
                First = &Sprite;
        });
    REQUIRE(First != nullptr);
    CHECK(First->Backsave == 0);
    CHECK_FALSE(First->Transparent);
    CHECK(First->OffsetX == 7);
    CHECK(First->OffsetY == 8);
    CHECK(First->SizeX == 21);
    CHECK(First->SizeY == 22);
    CHECK(First->X == doctest::Approx(0.0f));
    CHECK(First->Y == doctest::Approx(0.0f));
    int Pasted = 0;
    Kernel.Sprites().ForEachPastedSprite(
        [&](const dbc::SpriteData& Sprite)
        {
            ++Pasted;
            CHECK(Sprite.X == doctest::Approx(31.0f));
            CHECK(Sprite.Y == doctest::Approx(32.0f));
        });
    CHECK(Pasted == 1);
    BuiltinValue Image;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_SpriteImage, {BuiltinValue::FromNumber(1)}, Image,
                           Error));
    CHECK(ReadBuiltinInteger(Image) == 101);
}

TEST_CASE("Classic BOBs retain independent render collision and transform state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-bobs", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Core().EnsureInitialized());

    SDL_Surface* Surface = SDL_CreateSurface(8, 6, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    for (int Y = 0; Y < Surface->h; ++Y)
    {
        for (int X = 0; X < Surface->w; ++X)
        {
            const bool Right = X >= Surface->w / 2;
            const bool Bottom = Y >= Surface->h / 2;
            const Uint8 Red = Bottom ? (Right ? 255 : 0) : (Right ? 0 : 255);
            const Uint8 Green = Bottom ? (Right ? 255 : 0) : (Right ? 255 : 0);
            const Uint8 Blue = Bottom && !Right ? 255 : 0;
            REQUIRE(SDL_WriteSurfacePixel(Surface, X, Y, Red, Green, Blue, 255));
        }
    }
    SDL_Surface* OriginalSurface = SDL_DuplicateSurface(Surface);
    REQUIRE(OriginalSurface != nullptr);
    Kernel.ImageStoreRef().StoreImageFromSurface(21, Surface);
    SDL_DestroySurface(Surface);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };
    const auto Function = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        BuiltinValue Result;
        Error.clear();
        REQUIRE(InvokeFunction(Context, Symbol, std::vector<BuiltinValue>(Arguments), Result, Error));
        return ReadBuiltinInteger(Result);
    };

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(1, 25, 25, 21, &Error); });
    REQUIRE(Command(dbc::language::SymbolId::Command_Bob, {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(4),
                                                           BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(21)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_Bob, {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(8),
                                                           BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(21)}));
    bool FoundDefaultBob = false;
    Kernel.Sprites().ForEachVisibleSprite(
        [&](const dbc::SpriteData& Value)
        {
            if (Value.ImageId == 21 && Value.X == doctest::Approx(4.0f))
            {
                FoundDefaultBob = true;
                CHECK(Value.Backsave == 1);
                CHECK(Value.Transparent);
            }
        });
    CHECK(FoundDefaultBob);
    {
        std::string SpriteError;
        CHECK_FALSE(Kernel.Sprites().SetBob(1, 2, 1, &SpriteError));
        CHECK_FALSE(SpriteError.empty());
    };
    {
        std::string SpriteError;
        CHECK_FALSE(Kernel.Sprites().SetBob(1, 1, 2, &SpriteError));
        CHECK_FALSE(SpriteError.empty());
    };
    REQUIRE(Command(dbc::language::SymbolId::Command_SetBob,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_MirrorBob, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_FlipBob, {BuiltinValue::FromNumber(1)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_OffsetBob,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(3)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_ScaleBob,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(200)}));
    REQUIRE(Command(dbc::language::SymbolId::Command_StretchBob,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(150), BuiltinValue::FromNumber(50)}));
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);

    REQUIRE(Command(dbc::language::SymbolId::Command_Bob, {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(9),
                                                           BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(21)}));

    CHECK(Function(dbc::language::SymbolId::Function_BobExist, {BuiltinValue::FromNumber(1)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobX, {BuiltinValue::FromNumber(1)}) == 4);
    CHECK(Function(dbc::language::SymbolId::Function_BobY, {BuiltinValue::FromNumber(1)}) == 5);
    CHECK(Function(dbc::language::SymbolId::Function_BobImage, {BuiltinValue::FromNumber(1)}) == 21);
    CHECK(Function(dbc::language::SymbolId::Function_BobMirrored, {BuiltinValue::FromNumber(1)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobFlipped, {BuiltinValue::FromNumber(1)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobOffsetX, {BuiltinValue::FromNumber(1)}) == 2);
    CHECK(Function(dbc::language::SymbolId::Function_BobOffsetY, {BuiltinValue::FromNumber(1)}) == 3);
    CHECK(Function(dbc::language::SymbolId::Function_BobScaleX, {BuiltinValue::FromNumber(1)}) == 150);
    CHECK(Function(dbc::language::SymbolId::Function_BobScaleY, {BuiltinValue::FromNumber(1)}) == 50);
    CHECK(Function(dbc::language::SymbolId::Function_BobWidth, {BuiltinValue::FromNumber(1)}) == 12);
    CHECK(Function(dbc::language::SymbolId::Function_BobHeight, {BuiltinValue::FromNumber(1)}) == 3);
    CHECK(Function(dbc::language::SymbolId::Function_BobCollision,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2)}) == 0);
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 0);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(3, 24, 5, &Error); });
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 0);
    // BOB COLLISION reports the overlap itself, not whether the query succeeded.
    CHECK(Function(dbc::language::SymbolId::Function_BobCollision,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 0);
    CHECK(Function(dbc::language::SymbolId::Function_BobCollision,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(0)}) == 2);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(1, 20, 5, &Error); });
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().HideBob(1, &Error); });
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().ShowBob(1, &Error); });
    CHECK(Function(dbc::language::SymbolId::Function_BobHit,
                   {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(3)}) == 1);

    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* PresentedFrame = Kernel.Renderer().CaptureScreen();
    REQUIRE(PresentedFrame != nullptr);
    SDL_DestroySurface(PresentedFrame);
    SDL_Surface* ReplacementSurface = SDL_CreateSurface(8, 6, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(ReplacementSurface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(ReplacementSurface, nullptr, SDL_MapSurfaceRGBA(ReplacementSurface, 0, 255, 255, 255)));
    Kernel.ImageStoreRef().StoreImageFromSurface(21, ReplacementSurface);
    SDL_DestroySurface(ReplacementSurface);
    REQUIRE(Command(dbc::language::SymbolId::Command_Bob, {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(18),
                                                           BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(21)}));
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) != nullptr);
    Uint8 SnapshotRed = 0;
    Uint8 SnapshotGreen = 0;
    Uint8 SnapshotBlue = 0;
    Uint8 SnapshotAlpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Kernel.ImageStoreRef().GetBitmapSurface(0), 28, 4, &SnapshotRed, &SnapshotGreen,
                                 &SnapshotBlue, &SnapshotAlpha));
    CHECK(SnapshotRed <= 16);
    CHECK(SnapshotGreen <= 16);
    CHECK(SnapshotBlue >= 240);
    Kernel.ImageStoreRef().StoreImageFromSurface(21, OriginalSurface);
    SDL_DestroySurface(OriginalSurface);
    CHECK(Function(dbc::language::SymbolId::Function_BobX, {BuiltinValue::FromNumber(1)}) == 18);
    CHECK(Function(dbc::language::SymbolId::Function_BobY, {BuiltinValue::FromNumber(1)}) == 5);
    CHECK(Function(dbc::language::SymbolId::Function_BobMirrored, {BuiltinValue::FromNumber(1)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobFlipped, {BuiltinValue::FromNumber(1)}) == 1);
    CHECK(Function(dbc::language::SymbolId::Function_BobOffsetX, {BuiltinValue::FromNumber(1)}) == 2);
    CHECK(Function(dbc::language::SymbolId::Function_BobOffsetY, {BuiltinValue::FromNumber(1)}) == 3);
    CHECK(Function(dbc::language::SymbolId::Function_BobScaleX, {BuiltinValue::FromNumber(1)}) == 150);
    CHECK(Function(dbc::language::SymbolId::Function_BobScaleY, {BuiltinValue::FromNumber(1)}) == 50);

    REQUIRE(Command(dbc::language::SymbolId::Command_PasteBob,
                    {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(19), BuiltinValue::FromNumber(20)}));
    int PersistentBobCount = 0;
    int PersistentSpriteCount = 0;
    Kernel.Sprites().ForEachVisibleSprite(
        [&](const dbc::SpriteData& Value)
        {
            if (Value.ImageId == 21)
            {
                ++PersistentSpriteCount;
                if (Value.X == doctest::Approx(18.0f))
                    ++PersistentBobCount;
            }
        });
    CHECK(PersistentSpriteCount == 4);
    CHECK(PersistentBobCount == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().HideBob(2, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().HideBob(3, &Error); });
    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    Uint8 Red = 0;
    Uint8 Green = 0;
    Uint8 Blue = 0;
    Uint8 Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Capture, 16, 4, &Red, &Green, &Blue, &Alpha));
    CHECK(Red >= 240);
    CHECK(Green >= 240);
    CHECK(Blue <= 16);
    REQUIRE(SDL_ReadSurfacePixel(Capture, 25, 6, &Red, &Green, &Blue, &Alpha));
    CHECK(Red >= 240);
    CHECK(Green <= 16);
    CHECK(Blue <= 16);
    REQUIRE(SDL_ReadSurfacePixel(Capture, 28, 4, &Red, &Green, &Blue, &Alpha));
    CHECK(Red <= 16);
    CHECK(Green <= 16);
    CHECK(Blue >= 240);
    REQUIRE(SDL_ReadSurfacePixel(Capture, 17, 19, &Red, &Green, &Blue, &Alpha));
    CHECK(Red >= 240);
    CHECK(Green >= 240);
    CHECK(Blue <= 16);
    SDL_DestroySurface(Capture);
    int PastedBobCount = 0;
    Kernel.Sprites().ForEachPastedSprite(
        [&](const dbc::SpriteData& Value)
        {
            ++PastedBobCount;
            CHECK(Value.X == doctest::Approx(19.0f));
            CHECK(Value.Y == doctest::Approx(20.0f));
        });
    CHECK(PastedBobCount == 0);

    SDL_Surface* Background = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Background != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Background, nullptr, SDL_MapSurfaceRGBA(Background, 255, 0, 255, 255)));
    Kernel.ImageStoreRef().StoreImageFromSurface(23, Background);
    SDL_DestroySurface(Background);
    Kernel.ImageStoreRef().PasteImage(23, 0, 0, false);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(1, 2, 5, &Error); });
    SDL_Surface* RestoredBackground = Kernel.Renderer().CaptureScreen();
    REQUIRE(RestoredBackground != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(RestoredBackground, 16, 4, &Red, &Green, &Blue, &Alpha));
    CHECK(Red >= 240);
    CHECK(Green <= 16);
    CHECK(Blue >= 240);
    REQUIRE(SDL_ReadSurfacePixel(RestoredBackground, 17, 19, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(RestoredBackground);
    CHECK(Red >= 240);
    CHECK(Green <= 16);
    CHECK(Blue >= 240);

    PastedBobCount = 0;
    Kernel.Sprites().ForEachPastedSprite([&](const dbc::SpriteData&) { ++PastedBobCount; });
    CHECK(PastedBobCount == 0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PasteSprite(1, 19, 20, &Error); });
    Kernel.Sprites().ForEachPastedSprite([&](const dbc::SpriteData&) { ++PastedBobCount; });
    CHECK(PastedBobCount == 1);

    REQUIRE(Command(dbc::language::SymbolId::Command_Cls, {BuiltinValue::FromNumber(0xFFFFFF)}));
    PastedBobCount = 0;
    Kernel.Sprites().ForEachPastedSprite([&](const dbc::SpriteData&) { ++PastedBobCount; });
    CHECK(PastedBobCount == 0);

    SDL_Surface* TransparentBlack = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(TransparentBlack != nullptr);
    REQUIRE(SDL_FillSurfaceRect(TransparentBlack, nullptr, SDL_MapSurfaceRGBA(TransparentBlack, 0, 0, 0, 0)));
    Kernel.ImageStoreRef().StoreImageFromSurface(22, TransparentBlack);
    SDL_DestroySurface(TransparentBlack);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateBob(4, 0, 0, 22, &Error); });
    SDL_Surface* TransparentCapture = Kernel.Renderer().CaptureScreen();
    REQUIRE(TransparentCapture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(TransparentCapture, 0, 0, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(TransparentCapture);
    CHECK(Red >= 240);
    CHECK(Green >= 240);
    CHECK(Blue >= 240);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetBob(4, 1, 0, &Error); });
    SDL_Surface* OpaqueCapture = Kernel.Renderer().CaptureScreen();
    REQUIRE(OpaqueCapture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(OpaqueCapture, 0, 0, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(OpaqueCapture);
    CHECK(Red <= 16);
    CHECK(Green <= 16);
    CHECK(Blue <= 16);

    Kernel.ImageStoreRef().DeleteBitmap(0);
    SDL_Surface* HalfAlpha = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(HalfAlpha != nullptr);
    REQUIRE(SDL_FillSurfaceRect(HalfAlpha, nullptr, SDL_MapSurfaceRGBA(HalfAlpha, 255, 0, 0, 128)));
    Kernel.ImageStoreRef().StoreImageFromSurface(24, HalfAlpha);
    SDL_DestroySurface(HalfAlpha);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateBob(5, 12, 12, 24, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PasteBob(5, 12, 12, &Error); });
    SDL_Surface* HalfAlphaScreen = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(HalfAlphaScreen != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(HalfAlphaScreen, 12, 12, &Red, &Green, &Blue, &Alpha));
    CHECK(Red >= 250);
    CHECK(Green <= 4);
    CHECK(Blue <= 4);
    CHECK(Alpha >= 126);
    CHECK(Alpha <= 129);

    REQUIRE(Command(dbc::language::SymbolId::Command_DeleteBob, {BuiltinValue::FromNumber(1)}));
    CHECK(Function(dbc::language::SymbolId::Function_BobExist, {BuiltinValue::FromNumber(1)}) == 0);
    CHECK(Kernel.Sprites().SpriteExist(1) == 1);
}

TEST_CASE("Classic BOB trails preserve presented order before later bitmap mutations")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-bob-trail-order", 24, 24));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Core().EnsureInitialized());

    SDL_Surface* BottomImage = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* TopImage = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(BottomImage != nullptr);
    REQUIRE(TopImage != nullptr);
    REQUIRE(SDL_FillSurfaceRect(BottomImage, nullptr, SDL_MapSurfaceRGBA(BottomImage, 255, 0, 0, 255)));
    REQUIRE(SDL_FillSurfaceRect(TopImage, nullptr, SDL_MapSurfaceRGBA(TopImage, 0, 255, 0, 255)));
    Kernel.ImageStoreRef().StoreImageFromSurface(31, BottomImage);
    Kernel.ImageStoreRef().StoreImageFromSurface(32, TopImage);
    SDL_DestroySurface(BottomImage);
    SDL_DestroySurface(TopImage);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateBob(10, 4, 4, 31, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateBob(20, 4, 4, 32, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetBob(10, 0, 1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetBob(20, 0, 1, &Error); });
    REQUIRE(Kernel.Core().EnsureInitialized());
    SDL_Surface* Presented = Kernel.Renderer().CaptureScreen();
    REQUIRE(Presented != nullptr);
    SDL_DestroySurface(Presented);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(20, 12, 4, &Error); });
    SDL_Surface* Trail = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(Trail != nullptr);
    Uint8 Red = 0;
    Uint8 Green = 0;
    Uint8 Blue = 0;
    Uint8 Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Trail, 5, 5, &Red, &Green, &Blue, &Alpha));
    CHECK(Red <= 8);
    CHECK(Green >= 247);
    CHECK(Blue <= 8);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(10, 16, 4, &Error); });
    REQUIRE(SDL_ReadSurfacePixel(Trail, 5, 5, &Red, &Green, &Blue, &Alpha));
    CHECK(Red <= 8);
    CHECK(Green >= 247);
    CHECK(Blue <= 8);

    Kernel.ImageStoreRef().DeleteBitmap(0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(10, 4, 4, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(20, 4, 4, &Error); });
    Presented = Kernel.Renderer().CaptureScreen();
    REQUIRE(Presented != nullptr);
    SDL_DestroySurface(Presented);
    Kernel.Draw().DrawBox(4, 4, 5, 5, 0x0000FF, 0x0000FF, 0x0000FF, 0x0000FF);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(20, 12, 4, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(10, 16, 4, &Error); });
    Trail = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(Trail != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Trail, 4, 4, &Red, &Green, &Blue, &Alpha));
    CHECK(Red <= 8);
    CHECK(Green <= 8);
    CHECK(Blue >= 247);
    REQUIRE(SDL_ReadSurfacePixel(Trail, 7, 7, &Red, &Green, &Blue, &Alpha));
    CHECK(Red <= 8);
    CHECK(Green >= 247);
    CHECK(Blue <= 8);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().DeleteBob(10, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().DeleteBob(20, &Error); });
    Kernel.ImageStoreRef().DeleteBitmap(0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateBob(30, 2, 12, 31, &Error); });
    Presented = Kernel.Renderer().CaptureScreen();
    REQUIRE(Presented != nullptr);
    SDL_DestroySurface(Presented);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetBob(30, 0, 1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(30, 8, 12, &Error); });
    Trail = Kernel.ImageStoreRef().GetBitmapSurface(0);
    REQUIRE(Trail != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Trail, 2, 12, &Red, &Green, &Blue, &Alpha));
    CHECK(Red >= 247);
    CHECK(Green <= 8);
    CHECK(Blue <= 8);

    Kernel.ImageStoreRef().DeleteBitmap(0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(30, 2, 12, &Error); });
    Presented = Kernel.Renderer().CaptureScreen();
    REQUIRE(Presented != nullptr);
    SDL_DestroySurface(Presented);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetBob(30, 1, 1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(30, 8, 12, &Error); });
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetBob(30, 0, 1, &Error); });
    Presented = Kernel.Renderer().CaptureScreen();
    REQUIRE(Presented != nullptr);
    SDL_DestroySurface(Presented);
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);
    Kernel.ImageStoreRef().DeleteBitmap(0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(30, 14, 12, &Error); });
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);

    Presented = Kernel.Renderer().CaptureScreen();
    REQUIRE(Presented != nullptr);
    SDL_DestroySurface(Presented);
    dbc::interpreter::ExecutionService Service(Kernel);
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "bob-profile-reset.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.Sources.push_back({Request.RootLogicalPath, "value = 1\n"});
    REQUIRE(Service.Prepare(std::move(Request)));
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionBob(30, 18, 12, &Error); });
    CHECK(Kernel.ImageStoreRef().GetBitmapSurface(0) == nullptr);
}

TEST_CASE("DBPro sprites preserve transient paste atlas frame and render ordering state")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    SDL_Surface* Surface = SDL_CreateSurface(8, 4, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, 255, 255, 255, 255)));
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(1, 10, 20, 101, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSprite(1, 2, 2, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SizeSprite(1, 12, 6, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().OffsetSprite(1, 2, 3, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().RotateSprite(1, 90.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().MoveSprite(1, 5.0f, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteX(Output, 1, &Error); }) == 15);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteY(Output, 1, &Error); }) == 20);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteWidth(Output, 1, &Error); }) == 12);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteHeight(Output, 1, &Error); }) == 6);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteScaleX(Output, 1, &Error); }) == 12);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteScaleY(Output, 1, &Error); }) == 6);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteVisible(Output, 1, &Error); }) == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PasteSprite(1, 30, 31, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteX(Output, 1, &Error); }) == 15);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteY(Output, 1, &Error); }) == 20);
    int Pasted = 0;
    Kernel.Sprites().ForEachPastedSprite(
        [&](const dbc::SpriteData& Value)
        {
            ++Pasted;
            CHECK(Value.X == doctest::Approx(30.0f));
            CHECK(Value.Y == doctest::Approx(31.0f));
        });
    CHECK(Pasted == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CloneSprite(1, 2, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteImage(Output, 2, &Error); }) == 101);
    {
        std::string SpriteError;
        CHECK_FALSE(Kernel.Sprites().CloneSprite(1, 2, &SpriteError));
        CHECK_FALSE(SpriteError.empty());
    };
    Kernel.Sprites().HideAllSprites();
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteVisible(Output, 1, &Error); }) == 0);
    Kernel.Sprites().ShowAllSprites();
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteVisible(Output, 2, &Error); }) == 1);
    Kernel.Sprites().DrawSpritesFirst();
    CHECK(Kernel.Sprites().SpritesDrawFirst());
    Kernel.Sprites().DrawSpritesLast();
    CHECK_FALSE(Kernel.Sprites().SpritesDrawFirst());
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpriteResize(1, 4.0f, 2.0f, &Error); });
    CHECK(Kernel.Sprites().SpriteGlobalOffsetX() == doctest::Approx(4.0f));
    CHECK(Kernel.Sprites().SpriteGlobalScaleX() == doctest::Approx(2.0f));

    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Atlas =
        std::filesystem::temp_directory_path() / ("opendbc-sprite-atlas-" + std::to_string(Suffix) + ".bmp");
    REQUIRE(SDL_SaveBMP(Surface, Atlas.string().c_str()));
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Sprites().CreateAnimatedSprite(3, Atlas.string(), 4, 2, 200, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteWidth(Output, 3, &Error); }) == 2);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteHeight(Output, 3, &Error); }) == 2);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteScaleX(Output, 3, &Error); }) == 2);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteScaleY(Output, 3, &Error); }) == 2);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteFrame(Output, 3, &Error); }) == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpriteFrame(3, 0, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteFrame(Output, 3, &Error); }) == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpriteFrame(3, 8, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteFrame(Output, 3, &Error); }) == 8);
    RequireEngineStatus([&](std::string& Error)
                        { return Kernel.Sprites().SetSpriteTextureCoord(3, 0, 0.25f, 0.5f, &Error); });
    bool FoundAtlas = false;
    Kernel.Sprites().ForEachVisibleSprite(
        [&](const dbc::SpriteData& Value)
        {
            if (Value.ImageId == 200)
            {
                FoundAtlas = true;
                CHECK(Value.TextureCoordinates[0].U == doctest::Approx(0.25f));
                CHECK(Value.TextureCoordinates[0].V == doctest::Approx(0.5f));
            }
        });
    CHECK(FoundAtlas);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PlaySprite(3, 2, 4, 0, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PlaySprite(3, 2, 4, 0, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpriteFrame(3, 4, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteFrame(Output, 3, &Error); }) == 4);
    {
        std::string SpriteError;
        CHECK_FALSE(Kernel.Sprites().SetSpriteFrame(3, 9, &SpriteError));
        CHECK_FALSE(SpriteError.empty());
    };
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(3, 6, 7, 201, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteImage(Output, 3, &Error); }) == 200);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteX(Output, 3, &Error); }) == 6);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteY(Output, 3, &Error); }) == 7);

    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SetSpriteResize(0, 0.0f, 1.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(10, 0, 0, 101, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().CreateSprite(11, 4, 0, 101, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SizeSprite(10, 8, 4, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SizeSprite(11, 8, 4, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteCollision(Output, 10, 11, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteHit(Output, 10, 11, &Error); }) == 1);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteHit(Output, 10, 11, &Error); }) == 0);
    {
        std::string Error;
        float Output = 123.0f;
        CHECK_FALSE(Kernel.Sprites().SpriteAlpha(Output, 99, &Error));
        CHECK_FALSE(Error.empty());
        CHECK(Output == 123.0f);
    };
    {
        std::string Error;
        float Output = 123.0f;
        CHECK_FALSE(Kernel.Sprites().SpriteRed(Output, 99, &Error));
        CHECK_FALSE(Error.empty());
        CHECK(Output == 123.0f);
    };
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionSprite(11, 20, 0, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteHit(Output, 10, 11, &Error); }) == 0);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().SizeSprite(11, 1, 1, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionSprite(11, -3, 4, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().RotateSprite(10, 90.0f, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteCollision(Output, 10, 11, &Error); }) == 1);
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().RotateSprite(10, 0.0f, &Error); });
    RequireEngineStatus([&](std::string& Error) { return Kernel.Sprites().PositionSprite(11, 4, 0, &Error); });
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Sprites().SpriteHit(Output, 10, 11, &Error); }) == 1);

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_Cls, {BuiltinValue::FromNumber(0)}, Error));
    Pasted = 0;
    Kernel.Sprites().ForEachPastedSprite([&](const dbc::SpriteData&) { ++Pasted; });
    CHECK(Pasted == 0);

    SDL_DestroySurface(Surface);
    std::error_code RemoveError;
    std::filesystem::remove(Atlas, RemoveError);
}

TEST_CASE("LOAD ARRAY rejects a truncated record without changing the destination")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Path =
        std::filesystem::temp_directory_path() / ("opendbc-truncated-array-" + std::to_string(Suffix) + ".bin");
    {
        std::ofstream Output(Path, std::ios::binary);
        const char Bytes[] = {1, 2, 3};
        Output.write(Bytes, sizeof(Bytes));
    }

    dbc::EngineKernel Kernel;
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    EngineBuiltinHost Host(Context);

    auto Array = std::make_shared<dbc::vm::ArrayValue>();
    Array->ElementType = {dbc::language::TypeKind::Integer, {}};
    Array->Dimensions = {2};
    Array->Elements = {dbc::vm::Value::Integer(10), dbc::vm::Value::Integer(20)};
    const dbc::vm::Value Destination = dbc::vm::Value::Array(Array, Array->ElementType);
    const std::vector<dbc::vm::Value> Arguments{dbc::vm::Value::String(Path.string())};
    const dbc::vm::BuiltinResult Result = Host.Invoke({77,
                                                       dbc::language::SymbolId::Command_LoadArray,
                                                       Arguments,
                                                       {dbc::language::TypeKind::Void, {}},
                                                       false,
                                                       false,
                                                       &Destination,
                                                       true});

    std::error_code Ignored;
    std::filesystem::remove(Path, Ignored);
    CHECK(Result.State == dbc::vm::BuiltinState::Failed);
    CHECK(Result.Error.find("truncated record") != std::string::npos);
    CHECK(Array->Elements[0].AsInteger() == 10);
    CHECK(Array->Elements[1].AsInteger() == 20);
}

TEST_CASE("Asset load failures become runtime errors and leave no resource slot")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 1, 1));
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = std::filesystem::temp_directory_path();
    std::string Error;

    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadImage,
                              {BuiltinValue::FromString("opendbc-missing-image.png"), BuiltinValue::FromNumber(91)},
                              Error));
    CHECK(Error.find("LOAD IMAGE failed") != std::string::npos);
    CHECK(Kernel.ImageStoreRef().ImageExist(91) == 0);

    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadBitmap,
                              {BuiltinValue::FromString("opendbc-missing-bitmap.png"), BuiltinValue::FromNumber(92)},
                              Error));
    CHECK(Error.find("LOAD BITMAP failed") != std::string::npos);
    CHECK(Kernel.ImageStoreRef().BitmapExist(92) == 0);
}

TEST_CASE("Classic restored object commands preserve typed state and historical paths")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-restored-objects", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    Context.AssetRoot = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic";
    std::string Error;

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadObject,
                          {BuiltinValue::FromString("obj\\crawleridle.x"), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_AppendObject,
        {BuiltinValue::FromString("obj/crawlerwalk.x"), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(100)},
        Error));
    CHECK(Kernel.Objects().TotalObjectFrames(1) >= 100);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectCone,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(20)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectCylinder,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(12)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectTriangle,
                          {BuiltinValue::FromNumber(4), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0),
                           BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(3),
                           BuiltinValue::FromNumber(0)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectPlain,
                          {BuiltinValue::FromNumber(5), BuiltinValue::FromNumber(8), BuiltinValue::FromNumber(6)},
                          Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeObjectBox,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(4),
                           BuiltinValue::FromNumber(6)},
                          Error));
    REQUIRE(Kernel.Objects().GetObject(5) != nullptr);
    const dbc::Object3D* Plain = Kernel.Objects().GetObject(5);
    CHECK(Plain->RotY == doctest::Approx(0.0f));
    REQUIRE_FALSE(Plain->Meshes.empty());
    const dbc::Mesh& PlainMesh = Plain->Meshes.front();
    REQUIRE(PlainMesh.Geometry().Indices.size() >= 3);
    const dbc::Vec3 PlainNormal =
        dbc::Vec3Cross(dbc::Vec3Sub(PlainMesh.Geometry().Vertices[PlainMesh.Geometry().Indices[1]],
                                    PlainMesh.Geometry().Vertices[PlainMesh.Geometry().Indices[0]]),
                       dbc::Vec3Sub(PlainMesh.Geometry().Vertices[PlainMesh.Geometry().Indices[2]],
                                    PlainMesh.Geometry().Vertices[PlainMesh.Geometry().Indices[0]]));
    CHECK(PlainNormal.Z < 0.0f);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_TurnObjectLeft,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(10)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_TurnObjectRight,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(25)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PitchObjectUp,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(5)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PitchObjectDown,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(12)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RollObjectLeft,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(20)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_RollObjectRight,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(25)}, Error));
    const dbc::Object3D* Box = Kernel.Objects().GetObject(6);
    REQUIRE(Box != nullptr);
    CHECK(Box->RotX == doctest::Approx(7.0f));
    CHECK(Box->RotY == doctest::Approx(15.0f));
    CHECK(Box->RotZ == doctest::Approx(5.0f));

    BuiltinValue SizeResult;
    for (const auto& [Symbol, Expected] : {
             std::pair{dbc::language::SymbolId::Function_ObjectSize, 1.5},
             std::pair{dbc::language::SymbolId::Function_ObjectSizeX, 2.0},
             std::pair{dbc::language::SymbolId::Function_ObjectSizeY, 4.0},
             std::pair{dbc::language::SymbolId::Function_ObjectSizeZ, 6.0},
         })
    {
        REQUIRE(InvokeFunction(Context, Symbol, {BuiltinValue::FromNumber(6)}, SizeResult, Error));
        CHECK(SizeResult.AsNumber() == doctest::Approx(Expected));
    }

    REQUIRE(Kernel.ImageStoreRef().LoadImageFile((Context.AssetRoot / "bmp/stone.bmp").string(), 20));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_TextureLimb,
                          {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(20)},
                          Error));
    BuiltinValue LimbTexture;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_LimbTexture,
                           {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(0)}, LimbTexture, Error));
    CHECK(ReadBuiltinInteger(LimbTexture) == 20);
    BuiltinValue LimbTextureName;
    REQUIRE(InvokeFunction(Context, dbc::language::SymbolId::Function_LimbTextureName,
                           {BuiltinValue::FromNumber(6), BuiltinValue::FromNumber(0)}, LimbTextureName, Error));
    CHECK(std::filesystem::path(LimbTextureName.AsString()).filename() == "stone.bmp");

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_XrotateObject,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(10)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_YrotateObject,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(20)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ZrotateObject,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(30)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MoveObject,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(5)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PointObject,
                          {BuiltinValue::FromNumber(3), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(2),
                           BuiltinValue::FromNumber(3)},
                          Error));
    REQUIRE(Kernel.Objects().GetObject(2) != nullptr);
    Kernel.Objects().GetObject(2)->TransformDirty = false;
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollObjectTexture,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0.25), BuiltinValue::FromNumber(-0.5)},
                          Error));
    CHECK(Kernel.Objects().GetObject(2)->TransformDirty);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollObjectTexture,
                          {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0.5), BuiltinValue::FromNumber(0.25)},
                          Error));

    const dbc::Object3D* Cone = Kernel.Objects().GetObject(2);
    REQUIRE(Cone != nullptr);
    CHECK(Cone->RotX == doctest::Approx(10.0f));
    CHECK(Cone->RotY == doctest::Approx(20.0f));
    CHECK(Cone->RotZ == doctest::Approx(30.0f));
    CHECK(Cone->TexScrollU == doctest::Approx(0.75f));
    CHECK(Cone->TexScrollV == doctest::Approx(-0.25f));

    Context.Checklist = {{"entry", 11, 22, 33, 44}};
    for (const auto& [Symbol, Expected] : {
             std::pair{dbc::language::SymbolId::Function_ChecklistValueA, 11},
             std::pair{dbc::language::SymbolId::Function_ChecklistValueB, 22},
             std::pair{dbc::language::SymbolId::Function_ChecklistValueC, 33},
             std::pair{dbc::language::SymbolId::Function_ChecklistValueD, 44},
         })
    {
        BuiltinValue Result;
        REQUIRE(InvokeFunction(Context, Symbol, {BuiltinValue::FromNumber(1)}, Result, Error));
        CHECK(ReadBuiltinInteger(Result) == Expected);
    }

    Error.clear();
    CHECK_FALSE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_AppendObject,
        {BuiltinValue::FromString("obj/crawlerwalk.x"), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(-1)},
        Error));
    CHECK(Error.find("start frame cannot be negative") != std::string::npos);
    Error.clear();
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_ScrollObjectTexture,
                              {BuiltinValue::FromNumber(99), BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)},
                              Error));
    CHECK(Error.find("requires existing object 99") != std::string::npos);
}

TEST_CASE("Music fallback is limited to same-stem MIDI alternatives")
{
    std::string AudioError;
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-midi-fallback-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));
    const std::filesystem::path Source =
        std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/music/music/hero.mp3";
    REQUIRE(std::filesystem::copy_file(Source, Root / "theme.mp3"));
    REQUIRE(std::filesystem::copy_file(Source, Root / "not-midi.mp3"));
    {
        std::ofstream InvalidMidi(Root / "theme.mid", std::ios::binary);
        InvalidMidi << "not a midi file";
        std::ofstream InvalidOgg(Root / "not-midi.ogg", std::ios::binary);
        InvalidOgg << "not an ogg file";
    }

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("midi-fallback", 16, 16));
    REQUIRE(Kernel.Audio().LoadMusic((Root / "theme.mid").string(), 1, &AudioError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicExist(Output, 1, &Error); }) == 1);
    CHECK_FALSE(Kernel.Audio().LoadMusic((Root / "not-midi.ogg").string(), 2, &AudioError));
    CHECK(AudioError.find("LOAD MUSIC failed") != std::string::npos);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Audio().MusicExist(Output, 2, &Error); }) == 0);

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Classic gamma and mipmap modes drive final pixels and sampler state")
{
    const dbc::language::SymbolDescriptor& GammaDescriptor =
        dbc::language::Symbol(dbc::language::SymbolId::Command_SetGamma);
    CHECK(dbc::language::SupportFor(GammaDescriptor, dbc::language::LanguageProfile::Dbc) ==
          dbc::language::SupportStatus::Implemented);
    CHECK(dbc::language::SupportFor(GammaDescriptor, dbc::language::LanguageProfile::DbPro) ==
          dbc::language::SupportStatus::Unsupported);
    CHECK(dbc::language::StubPolicyFor(GammaDescriptor, dbc::language::LanguageProfile::DbPro) ==
          dbc::language::CompatibilityStubPolicy::None);

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-final-color", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Core().EnsureInitialized());

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::Dbc;
    std::string Error;
    const auto Command = [&](dbc::language::SymbolId Symbol, std::initializer_list<BuiltinValue> Arguments)
    {
        Error.clear();
        return InvokeCommand(Context, Symbol, std::vector<BuiltinValue>(Arguments), Error);
    };

    REQUIRE(Command(dbc::language::SymbolId::Command_Ink,
                    {BuiltinValue::FromNumber(0xFFFFFF), BuiltinValue::FromNumber(0)}));
    REQUIRE(
        Command(dbc::language::SymbolId::Command_Box, {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0),
                                                       BuiltinValue::FromNumber(Kernel.Core().WindowWidth() - 1),
                                                       BuiltinValue::FromNumber(Kernel.Core().WindowHeight() - 1)}));

    const auto CaptureCenter = [&]()
    {
        SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        std::array<Uint8, 4> Pixel{};
        REQUIRE(
            SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Pixel[0], &Pixel[1], &Pixel[2], &Pixel[3]));
        SDL_DestroySurface(Capture);
        return Pixel;
    };

    const std::array<Uint8, 4> NeutralPixel = CaptureCenter();
    CHECK(NeutralPixel[0] >= 240);
    CHECK(NeutralPixel[1] >= 240);
    CHECK(NeutralPixel[2] >= 240);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetGamma,
                    {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(255), BuiltinValue::FromNumber(0)}));
    CHECK(Kernel.Renderer().GammaLevels() == std::array<int, 3>{0, 255, 0});

    const std::array<Uint8, 4> Pixel = CaptureCenter();
    CHECK(Pixel[0] <= 8);
    CHECK(Pixel[1] >= 240);
    CHECK(Pixel[2] <= 8);

    REQUIRE(Command(dbc::language::SymbolId::Command_SetGamma,
                    {BuiltinValue::FromNumber(-20), BuiltinValue::FromNumber(255), BuiltinValue::FromNumber(900)}));
    CHECK(Kernel.Renderer().GammaLevels() == std::array<int, 3>{0, 255, 511});
    const std::array<float, 3> Scale = Kernel.Renderer().FinalColorScale();
    CHECK(Scale[0] == doctest::Approx(0.0f));
    CHECK(Scale[1] == doctest::Approx(1.0f));
    CHECK(Scale[2] == doctest::Approx(511.0f / 255.0f));

    REQUIRE(Command(dbc::language::SymbolId::Command_SetMipmapMode, {BuiltinValue::FromNumber(0)}));
    CHECK(Kernel.Renderer().HasMipmapModeOverride());
    CHECK(Kernel.Renderer().ResolveMipmapMode(2) == -1);
    REQUIRE(Kernel.Gpu().SurfaceSampler(0, true, -1) != nullptr);
    REQUIRE(Kernel.Gpu().SurfaceSampler(0, true, 1) != nullptr);
    REQUIRE(Kernel.Gpu().SurfaceSampler(0, true, 2) != nullptr);
    CHECK(Kernel.Gpu().SurfaceSampler(0, true, -1) != Kernel.Gpu().SurfaceSampler(0, true, 1));
    CHECK(Kernel.Gpu().SurfaceSampler(0, true, 1) != Kernel.Gpu().SurfaceSampler(0, true, 2));
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMipmapMode, {BuiltinValue::FromNumber(1)}));
    CHECK(Kernel.Renderer().ResolveMipmapMode(2) == 1);
    REQUIRE(Command(dbc::language::SymbolId::Command_SetMipmapMode, {BuiltinValue::FromNumber(2)}));
    CHECK(Kernel.Renderer().ResolveMipmapMode(1) == 2);
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetMipmapMode, {BuiltinValue::FromNumber(3)}));
    CHECK(Error.find("requires a value from 0 to 2") != std::string::npos);

    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    CHECK(Kernel.Renderer().GammaLevels() == std::array<int, 3>{255, 255, 255});
    CHECK_FALSE(Kernel.Renderer().HasMipmapModeOverride());
    CHECK(Kernel.Renderer().ResolveMipmapMode(1) == 1);
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    CHECK_FALSE(Command(dbc::language::SymbolId::Command_SetGamma,
                        {BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0), BuiltinValue::FromNumber(0)}));
    CHECK(Error == "Unsupported command: SET GAMMA");
    CHECK(Kernel.Renderer().GammaLevels() == std::array<int, 3>{255, 255, 255});
}

TEST_CASE("Classic portable static world, line of sight, and global collision have observable state")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-static-world-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-static-world", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    Kernel.Objects().MakeObjectCube(1, 10.0f);
    Kernel.Objects().MakeObjectCube(2, 10.0f);
    REQUIRE(Kernel.Objects().SetObjectCollisionOff(1));
    REQUIRE(Kernel.Objects().SetObjectCollisionOff(2));
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);
    Kernel.Objects().SetGlobalCollision(true);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 1);
    Kernel.Objects().SetGlobalCollision(false);
    CHECK(Kernel.Objects().ObjectCollision(1, 2) == 0);

    REQUIRE(Kernel.Objects().MakeStaticCollisionBox(20.0f, -5.0f, -5.0f, 30.0f, 5.0f, 5.0f));
    CHECK(ReadEngineNumber<float>(
              [&](std::string& Error, float& Output)
              {
                  return Kernel.Objects().StaticLineOfSight(Output, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 2.0f, 1.0f,
                                                            &Error);
              }) == 1.0f);
    CHECK(Kernel.Objects().StaticLineOfSightX() == doctest::Approx(18.0f));
    CHECK(ReadEngineNumber<float>(
              [&](std::string& Error, float& Output)
              {
                  return Kernel.Objects().StaticLineOfSight(Output, 0.0f, 20.0f, 0.0f, 100.0f, 20.0f, 0.0f, 0.0f, 1.0f,
                                                            &Error);
              }) == 0.0f);

    Kernel.Objects().PositionObject(1, 40.0f, 0.0f, 0.0f);
    REQUIRE(Kernel.Objects().MakeStaticObject(1));
    CHECK(ReadEngineNumber<float>(
              [&](std::string& Error, float& Output)
              {
                  return Kernel.Objects().StaticLineOfSight(Output, 31.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                                            &Error);
              }) == 1.0f);
    CHECK(Kernel.Objects().StaticLineOfSightX() == doctest::Approx(35.0f));
    REQUIRE(Kernel.Objects().AttachObjectToStatic(2));
    CHECK(Kernel.Objects().IsStaticRenderObject(2));
    Kernel.Objects().SetStaticObjectsWireframe(true);
    CHECK(Kernel.Objects().GetObject(2)->Wireframe);
    REQUIRE(Kernel.Objects().SetStaticObjectsTexture(2));
    Kernel.Objects().EnableStaticOcclusion(false);
    CHECK_FALSE(Kernel.Objects().StaticOcclusionEnabled());
    Kernel.Objects().EnableStaticOcclusion(true);
    CHECK(Kernel.Objects().StaticOcclusionEnabled());
    REQUIRE(Kernel.Objects().DetachObjectFromStatic(2));
    CHECK_FALSE(Kernel.Objects().IsStaticRenderObject(2));

    const std::filesystem::path Archive = Root / "world.odbstatic";
    REQUIRE(Kernel.Objects().SaveStaticObjects(Archive.string()));
    Kernel.Objects().DeleteStaticObjects();
    int StaticCount = 0;
    Kernel.Objects().ForEachObject([&](int Id, const dbc::Object3D&) { StaticCount += Id < 0 ? 1 : 0; });
    CHECK(StaticCount == 0);
    REQUIRE(Kernel.Objects().LoadStaticObjects(Archive.string()));
    Kernel.Objects().ForEachObject(
        [&](int Id, const dbc::Object3D& Object)
        {
            if (Id < 0)
            {
                ++StaticCount;
                CHECK(Object.Wireframe);
                REQUIRE_FALSE(Object.Meshes.empty());
                CHECK(Object.Meshes.front().TextureStageAddressModes.at(0) == 2);
            }
        });
    CHECK(StaticCount == 1);
    CHECK(ReadEngineNumber<float>(
              [&](std::string& Error, float& Output)
              {
                  return Kernel.Objects().StaticLineOfSight(Output, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                                            &Error);
              }) == 1.0f);
    CHECK(ReadEngineNumber<float>(
              [&](std::string& Error, float& Output)
              {
                  return Kernel.Objects().StaticLineOfSight(Output, 31.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                                            &Error);
              }) == 1.0f);
    CHECK(Kernel.Objects().StaticLineOfSightX() == doctest::Approx(35.0f));

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Classic authored object keyframes round-trip through the portable animation format")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-object-animation-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-object-keyframes", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    Kernel.Objects().MakeObjectCube(1, 10.0f);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(1, 1));
    REQUIRE(Kernel.Objects().AddLimb(1, 1, 1));
    REQUIRE(Kernel.Objects().LinkLimb(1, 0, 1));
    const std::filesystem::path Model = Root / "cube.dbo";
    REQUIRE(Kernel.Objects().SaveObject(Model.string(), 1));
    REQUIRE(Kernel.Objects().ClearAllObjectKeyframes(1));
    REQUIRE(Kernel.Objects().RotateLimb(1, 0, 0.0f, 0.0f, 0.0f));
    REQUIRE(Kernel.Objects().OffsetLimb(1, 1, 0.0f, 0.0f, 0.0f));
    REQUIRE(Kernel.Objects().ScaleLimb(1, 1, 100.0f, 100.0f, 100.0f));
    REQUIRE(Kernel.Objects().HideLimb(1, 1));
    REQUIRE(Kernel.Objects().SetObjectKeyframe(1, 0));
    REQUIRE(Kernel.Objects().RotateLimb(1, 0, 0.0f, 0.0f, 90.0f));
    REQUIRE(Kernel.Objects().OffsetLimb(1, 1, 10.0f, -4.0f, 6.0f));
    REQUIRE(Kernel.Objects().ScaleLimb(1, 1, 200.0f, 50.0f, 150.0f));
    REQUIRE(Kernel.Objects().ShowLimb(1, 1));
    REQUIRE(Kernel.Objects().SetObjectKeyframe(1, 10));

    const std::filesystem::path Animation = Root / "wave.odbanim";
    REQUIRE(Kernel.Objects().SaveObjectAnimation(Animation.string(), 1));
    REQUIRE(Kernel.Objects().ClearAllObjectKeyframes(1));
    REQUIRE(Kernel.Objects().AppendObjectAnimation(Model.string(), Animation.string(), 1, 20));
    CHECK(Kernel.Objects().TotalObjectFrames(1) == 30);
    REQUIRE(Kernel.Objects().SetObjectFrame(1, 25.0f));
    REQUIRE(Kernel.Objects().GetObject(1) != nullptr);
    REQUIRE(Kernel.Objects().GetObject(1)->LimbTransforms.size() == 2);
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[0].RotZ == doctest::Approx(45.0f));
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[1].OffX == doctest::Approx(5.0f));
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[1].OffY == doctest::Approx(-2.0f));
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[1].OffZ == doctest::Approx(3.0f));
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[1].ScaleX == doctest::Approx(1.5f));
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[1].ScaleY == doctest::Approx(0.75f));
    CHECK(Kernel.Objects().GetObject(1)->LimbTransforms[1].ScaleZ == doctest::Approx(1.25f));
    CHECK(Kernel.Objects().LimbVisible(1, 1) == 1);
    const dbc::Vec3 InterpolatedOrigin = Kernel.Renderer().WorldLimbOrigin(*Kernel.Objects().GetObject(1), 1);
    const dbc::Vec3 ExpectedOrigin = dbc::RotateZ({5.0f, -2.0f, 3.0f}, 45.0f);
    CHECK(InterpolatedOrigin.X == doctest::Approx(ExpectedOrigin.X).epsilon(0.001));
    CHECK(InterpolatedOrigin.Y == doctest::Approx(ExpectedOrigin.Y).epsilon(0.001));
    CHECK(InterpolatedOrigin.Z == doctest::Approx(ExpectedOrigin.Z).epsilon(0.001));
    REQUIRE(Kernel.Objects().SetObjectFrame(1, 20.0f));
    CHECK(Kernel.Objects().LimbVisible(1, 1) == 0);
    REQUIRE(Kernel.Objects().SetObjectFrame(1, 30.0f));
    CHECK(Kernel.Objects().LimbVisible(1, 1) == 1);

    Kernel.Objects().MakeObjectCube(2, 12.0f);
    REQUIRE(Kernel.Objects().MakeMeshFromObject(2, 2));
    REQUIRE(Kernel.Objects().AddLimb(2, 1, 2));
    const std::filesystem::path WrongModel = Root / "wrong-cube.dbo";
    REQUIRE(Kernel.Objects().SaveObject(WrongModel.string(), 2));
    {
        std::string Error;
        CHECK_FALSE(Kernel.Objects().AppendObjectAnimation(WrongModel.string(), Animation.string(), 1, 40, &Error));
        CHECK(Error.find("model structure does not match") != std::string::npos);
    };

    const std::filesystem::path CyclicAnimation = Root / "cyclic.odbanim";
    std::filesystem::copy_file(Animation, CyclicAnimation);
    {
        std::fstream Corrupt(CyclicAnimation, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(Corrupt.good());
        const std::array<unsigned char, 4> ParentOne{1, 0, 0, 0};
        Corrupt.seekp(61);
        Corrupt.write(reinterpret_cast<const char*>(ParentOne.data()), ParentOne.size());
        REQUIRE(Corrupt.good());
    }
    {
        std::string Error;
        CHECK_FALSE(Kernel.Objects().AppendObjectAnimation(Model.string(), CyclicAnimation.string(), 1, 40, &Error));
        CHECK(Error.find("hierarchy cycle") != std::string::npos);
    };

    const std::filesystem::path SelfParentedAnimation = Root / "self-parented.odbanim";
    std::filesystem::copy_file(Animation, SelfParentedAnimation);
    {
        std::fstream Corrupt(SelfParentedAnimation, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(Corrupt.good());
        const std::array<unsigned char, 4> ParentOne{1, 0, 0, 0};
        Corrupt.seekp(102);
        Corrupt.write(reinterpret_cast<const char*>(ParentOne.data()), ParentOne.size());
        REQUIRE(Corrupt.good());
    }
    {
        std::string Error;
        CHECK_FALSE(
            Kernel.Objects().AppendObjectAnimation(Model.string(), SelfParentedAnimation.string(), 1, 40, &Error));
        CHECK(Error.find("self-parented limb") != std::string::npos);
    };

    dbc::Object3D* MutableObject = Kernel.Objects().GetObject(1);
    REQUIRE(MutableObject != nullptr);
    MutableObject->ObjectKeyframes[30].Limbs[0].ScaleX = std::numeric_limits<float>::quiet_NaN();
    {
        std::string Error;
        CHECK_FALSE(Kernel.Objects().SaveObjectAnimation((Root / "non-finite.odbanim").string(), 1, &Error));
        CHECK(Error.find("non-finite limb transform") != std::string::npos);
    };
    MutableObject->ObjectKeyframes[30].Limbs[0].ScaleX = 1.0f;

    REQUIRE(Kernel.Objects().ClearObjectKeyframe(1, 30));
    CHECK(Kernel.Objects().TotalObjectFrames(1) == 20);

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Cloned imported objects keep their animation after the source is deleted")
{
    const std::filesystem::path Model = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models/walk.x";
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("cloned-imported-animation", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    REQUIRE(Kernel.Objects().LoadObject(Model.string(), 1));
    REQUIRE(Kernel.Objects().TotalObjectFrames(1) > 1);
    Kernel.Objects().CloneObject(2, 1);
    Kernel.Objects().DeleteObject(1);
    const dbc::Object3D* Clone = Kernel.Objects().GetObject(2);
    REQUIRE(Clone != nullptr);
    REQUIRE(Clone->AssimpScene != nullptr);
    CHECK(Clone->LimbNodes.size() > 1);
    REQUIRE(Kernel.Objects().LoopObject(2));
    CHECK(Kernel.Objects().ObjectFrame(2) == 0);
    Kernel.Renderer().PrepareFrameSimulation(0.5);
    CHECK(Kernel.Objects().ObjectPlaying(2) == 1);
    CHECK(Kernel.Objects().ObjectFrame(2) > 0);
}

TEST_CASE("Classic imported object animation saves every moving frame and appends losslessly")
{
    const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Root =
        std::filesystem::temp_directory_path() / ("opendbc-imported-animation-" + std::to_string(Suffix));
    REQUIRE(std::filesystem::create_directory(Root));
    const std::filesystem::path Model = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic/models/walk.x";
    REQUIRE(std::filesystem::is_regular_file(Model));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("classic-imported-animation", 32, 32));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Objects().LoadObject(Model.string(), 1));
    const dbc::Object3D* Source = Kernel.Objects().GetObject(1);
    REQUIRE(Source != nullptr);
    const int SourceLastFrame = Kernel.Objects().TotalObjectFrames(1);
    REQUIRE(Source->AssimpScene != nullptr);
    REQUIRE(Source->AssimpScene->mNumAnimations > 0);
    REQUIRE(Source->AssimpScene->mAnimations[0] != nullptr);
    const aiAnimation* SourceAnimation = Source->AssimpScene->mAnimations[0];
    const double SourceTicksPerSecond =
        SourceAnimation->mTicksPerSecond > 0.0 ? SourceAnimation->mTicksPerSecond : 25.0;
    CHECK(SourceLastFrame == static_cast<int>(std::lround(SourceAnimation->mDuration * 25.0 / SourceTicksPerSecond)));
    REQUIRE(SourceLastFrame > 1);
    REQUIRE(Source->LimbNodes.size() > 1);

    REQUIRE(Kernel.Objects().SetObjectFrame(1, 0));
    std::vector<dbc::Vec3> BindOrigins;
    BindOrigins.reserve(Source->LimbNodes.size());
    for (std::size_t Limb = 0; Limb < Source->LimbNodes.size(); ++Limb)
        BindOrigins.push_back(Kernel.Renderer().WorldLimbOrigin(*Source, static_cast<int>(Limb)));

    int MovingFrame = -1;
    int MovingLimb = -1;
    dbc::Vec3 ImportedPosition{};
    for (int Frame = 1; Frame <= SourceLastFrame && MovingFrame < 0; ++Frame)
    {
        REQUIRE(Kernel.Objects().SetObjectFrame(1, Frame));
        for (std::size_t Limb = 1; Limb < Source->LimbNodes.size(); ++Limb)
        {
            const dbc::Vec3 Position = Kernel.Renderer().WorldLimbOrigin(*Source, static_cast<int>(Limb));
            const dbc::Vec3 Delta = dbc::Vec3Sub(Position, BindOrigins[Limb]);
            if (dbc::Vec3Dot(Delta, Delta) > 0.0001f)
            {
                MovingFrame = Frame;
                MovingLimb = static_cast<int>(Limb);
                ImportedPosition = Position;
                break;
            }
        }
    }
    REQUIRE(MovingFrame > 0);
    REQUIRE(MovingLimb > 0);

    const std::filesystem::path Animation = Root / "walk.odbanim";
    REQUIRE(Kernel.Objects().SaveObjectAnimation(Animation.string(), 1));
    REQUIRE(Kernel.Objects().LoadObject(Model.string(), 2));
    REQUIRE(Kernel.Objects().ClearAllObjectKeyframes(2));
    constexpr int AppendStart = 40;
    REQUIRE(Kernel.Objects().AppendObjectAnimation(Model.string(), Animation.string(), 2, AppendStart));

    const dbc::Object3D* Appended = Kernel.Objects().GetObject(2);
    REQUIRE(Appended != nullptr);
    REQUIRE(Appended->ObjectKeyframes.size() > 2);
    CHECK(Appended->ObjectKeyframes.begin()->first == AppendStart);
    CHECK(Appended->ObjectKeyframes.rbegin()->first == AppendStart + SourceLastFrame);
    CHECK(Kernel.Objects().TotalObjectFrames(2) == AppendStart + SourceLastFrame);

    REQUIRE(Kernel.Objects().SetObjectFrame(2, AppendStart));
    const dbc::Vec3 AppendedBind = Kernel.Renderer().WorldLimbOrigin(*Appended, MovingLimb);
    REQUIRE(Kernel.Objects().SetObjectFrame(2, AppendStart + MovingFrame));
    const dbc::Vec3 AppendedPosition = Kernel.Renderer().WorldLimbOrigin(*Appended, MovingLimb);
    const dbc::Vec3 AppendedDelta = dbc::Vec3Sub(AppendedPosition, AppendedBind);
    CHECK(dbc::Vec3Dot(AppendedDelta, AppendedDelta) > 0.0001f);
    CHECK(std::abs(AppendedPosition.X - ImportedPosition.X) < 0.01f);
    CHECK(std::abs(AppendedPosition.Y - ImportedPosition.Y) < 0.01f);
    CHECK(std::abs(AppendedPosition.Z - ImportedPosition.Z) < 0.01f);

    std::error_code CleanupError;
    std::filesystem::remove_all(Root, CleanupError);
}

TEST_CASE("Checked engine failures preserve outputs and avoid invalid resource publication")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("checked-errors", 16, 16));
    std::string Error;
    REQUIRE(Kernel.Sprites().CreateSprite(1, 0, 0, 1, &Error));
    REQUIRE(Kernel.Sprites().MoveSprite(1, std::numeric_limits<float>::max(), &Error));
    int Coordinate = 77;
    CHECK_FALSE(Kernel.Sprites().SpriteY(Coordinate, 1, &Error));
    CHECK(Coordinate == 77);
    CHECK_FALSE(Error.empty());
    CHECK_FALSE(Kernel.Sprites().CreateAnimatedSprite(2, "unused.bmp", 65536, 65536, 1, &Error));
    CHECK(Kernel.Sprites().SpriteExist(2) == 0);
    CHECK_FALSE(Kernel.Memblocks().ChangeMesh(1, -1, 1, &Error));
    CHECK(Error.find("limb number") != std::string::npos);

    REQUIRE(Kernel.Matrices().MakeMatrix(1, 10.0f, 10.0f, 1, 1, true, &Error));
    REQUIRE(Kernel.Matrices().FillMatrix(1, 3.0f, 1, &Error));
    CHECK_FALSE(Kernel.Matrices().RandomizeMatrix(1, 2147483648.0f, &Error));
    float Height = 77.0f;
    REQUIRE(Kernel.Matrices().GetMatrixHeight(Height, 1, 0, 0, &Error));
    CHECK(Height == 3.0f);
    CHECK_FALSE(Kernel.Matrices().GetGroundHeight(Height, 1, std::numeric_limits<float>::quiet_NaN(), 0.0f, &Error));
    CHECK(Height == 3.0f);
    CHECK_FALSE(Kernel.Matrices().FillMatrix(1, std::numeric_limits<float>::infinity(), 1, &Error));
    REQUIRE(Kernel.Matrices().RandomizeMatrix(1, -std::numeric_limits<float>::max(), &Error));
    REQUIRE(Kernel.Matrices().GetMatrixHeight(Height, 1, 0, 0, &Error));
    CHECK(Height == 0.0f);

    std::vector<std::uint8_t> Samples(29, 0);
    const std::uint32_t WideFormat = 65537;
    std::memcpy(Samples.data(), &WideFormat, sizeof(WideFormat));
    CHECK_FALSE(Kernel.Audio().MakeSoundFromMemblock(1, Samples, &Error));
    CHECK(Error.find("out-of-range WAVEFORMATEX field") != std::string::npos);
}

TEST_CASE("Binary float I/O rejects non-finite values and preserves failed read outputs")
{
    const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto Path = std::filesystem::temp_directory_path() / ("opendbc-float-io-" + std::to_string(Stamp));
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("float-io", 16, 16));
    std::string Error;
    REQUIRE(Kernel.FileIO().OpenToWrite(1, Path.string(), &Error));
    CHECK_FALSE(Kernel.FileIO().FileWriteFloat(1, std::numeric_limits<float>::infinity(), &Error));
    REQUIRE(Kernel.FileIO().FileWriteFloat(1, 1.25f, &Error));
    REQUIRE(Kernel.FileIO().FileWriteLong(1, 0x7f800000, &Error));
    REQUIRE(Kernel.FileIO().FileWriteByte(1, 7, &Error));
    REQUIRE(Kernel.FileIO().CloseFile(1, &Error));
    CHECK(std::filesystem::file_size(Path) == 9);
    REQUIRE(Kernel.FileIO().OpenToRead(1, Path.string(), &Error));
    float Value = 9.0f;
    REQUIRE(Kernel.FileIO().FileReadFloat(Value, 1, &Error));
    CHECK(Value == 1.25f);
    CHECK_FALSE(Kernel.FileIO().FileReadFloat(Value, 1, &Error));
    CHECK(Value == 1.25f);
    CHECK_FALSE(Kernel.FileIO().FileReadFloat(Value, 1, &Error));
    CHECK(Value == 1.25f);
    REQUIRE(Kernel.FileIO().CloseFile(1, &Error));
    std::error_code CleanupError;
    CHECK(std::filesystem::remove(Path, CleanupError));
}

TEST_CASE("Optional engine diagnostics preserve failure status and output values")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("optional-diagnostics", 16, 16));
    CHECK_FALSE(Kernel.FileIO().FileWriteByte(0, 42));
    CHECK_FALSE(Kernel.FileIO().FileWriteByte(1, 42, nullptr));
    int Value = 123;
    CHECK_FALSE(Kernel.FileIO().FileReadByte(Value, 1));
    CHECK(Value == 123);
    std::string Error;
    CHECK_FALSE(Kernel.FileIO().FileWriteByte(1, 42, &Error));
    CHECK(Error == "File 1 is not open");
    CHECK_FALSE(dbc::FileWriteByte(Kernel, 1, 42));
    CHECK_FALSE(Kernel.ImageStoreRef().DeleteImage(0));
    CHECK_FALSE(Kernel.Camera().SetCurrentCamera(-1));
}

TEST_CASE("Builtin integer conversions fail before mutation and preserve real overloads")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("integer-preflight", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    std::string Error;
    float Real = 91.0f;
    CHECK_FALSE(BuiltinValue::FromNumber(1.0e100).AsFloat(Real));
    CHECK(Real == 91.0f);
    CHECK_FALSE(BuiltinValue::FromNumber(std::numeric_limits<double>::quiet_NaN()).AsFloat(Real, &Error));
    CHECK(Real == 91.0f);
    REQUIRE(BuiltinValue::FromNumber(1.0e20).AsFloat(Real));
    CHECK(Real == doctest::Approx(1.0e20f));
    int Number = 91;
    CHECK_FALSE(BuiltinValue::FromNumber(std::numeric_limits<double>::infinity()).AsInt(Number));
    CHECK(Number == 91);
    CHECK_FALSE(BuiltinValue::FromNumber(2147483648.0).AsInt(Number, &Error));
    CHECK(Number == 91);
    REQUIRE(BuiltinValue::FromNumber(-2147483648.0).AsInt(Number));
    CHECK(Number == std::numeric_limits<int>::min());
    REQUIRE(BuiltinValue::FromNumber(12.9).AsInt(Number));
    CHECK(Number == 12);

    REQUIRE(Kernel.ImageStoreRef().CreateBitmap(1, 4, 4));
    CHECK_FALSE(InvokeCommand(Context, dbc::language::SymbolId::Command_DeleteBitmap,
                              {BuiltinValue::FromNumber(2147483648.0)}, Error));
    CHECK(Kernel.ImageStoreRef().BitmapExist(1) == 1);
    CHECK(Kernel.ImageStoreRef().CurrentBitmap() == 1);
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_PositionCamera,
        {BuiltinValue::FromNumber(1.0e20), BuiltinValue::FromNumber(2.0), BuiltinValue::FromNumber(3.0)}, Error));
    CHECK(Kernel.Camera().PositionX() == doctest::Approx(1.0e20f));
    CHECK_FALSE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_PositionCamera,
        {BuiltinValue::FromNumber(4.0), BuiltinValue::FromNumber(1.0e100), BuiltinValue::FromNumber(6.0)}, Error));
    CHECK(Kernel.Camera().PositionX() == doctest::Approx(1.0e20f));
    CHECK(Kernel.Camera().PositionY() == doctest::Approx(2.0f));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MakeMatrix4, {BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_MultiplyMatrix4,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1.0e20)}, Error));
    BuiltinValue Result = BuiltinValue::FromNumber(73);
    CHECK_FALSE(InvokeFunction(Context, dbc::language::SymbolId::Function_BitmapWidth,
                               {BuiltinValue::FromNumber(2147483648.0)}, Result, Error));
    CHECK(Result.Number == 73);
}

TEST_CASE("Extreme 2D coordinates are clipped and BOB integer queries report overflow")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("checked-2d-bounds", 16, 16));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.ImageStoreRef().CreateBitmap(1, 16, 16));
    const int Minimum = std::numeric_limits<int>::min();
    const int Maximum = std::numeric_limits<int>::max();
    Kernel.Draw().SetInk(0xFFFFFF, 0);
    REQUIRE(Kernel.Draw().DrawLine(Minimum, 8, Maximum, 8));
    int Pixel = 0;
    REQUIRE(Kernel.Draw().ReadPoint(Pixel, 8, 8));
    CHECK(Pixel == 0xFFFFFF);
    REQUIRE(Kernel.Draw().DrawBox(Minimum, Minimum, Maximum, Maximum));
    REQUIRE(Kernel.Draw().ReadPoint(Pixel, 15, 15));
    CHECK(Pixel == 0xFFFFFF);
    REQUIRE(Kernel.Draw().DrawCircle(Maximum, Maximum, Minimum));
    REQUIRE(Kernel.Draw().DrawEllipse(Minimum, Minimum, Minimum, Maximum));

    SDL_Surface* Surface = SDL_CreateSurface(128, 1, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(Surface != nullptr);
    REQUIRE(SDL_FillSurfaceRect(Surface, nullptr, 0));
    REQUIRE(Kernel.ImageStoreRef().StoreImageFromSurface(1, Surface));
    SDL_DestroySurface(Surface);
    REQUIRE(Kernel.Sprites().CreateBob(1, 0, 0, 1));
    REQUIRE(Kernel.Sprites().ScaleBob(1, Maximum));
    int Width = 73;
    std::string Error;
    CHECK_FALSE(Kernel.Sprites().BobWidth(Width, 1, &Error));
    CHECK(Width == 73);
    CHECK(Error.find("integer range") != std::string::npos);
    CHECK_FALSE(Kernel.Sprites().BobWidth(Width, 1));
    CHECK(Width == 73);
}

TEST_CASE("DBO frame traversal handles deep input and rejects truncated data before publication")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("checked-dbo-frames", 16, 16));
    Kernel.Objects().MakeObjectCube(1, 1.0f);
    const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto Path = std::filesystem::temp_directory_path() / ("opendbc-deep-" + std::to_string(Stamp) + ".dbo");
    REQUIRE(Kernel.Objects().SaveObject(Path.string(), 1));
    std::vector<std::uint8_t> Original;
    {
        std::ifstream Input(Path, std::ios::binary);
        REQUIRE(Input.good());
        Original.assign(std::istreambuf_iterator<char>(Input), {});
    }
    REQUIRE(Original.size() > 32);
    std::uint32_t FrameSize = 0;
    std::memcpy(&FrameSize, Original.data() + 28, sizeof(FrameSize));
    REQUIRE(FrameSize <= Original.size() - 32);
    constexpr std::uint32_t Depth = 8192;
    std::vector<std::uint8_t> Deep(Original.begin(), Original.begin() + 28);
    const auto Append = [&](std::uint32_t Value)
    {
        const auto* Bytes = reinterpret_cast<const std::uint8_t*>(&Value);
        Deep.insert(Deep.end(), Bytes, Bytes + sizeof(Value));
    };
    Append(FrameSize + Depth * 16);
    for (std::uint32_t Index = 0; Index < Depth; ++Index)
    {
        Append(104); // DBO child-frame block.
        Append(FrameSize + (Depth - Index - 1) * 16);
    }
    Deep.insert(Deep.end(), Original.begin() + 32, Original.begin() + 32 + FrameSize);
    for (std::uint32_t Index = 0; Index < Depth; ++Index)
    {
        Append(0);
        Append(0);
    }
    Deep.insert(Deep.end(), Original.begin() + 32 + FrameSize, Original.end());
    const auto Write = [&]
    {
        std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
        REQUIRE(Output.good());
        Output.write(reinterpret_cast<const char*>(Deep.data()), static_cast<std::streamsize>(Deep.size()));
        Output.close();
        REQUIRE(Output.good());
    };
    Write();
    REQUIRE(Kernel.Objects().LoadObject(Path.string(), 2));
    REQUIRE(Kernel.Objects().GetObject(2) != nullptr);
    CHECK(Kernel.Objects().GetObject(2)->Meshes.size() == 1);

    Deep = Original;
    const std::array<std::uint8_t, 4> VertexDataCode{115, 0, 0, 0};
    const auto VertexBlock = std::search(Deep.begin(), Deep.end(), VertexDataCode.begin(), VertexDataCode.end());
    REQUIRE(VertexBlock != Deep.end());
    REQUIRE(Deep.end() - VertexBlock >= 12);
    const float NonFinite = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(&*(VertexBlock + 8), &NonFinite, sizeof(NonFinite));
    Write();
    std::string Error;
    CHECK_FALSE(Kernel.Objects().LoadObject(Path.string(), 3, 0, 0, &Error));
    CHECK(Error.find("non-finite vertex data") != std::string::npos);
    CHECK(Kernel.Objects().ObjectExist(3) == 0);

    Deep.resize(Deep.size() / 2);
    Write();
    CHECK_FALSE(Kernel.Objects().LoadObject(Path.string(), 4, 0, 0, &Error));
    CHECK_FALSE(Error.empty());
    CHECK(Kernel.Objects().ObjectExist(4) == 0);
    std::error_code CleanupError;
    std::filesystem::remove(Path, CleanupError);
}
