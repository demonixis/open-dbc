#include <doctest/doctest.h>

#include "scene/heightmap.hpp"
#include "scene/scene_geometry.hpp"
#include "scene/scene_renderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using dbc::language::LanguageProfile;

using namespace dbc::studio;

TEST_CASE("Scene rotations decompose back to the angles ROTATE OBJECT takes")
{
    for (const glm::vec3 Angles : {glm::vec3(0.0f), glm::vec3(30.0f, 0.0f, 0.0f), glm::vec3(0.0f, -120.0f, 0.0f),
                                   glm::vec3(10.0f, 45.0f, -75.0f), glm::vec3(-60.0f, 20.0f, 170.0f)})
    {
        CAPTURE(Angles.x);
        CAPTURE(Angles.y);
        CAPTURE(Angles.z);
        const glm::vec3 Back = scene::EulerDegrees(scene::RotationMatrix(Angles));
        const glm::mat3 Expected = scene::RotationMatrix(Angles);
        const glm::mat3 Actual = scene::RotationMatrix(Back);
        for (int Column = 0; Column < 3; ++Column)
            for (int Row = 0; Row < 3; ++Row)
                CHECK(Actual[Column][Row] == doctest::Approx(Expected[Column][Row]).epsilon(1e-4));
    }
    // ROTATE OBJECT 0, 90, 0 turns +Z toward +X, as the engine does.
    const glm::vec3 Turned = scene::RotationMatrix(glm::vec3(0.0f, 90.0f, 0.0f)) * glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK(Turned.x == doctest::Approx(1.0f));
    CHECK(Turned.z == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("Scene primitives keep the engine's sizes and tessellation")
{
    scene::SceneNode Node;
    Node.Kind = scene::NodeKind::Cube;
    Node.Size = 10.0f;
    scene::CpuMesh Mesh = scene::BuildMesh(Node, LanguageProfile::Dbc);
    CHECK(Mesh.Vertices.size() == 36);
    CHECK(Mesh.Max.x == doctest::Approx(5.0f));

    Node.Kind = scene::NodeKind::Sphere;
    Node.Size = 20.0f;
    Mesh = scene::BuildMesh(Node, LanguageProfile::Dbc);
    CHECK(Mesh.Vertices.size() == 12 * 16 * 6);
    CHECK(Mesh.Max.y == doctest::Approx(10.0f));

    Node.Kind = scene::NodeKind::Cylinder;
    CHECK(scene::BuildMesh(Node, LanguageProfile::Dbc).Vertices.size() == 30 * 4 * 3);
    Node.Kind = scene::NodeKind::Cone;
    CHECK(scene::BuildMesh(Node, LanguageProfile::Dbc).Vertices.size() == 16 * 3);

    Node.Kind = scene::NodeKind::Matrix;
    Node.Width = 100.0f;
    Node.Depth = 50.0f;
    Node.XSegments = 4;
    Node.ZSegments = 2;
    Mesh = scene::BuildMesh(Node, LanguageProfile::Dbc);
    CHECK(Mesh.Vertices.size() == 4 * 2 * 6);
    CHECK(Mesh.Min.x == doctest::Approx(0.0f));
    CHECK(Mesh.Max.z == doctest::Approx(50.0f));

    // A Classic plain faces -Z; a Pro plain's raw mesh faces +Z.
    Node.Kind = scene::NodeKind::Plain;
    Node.Extent = glm::vec3(10.0f, 10.0f, 0.0f);
    CHECK(scene::BuildMesh(Node, LanguageProfile::Dbc).Vertices.front().Normal.z == doctest::Approx(-1.0f));
    CHECK(scene::BuildMesh(Node, LanguageProfile::DbPro).Vertices.front().Normal.z == doctest::Approx(1.0f));

    const scene::Ray Down{{0.0f, 100.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    Node.Kind = scene::NodeKind::Cube;
    Node.Size = 10.0f;
    Node.Position = glm::vec3(0.0f, 20.0f, 0.0f);
    const std::optional<float> Hit = scene::Intersect(Down, scene::BuildMesh(Node, LanguageProfile::Dbc),
                                                      scene::NodeWorld(Node));
    REQUIRE(Hit.has_value());
    CHECK(*Hit == doctest::Approx(75.0f));
}

TEST_CASE("Scene models and terrains load from the project's files")
{
    const std::filesystem::path Fixtures = std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic";
    scene::CpuMesh Mesh;
    std::string Error;
    CHECK_MESSAGE(scene::LoadModelMesh(Fixtures / "models/dome.x", Mesh, Error), Error);
    CHECK(Mesh.Vertices.size() >= 3);
    CHECK_FALSE(scene::LoadModelMesh(Fixtures / "models/missing.x", Mesh, Error));

    REQUIRE(scene::LoadTerrainMesh(Fixtures / "bmp/stone.bmp", Mesh, Error));
    // The terrain runs from its position toward -Z, never above the height scale.
    CHECK(Mesh.Max.z == doctest::Approx(0.0f));
    CHECK(Mesh.Min.z < 0.0f);
    CHECK(Mesh.Max.y <= scene::KTerrainHeightScale);
}

TEST_CASE("The Scene renderer draws a lit, textured level offscreen")
{
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_GPUDevice* Gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (Gpu == nullptr)
    {
        const std::string Reason = std::string("No GPU device: ") + SDL_GetError();
        MESSAGE(Reason);
        SDL_Quit();
        return;
    }

    scene::SceneRenderer Renderer;
    std::string Error;
    REQUIRE_MESSAGE(Renderer.Initialize(Gpu, Error), Error);
    constexpr int KSize = 256;
    SDL_GPUTexture* Target = Renderer.Target(KSize, KSize);
    REQUIRE(Target != nullptr);

    scene::SceneDocument Document;
    Document.Profile = LanguageProfile::Dbc;
    Document.Env.Backdrop = 0x000040;
    scene::SceneNode& Crate = Document.Add(scene::NodeKind::Cube);
    Crate.Size = 40.0f;
    Crate.Rotation = glm::vec3(0.0f, 30.0f, 0.0f);
    Crate.Mat.Texture = "bmp/stone.bmp";
    scene::SceneNode& Ball = Document.Add(scene::NodeKind::Sphere);
    Ball.Size = 20.0f;
    Ball.Position = glm::vec3(40.0f, 0.0f, 0.0f);
    Ball.Mat.Color = 0xFF2020;
    scene::SceneNode& Ground = Document.Add(scene::NodeKind::Matrix);
    Ground.Position = glm::vec3(-50.0f, -20.0f, -50.0f);

    scene::ViewParameters View;
    View.Eye = glm::vec3(0.0f, 40.0f, -120.0f);
    View.View = glm::lookAt(View.Eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    View.Projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.5f, 2000.0f);
    const std::vector<scene::LineVertex> Lines = {{{-100.0f, -20.0f, 60.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
                                                  {{100.0f, -20.0f, 60.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}};
    Renderer.Prepare(Document, View, std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/classic", Lines);
    CHECK(Renderer.TakeErrors().empty());
    // Add() may move the nodes, so look the crate up again.
    REQUIRE(Renderer.Mesh(Document.Nodes.front()) != nullptr);

    SDL_GPUTransferBufferCreateInfo TransferInfo = {};
    TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    TransferInfo.size = KSize * KSize * 4;
    SDL_GPUTransferBuffer* Download = SDL_CreateGPUTransferBuffer(Gpu, &TransferInfo);
    SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Gpu);
    REQUIRE(Commands != nullptr);
    Renderer.Record(Commands);
    SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Commands);
    SDL_GPUTextureRegion From = {};
    From.texture = Target;
    From.w = KSize;
    From.h = KSize;
    From.d = 1;
    SDL_GPUTextureTransferInfo To = {};
    To.transfer_buffer = Download;
    SDL_DownloadFromGPUTexture(Copy, &From, &To);
    SDL_EndGPUCopyPass(Copy);
    SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
    REQUIRE(Fence != nullptr);
    REQUIRE(SDL_WaitForGPUFences(Gpu, true, &Fence, 1));
    SDL_ReleaseGPUFence(Gpu, Fence);

    std::vector<Uint8> Pixels(KSize * KSize * 4);
    std::memcpy(Pixels.data(), SDL_MapGPUTransferBuffer(Gpu, Download, false), Pixels.size());
    SDL_UnmapGPUTransferBuffer(Gpu, Download);
    SDL_ReleaseGPUTransferBuffer(Gpu, Download);

    const auto At = [&](int X, int Y) { return &Pixels[(static_cast<std::size_t>(Y) * KSize + X) * 4]; };
    // The backdrop shows in a corner; the crate covers the middle and is not flat.
    CHECK(At(2, 2)[2] == 0x40);
    const Uint8* Middle = At(KSize / 2, KSize / 2);
    CHECK((Middle[0] != 0 || Middle[1] != 0 || Middle[2] != 0x40));
    int Reddish = 0, Covered = 0;
    for (int Index = 0; Index < KSize * KSize; ++Index)
    {
        const Uint8* Pixel = &Pixels[static_cast<std::size_t>(Index) * 4];
        if (Pixel[0] != 0 || Pixel[1] != 0 || Pixel[2] != 0x40)
            ++Covered;
        if (Pixel[0] > 2 * Pixel[1] + 30 && Pixel[0] > 2 * Pixel[2] + 30)
            ++Reddish;
    }
    CHECK(Covered > KSize * KSize / 10);
    CHECK(Reddish > 50);

    // Kept for a look by hand: build/tests/scene_render.png.
    if (SDL_Surface* Image = SDL_CreateSurfaceFrom(KSize, KSize, SDL_PIXELFORMAT_ABGR8888, Pixels.data(), KSize * 4))
    {
        IMG_SavePNG(Image, (std::filesystem::path(TEST_OUTPUT_DIR) / "scene_render.png").string().c_str());
        SDL_DestroySurface(Image);
    }

    Renderer.Shutdown();
    SDL_DestroyGPUDevice(Gpu);
    SDL_Quit();
}

TEST_CASE("Generated terrain heightmaps load back as the heights they were made for")
{
    const scene::Heightmap Flat = scene::FlatHeightmap(4, 50.0f);
    REQUIRE(Flat.size() == 16);
    CHECK(std::all_of(Flat.begin(), Flat.end(), [](std::uint8_t Level) { return Level == 128; }));

    std::mt19937 First(7), Second(7);
    const scene::Heightmap Hills = scene::RandomHeightmap(64, 40.0f, 4, First);
    CHECK(Hills == scene::RandomHeightmap(64, 40.0f, 4, Second));
    const auto [Low, High] = std::minmax_element(Hills.begin(), Hills.end());
    CHECK(*Low == 0);
    CHECK(*High == 102); // 40 of 100 units
    CHECK(scene::TerrainHeightmapPath("Level 1", 2).generic_string() == "heightmaps/Level_1_terrain2.png");

    const std::filesystem::path Path =
        std::filesystem::temp_directory_path() / "opendbc-heightmap-test" / "heightmaps" / "hills.png";
    std::string Error;
    REQUIRE_MESSAGE(scene::WriteHeightmap(Path, 64, Hills, Error), Error);
    CHECK_FALSE(scene::WriteHeightmap(Path, 63, Hills, Error));
    scene::CpuMesh Mesh;
    REQUIRE_MESSAGE(scene::LoadTerrainMesh(Path, Mesh, Error), Error);
    scene::ComputeBounds(Mesh);
    CHECK(Mesh.Min.y == doctest::Approx(0.0f));
    CHECK(Mesh.Max.y == doctest::Approx(40.0f).epsilon(0.01));
    std::error_code Ignored;
    std::filesystem::remove_all(Path.parent_path().parent_path(), Ignored);
}
