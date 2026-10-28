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
#include <fstream>
#include <random>
#include <string>
#include <vector>

using dbc::language::LanguageProfile;

using namespace dbc::studio;

namespace
{
    // Records the renderer's prepared frame, if any, and reads `Target` back as RGBA8; empty on failure.
    std::vector<Uint8> ReadPixels(SDL_GPUDevice* Gpu, SDL_GPUTexture* Target, int Size,
                                  scene::SceneRenderer* Renderer = nullptr)
    {
        SDL_GPUTransferBufferCreateInfo TransferInfo = {};
        TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        TransferInfo.size = static_cast<Uint32>(Size * Size * 4);
        SDL_GPUTransferBuffer* Download = SDL_CreateGPUTransferBuffer(Gpu, &TransferInfo);
        SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Gpu);
        if (Download == nullptr || Commands == nullptr)
            return {};
        if (Renderer != nullptr)
            Renderer->Record(Commands);
        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Commands);
        SDL_GPUTextureRegion From = {};
        From.texture = Target;
        From.w = static_cast<Uint32>(Size);
        From.h = static_cast<Uint32>(Size);
        From.d = 1;
        SDL_GPUTextureTransferInfo To = {};
        To.transfer_buffer = Download;
        SDL_DownloadFromGPUTexture(Copy, &From, &To);
        SDL_EndGPUCopyPass(Copy);
        SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
        const bool Done = Fence != nullptr && SDL_WaitForGPUFences(Gpu, true, &Fence, 1);
        if (Fence != nullptr)
            SDL_ReleaseGPUFence(Gpu, Fence);
        std::vector<Uint8> Pixels;
        if (Done)
        {
            Pixels.resize(TransferInfo.size);
            std::memcpy(Pixels.data(), SDL_MapGPUTransferBuffer(Gpu, Download, false), Pixels.size());
            SDL_UnmapGPUTransferBuffer(Gpu, Download);
        }
        SDL_ReleaseGPUTransferBuffer(Gpu, Download);
        return Pixels;
    }

    // Two quads facing -Z: the left one textured by its material, the right one red.
    void WriteTexturedModel(const std::filesystem::path& Folder)
    {
        std::filesystem::create_directories(Folder / "Textures");
        SDL_Surface* Green = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(Green != nullptr);
        SDL_FillSurfaceRect(Green, nullptr, SDL_MapSurfaceRGBA(Green, 0, 255, 0, 255));
        REQUIRE(IMG_SavePNG(Green, (Folder / "Textures/grass.png").string().c_str()));
        SDL_DestroySurface(Green);
        std::ofstream(Folder / "pair.mtl") << "newmtl Grass\nKd 1 1 1\nmap_Kd textures\\GRASS.png\n"
                                              "newmtl Red\nKd 1 0 0\n";
        std::ofstream(Folder / "pair.obj") << "mtllib pair.mtl\n"
                                              "v -40 -20 0\nv -5 -20 0\nv -5 20 0\nv -40 20 0\n"
                                              "v 5 -20 0\nv 40 -20 0\nv 40 20 0\nv 5 20 0\n"
                                              "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                              "usemtl Grass\nf 1/1 2/2 3/3 4/4\n"
                                              "usemtl Red\nf 5/1 6/2 7/3 8/4\n";
    }
} // namespace

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

TEST_CASE("Scene snapping moves only the dragged axes onto whole steps")
{
    const glm::vec3 Start(3.2f, -7.5f, 12.0f);
    const glm::vec3 Snapped = scene::SnapChangedAxes(glm::vec3(16.4f, -7.5f, -4.9f), Start, 10.0f);
    CHECK(Snapped.x == doctest::Approx(20.0f));
    CHECK(Snapped.y == doctest::Approx(-7.5f));
    CHECK(Snapped.z == doctest::Approx(0.0f));
    const glm::vec3 Unsnapped = scene::SnapChangedAxes(glm::vec3(16.4f, -7.5f, -4.9f), Start, 0.0f);
    CHECK(Unsnapped.x == doctest::Approx(16.4f));
}

TEST_CASE("Uniform scale keeps the proportions of the starting scale")
{
    const glm::vec3 Scaled =
        scene::ProportionalScale(glm::vec3(100.0f, 50.0f, 200.0f), glm::vec3(100.0f, 75.0f, 200.0f));
    CHECK(Scaled.x == doctest::Approx(150.0f));
    CHECK(Scaled.y == doctest::Approx(75.0f));
    CHECK(Scaled.z == doctest::Approx(300.0f));
    // A zero axis has no ratio to follow.
    const glm::vec3 Flat = scene::ProportionalScale(glm::vec3(0.0f, 100.0f, 100.0f), glm::vec3(40.0f, 100.0f, 100.0f));
    CHECK(Flat.x == doctest::Approx(40.0f));
    CHECK(Flat.y == doctest::Approx(100.0f));
}

TEST_CASE("Group gizmo edits move, turn and scale nodes about the pivot")
{
    scene::SceneNode Cube;
    Cube.Position = glm::vec3(10.0f, 0.0f, 0.0f);
    Cube.Rotation = glm::vec3(0.0f, 30.0f, 0.0f);
    Cube.Scale = glm::vec3(100.0f, 50.0f, 100.0f);
    scene::SceneNode Matrix;
    Matrix.Kind = scene::NodeKind::Matrix;
    Matrix.Position = glm::vec3(-10.0f, 0.0f, 0.0f);

    scene::GroupChange Move;
    Move.Move = glm::vec3(0.0f, 5.0f, 0.0f);
    scene::SceneNode Node = Cube;
    scene::ApplyGroupChange(Move, Cube, Node);
    CHECK(Node.Position == glm::vec3(10.0f, 5.0f, 0.0f));
    CHECK(Node.Rotation == Cube.Rotation);
    CHECK(Node.Scale == Cube.Scale);

    // A quarter turn about Y carries +X to where RotationMatrix sends it, and adds to the angle.
    scene::GroupChange Turn;
    Turn.Turn = scene::RotationMatrix(glm::vec3(0.0f, 90.0f, 0.0f));
    scene::ApplyGroupChange(Turn, Cube, Node);
    const glm::vec3 Expected = Turn.Turn * Cube.Position;
    CHECK(glm::length(Node.Position - Expected) < 1e-4f);
    // Compared as orientations: EulerDegrees may pick another set of angles for the same turn.
    const glm::mat3 Turned = scene::RotationMatrix(Node.Rotation);
    const glm::mat3 Wanted = scene::RotationMatrix(glm::vec3(0.0f, 120.0f, 0.0f));
    for (int Column = 0; Column < 3; ++Column)
        CHECK(glm::length(Turned[Column] - Wanted[Column]) < 1e-4f);
    scene::SceneNode Flat = Matrix;
    scene::ApplyGroupChange(Turn, Matrix, Flat);
    CHECK(glm::length(Flat.Position - Turn.Turn * Matrix.Position) < 1e-4f);
    CHECK(Flat.Rotation == Matrix.Rotation);

    // Doubling along X spreads the nodes from the pivot and scales only objects.
    scene::GroupChange Grow;
    Grow.Factor = glm::vec3(2.0f, 1.0f, 1.0f);
    scene::ApplyGroupChange(Grow, Cube, Node);
    CHECK(Node.Position == glm::vec3(20.0f, 0.0f, 0.0f));
    CHECK(Node.Scale == glm::vec3(200.0f, 50.0f, 100.0f));
    scene::ApplyGroupChange(Grow, Matrix, Flat);
    CHECK(Flat.Position == glm::vec3(-20.0f, 0.0f, 0.0f));
    CHECK(Flat.Scale == Matrix.Scale);
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

    // The frames LOAD OBJECT gives the file's first animation, for the Inspector's range.
    scene::ModelAnimation Animation;
    CHECK_MESSAGE(scene::ReadModelAnimation(Fixtures / "models/walk.x", Animation, Error), Error);
    CHECK(Animation.Count == 1);
    CHECK(Animation.LastFrame == 25);
    CHECK_MESSAGE(scene::ReadModelAnimation(Fixtures / "models/dome.x", Animation, Error), Error);
    CHECK(Animation.Count == 0);
    CHECK_FALSE(scene::ReadModelAnimation(Fixtures / "models/missing.x", Animation, Error));

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

    std::vector<Uint8> Pixels = ReadPixels(Gpu, Target, KSize, &Renderer);
    REQUIRE(Pixels.size() == KSize * KSize * 4);

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

TEST_CASE("Scene models keep their materials' textures and colours")
{
    const std::filesystem::path Folder = std::filesystem::temp_directory_path() / "opendbc-scene-model-test";
    std::error_code Ignored;
    std::filesystem::remove_all(Folder, Ignored);
    WriteTexturedModel(Folder / "models");

    scene::CpuMesh Mesh;
    std::string Error;
    REQUIRE_MESSAGE(scene::LoadModelMesh(Folder / "models/pair.obj", Mesh, Error), Error);
    REQUIRE(Mesh.Parts.size() == 2);
    std::size_t Covered = 0;
    for (const scene::MeshPart& Part : Mesh.Parts)
        Covered += Part.Count;
    CHECK(Covered == Mesh.Vertices.size());
    const auto Textured = std::find_if(Mesh.Parts.begin(), Mesh.Parts.end(),
                                       [](const scene::MeshPart& Part) { return !Part.Texture.empty(); });
    REQUIRE(Textured != Mesh.Parts.end());
    // The name as the material writes it; the renderer resolves its folder and case.
    CHECK(Textured->Texture == "textures/GRASS.png");

    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_GPUDevice* Gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (Gpu == nullptr)
    {
        const std::string Reason = std::string("No GPU device: ") + SDL_GetError();
        MESSAGE(Reason);
        SDL_Quit();
        std::filesystem::remove_all(Folder, Ignored);
        return;
    }
    scene::SceneRenderer Renderer;
    REQUIRE_MESSAGE(Renderer.Initialize(Gpu, Error), Error);
    constexpr int KSize = 128;
    SDL_GPUTexture* Target = Renderer.Target(KSize, KSize);
    REQUIRE(Target != nullptr);

    // Dropped from the assets panel: the node names the model and nothing else.
    scene::SceneDocument Document;
    Document.Profile = LanguageProfile::Dbc;
    scene::SceneNode& Model = Document.Add(scene::NodeKind::Model);
    Model.Asset = "models/pair.obj";
    Model.Mat.Flags.Lighting = false;
    scene::ViewParameters View;
    View.Eye = glm::vec3(0.0f, 0.0f, -100.0f);
    View.View = glm::lookAt(View.Eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    View.Projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.5f, 2000.0f);
    Renderer.Prepare(Document, View, Folder, {});
    CHECK(Renderer.TakeErrors().empty());
    const std::vector<Uint8> Pixels = ReadPixels(Gpu, Target, KSize, &Renderer);
    REQUIRE(Pixels.size() == KSize * KSize * 4);

    int Green = 0, Red = 0;
    for (int Index = 0; Index < KSize * KSize; ++Index)
    {
        const Uint8* Pixel = &Pixels[static_cast<std::size_t>(Index) * 4];
        Green += Pixel[1] > 200 && Pixel[0] < 50 && Pixel[2] < 50;
        Red += Pixel[0] > 200 && Pixel[1] < 50 && Pixel[2] < 50;
    }
    CHECK(Green > 100);
    CHECK(Red > 100);

    // The Assets panel's tile: the model framed alone, both of its materials in view.
    core::GpuImage Thumbnail;
    REQUIRE_MESSAGE(Renderer.RenderThumbnail(Folder, "models/pair.obj", KSize, 0x000040, Thumbnail, Error), Error);
    const std::vector<Uint8> Tile = ReadPixels(Gpu, Thumbnail.Texture, KSize);
    REQUIRE(Tile.size() == KSize * KSize * 4);
    Green = Red = 0;
    int Backdrop = 0;
    for (int Index = 0; Index < KSize * KSize; ++Index)
    {
        const Uint8* Pixel = &Tile[static_cast<std::size_t>(Index) * 4];
        Green += Pixel[1] > 100 && Pixel[1] > 2 * Pixel[0] && Pixel[1] > 2 * Pixel[2];
        Red += Pixel[0] > 100 && Pixel[0] > 2 * Pixel[1] && Pixel[0] > 2 * Pixel[2];
        Backdrop += Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0x40;
    }
    CHECK(Green > KSize * KSize / 20);
    CHECK(Red > KSize * KSize / 20);
    CHECK(Backdrop > 0);
    // Kept for a look by hand: build/tests/model_thumbnail.png.
    std::vector<Uint8> Copy = Tile;
    if (SDL_Surface* Image = SDL_CreateSurfaceFrom(KSize, KSize, SDL_PIXELFORMAT_ABGR8888, Copy.data(), KSize * 4))
    {
        IMG_SavePNG(Image, (std::filesystem::path(TEST_OUTPUT_DIR) / "model_thumbnail.png").string().c_str());
        SDL_DestroySurface(Image);
    }
    core::ReleaseGpuImage(Gpu, Thumbnail);
    CHECK_FALSE(Renderer.RenderThumbnail(Folder, "models/missing.obj", KSize, 0, Thumbnail, Error));
    CHECK(Thumbnail.Texture == nullptr);

    Renderer.Shutdown();
    SDL_DestroyGPUDevice(Gpu);
    SDL_Quit();
    std::filesystem::remove_all(Folder, Ignored);
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
