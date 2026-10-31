#include "camera_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "object_manager.hpp"
#include "platform_io.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace dbc
{
    namespace
    {
        // Reads the red channel, the only one DBPro's terrain looks at, with the image's top row at the largest Z.
        [[nodiscard]] bool ReadTerrainHeights(AdvancedTerrain& Terrain, std::string* Error)
        {
            const auto Fail = [&](const std::string& Reason)
            {
                if (Error)
                    *Error = "BUILD TERRAIN " + Reason;
                return false;
            };
            if (Terrain.HeightmapPath.empty())
                return Fail("requires SET TERRAIN HEIGHTMAP");
            SDL_Surface* Surface = LoadSurface(Terrain.HeightmapPath);
            if (!Surface)
                return Fail("failed to load heightmap '" + Terrain.HeightmapPath + "': " + SDL_GetError());
            SDL_Surface* Rgba = SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(Surface);
            if (!Rgba)
                return Fail("failed to convert heightmap '" + Terrain.HeightmapPath + "': " + SDL_GetError());
            const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> Owner(Rgba, SDL_DestroySurface);
            if (Rgba->w < 2 || Rgba->h < 2)
                return Fail("heightmap must be at least 2x2 pixels");
            if (!SDL_LockSurface(Rgba))
                return Fail("failed to read heightmap '" + Terrain.HeightmapPath + "': " + SDL_GetError());
            Terrain.Width = Rgba->w;
            Terrain.Depth = Rgba->h;
            Terrain.Heights.resize(static_cast<std::size_t>(Rgba->w) * static_cast<std::size_t>(Rgba->h));
            for (int Z = 0; Z < Rgba->h; ++Z)
            {
                const auto* Row = static_cast<const Uint8*>(Rgba->pixels) +
                                  static_cast<std::ptrdiff_t>(Rgba->h - 1 - Z) * Rgba->pitch;
                for (int X = 0; X < Rgba->w; ++X)
                    Terrain.Heights[static_cast<std::size_t>(Z) * Rgba->w + X] =
                        static_cast<float>(Row[static_cast<std::ptrdiff_t>(X) * 4]) * Terrain.ScaleY;
            }
            SDL_UnlockSurface(Rgba);
            Terrain.CellWidth = Terrain.ScaleX;
            Terrain.CellDepth = Terrain.ScaleZ;
            return true;
        }

        // A map of W pixels spans W cells: the last row of vertices repeats the last row of pixels.
        [[nodiscard]] float TerrainHeight(const AdvancedTerrain& Terrain, int X, int Z)
        {
            return Terrain.Heights[static_cast<std::size_t>(std::min(Z, Terrain.Depth - 1)) * Terrain.Width +
                                   std::min(X, Terrain.Width - 1)];
        }

        [[nodiscard]] uint32_t TerrainVertexColor(const AdvancedTerrain& Terrain, const Vec3& Normal)
        {
            if (!Terrain.Lit)
                return 0xFFFFFFFFu;
            const Vec3 ToLight =
                Vec3Normalize({-Terrain.LightDirection.X, -Terrain.LightDirection.Y, -Terrain.LightDirection.Z});
            const float Facing = std::max(Vec3Dot(Normal, ToLight), 0.0f);
            const float Shade = 1.0f - Terrain.LightScale * (1.0f - Facing);
            const auto Channel = [&](float Color)
            { return static_cast<uint32_t>(std::lround(std::clamp(Color * Shade, 0.0f, 1.0f) * 255.0f)); };
            return 0xFF000000u | (Channel(Terrain.LightRed) << 16u) | (Channel(Terrain.LightGreen) << 8u) |
                   Channel(Terrain.LightBlue);
        }

        [[nodiscard]] Mesh BuildTerrainPart(const AdvancedTerrain& Terrain, int FirstX, int LastX, int FirstZ,
                                            int LastZ)
        {
            Mesh Part;
            MeshGeometry& Geometry = Part.EditGeometry();
            const std::size_t Count =
                static_cast<std::size_t>(LastX - FirstX + 1) * static_cast<std::size_t>(LastZ - FirstZ + 1);
            Geometry.Vertices.reserve(Count);
            Geometry.Normals.reserve(Count);
            Geometry.Uvs.reserve(Count);
            Geometry.DiffuseColors.reserve(Count);
            std::vector<SDL_FPoint>& DetailUvs = Geometry.UvStages[1];
            DetailUvs.reserve(Count);
            for (int Z = FirstZ; Z <= LastZ; ++Z)
            {
                for (int X = FirstX; X <= LastX; ++X)
                {
                    const float SlopeX =
                        (TerrainHeight(Terrain, X + 1, Z) - TerrainHeight(Terrain, std::max(X - 1, 0), Z)) /
                        (2.0f * Terrain.CellWidth);
                    const float SlopeZ =
                        (TerrainHeight(Terrain, X, Z + 1) - TerrainHeight(Terrain, X, std::max(Z - 1, 0))) /
                        (2.0f * Terrain.CellDepth);
                    const Vec3 Normal = Vec3Normalize({-SlopeX, 1.0f, -SlopeZ});
                    Geometry.Vertices.push_back({static_cast<float>(X) * Terrain.CellWidth,
                                                 TerrainHeight(Terrain, X, Z),
                                                 static_cast<float>(Z) * Terrain.CellDepth});
                    Geometry.Normals.push_back(Normal);
                    Geometry.DiffuseColors.push_back(TerrainVertexColor(Terrain, Normal));
                    // The base image lies over the whole terrain as seen from above.
                    Geometry.Uvs.push_back({static_cast<float>(X) / static_cast<float>(Terrain.Width),
                                            1.0f - static_cast<float>(Z) / static_cast<float>(Terrain.Depth)});
                    // Tiling is the number of cells one repeat of the detail image covers.
                    DetailUvs.push_back(
                        {static_cast<float>(X) / Terrain.Tiling, static_cast<float>(Z) / Terrain.Tiling});
                }
            }
            const uint32_t Stride = static_cast<uint32_t>(LastX - FirstX + 1);
            Geometry.Indices.reserve(static_cast<std::size_t>(LastX - FirstX) * (LastZ - FirstZ) * 6u);
            for (uint32_t Z = 0; Z < static_cast<uint32_t>(LastZ - FirstZ); ++Z)
            {
                for (uint32_t X = 0; X < Stride - 1u; ++X)
                {
                    const uint32_t Near = Z * Stride + X;
                    const uint32_t Far = Near + Stride;
                    for (const uint32_t Corner : {Near, Far, Far + 1u, Far + 1u, Near + 1u, Near})
                        Geometry.Indices.push_back(Corner);
                }
            }
            return Part;
        }
    } // namespace

    AdvancedTerrain* ObjectManager::RequireTerrainSettings(int ObjectId, const char* Command, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, Command, Error);
        if (!Object)
            return nullptr;
        if (!Object->Terrain && Error)
            *Error = std::string(Command) + " requires a terrain made with MAKE OBJECT TERRAIN";
        return Object->Terrain.get();
    }

    bool ObjectManager::MakeObjectTerrain(int ObjectId, std::string* Error)
    {
        (void)Error;
        Object3D Object = NewObjectState();
        Object.Visible = true;
        // SET TERRAIN LIGHT bakes its shading into the vertices.
        Object.UseLighting = false;
        Object.Terrain = std::make_shared<AdvancedTerrain>();
        Objects_[ObjectId] = std::move(Object);
        return true;
    }

    bool ObjectManager::SetTerrainHeightmap(int ObjectId, std::string Path, std::string* Error)
    {
        AdvancedTerrain* Terrain = RequireTerrainSettings(ObjectId, "SET TERRAIN HEIGHTMAP", Error);
        if (!Terrain)
            return false;
        Terrain->HeightmapPath = std::move(Path);
        return true;
    }

    bool ObjectManager::SetTerrainScale(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        AdvancedTerrain* Terrain = RequireTerrainSettings(ObjectId, "SET TERRAIN SCALE", Error);
        if (!Terrain)
            return false;
        if (!(std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z) && X > 0.0f && Y > 0.0f && Z > 0.0f))
        {
            if (Error)
                *Error = "SET TERRAIN SCALE requires positive scales";
            return false;
        }
        Terrain->ScaleX = X;
        Terrain->ScaleY = Y;
        Terrain->ScaleZ = Z;
        return true;
    }

    bool ObjectManager::SetTerrainSplit(int ObjectId, int Split, std::string* Error)
    {
        AdvancedTerrain* Terrain = RequireTerrainSettings(ObjectId, "SET TERRAIN SPLIT", Error);
        if (!Terrain)
            return false;
        // As many meshes as an object can save.
        if (Split < 1 || Split > 64)
        {
            if (Error)
                *Error = "SET TERRAIN SPLIT must be between 1 and 64";
            return false;
        }
        Terrain->Split = Split;
        return true;
    }

    bool ObjectManager::SetTerrainTiling(int ObjectId, float Tiling, std::string* Error)
    {
        AdvancedTerrain* Terrain = RequireTerrainSettings(ObjectId, "SET TERRAIN TILING", Error);
        if (!Terrain)
            return false;
        if (!std::isfinite(Tiling) || Tiling <= 0.0f)
        {
            if (Error)
                *Error = "SET TERRAIN TILING must be positive";
            return false;
        }
        Terrain->Tiling = Tiling;
        return true;
    }

    bool ObjectManager::SetTerrainLight(int ObjectId, const Vec3& Direction, float Red, float Green, float Blue,
                                        float Scale, std::string* Error)
    {
        AdvancedTerrain* Terrain = RequireTerrainSettings(ObjectId, "SET TERRAIN LIGHT", Error);
        if (!Terrain)
            return false;
        if (!(Vec3Dot(Direction, Direction) > 0.0f))
        {
            if (Error)
                *Error = "SET TERRAIN LIGHT requires a direction";
            return false;
        }
        Terrain->Lit = true;
        Terrain->LightDirection = Direction;
        Terrain->LightRed = Red;
        Terrain->LightGreen = Green;
        Terrain->LightBlue = Blue;
        Terrain->LightScale = std::clamp(Scale, 0.0f, 1.0f);
        return true;
    }

    bool ObjectManager::SetTerrainTexture(int ObjectId, int BaseImage, int DetailImage, std::string* Error)
    {
        AdvancedTerrain* Terrain = RequireTerrainSettings(ObjectId, "SET TERRAIN TEXTURE", Error);
        if (!Terrain)
            return false;
        for (const int ImageId : {BaseImage, DetailImage})
        {
            if (ImageId != 0 && Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
            {
                if (Error)
                    *Error = "SET TERRAIN TEXTURE requires existing image " + std::to_string(ImageId);
                return false;
            }
        }
        Terrain->BaseImage = BaseImage;
        Terrain->DetailImage = DetailImage;
        return true;
    }

    bool ObjectManager::BuildTerrain(int ObjectId, std::string* Error)
    {
        AdvancedTerrain* TerrainPointer = RequireTerrainSettings(ObjectId, "BUILD TERRAIN", Error);
        if (!TerrainPointer)
            return false;
        AdvancedTerrain& Terrain = *TerrainPointer;
        if (!ReadTerrainHeights(Terrain, Error))
            return false;

        Object3D& Object = Objects_[ObjectId];
        const int SplitX = std::min(Terrain.Split, Terrain.Width);
        const int SplitZ = std::min(Terrain.Split, Terrain.Depth);
        Object.Meshes.clear();
        Object.Meshes.reserve(static_cast<std::size_t>(SplitX) * SplitZ);
        Terrain.Parts.clear();
        Terrain.Parts.reserve(static_cast<std::size_t>(SplitX) * SplitZ);
        for (int CellZ = 0; CellZ < SplitZ; ++CellZ)
        {
            for (int CellX = 0; CellX < SplitX; ++CellX)
            {
                const int FirstX = Terrain.Width * CellX / SplitX;
                const int LastX = Terrain.Width * (CellX + 1) / SplitX;
                const int FirstZ = Terrain.Depth * CellZ / SplitZ;
                const int LastZ = Terrain.Depth * (CellZ + 1) / SplitZ;
                Mesh Part = BuildTerrainPart(Terrain, FirstX, LastX, FirstZ, LastZ);
                Part.SourceMeshIndex = static_cast<int>(Object.Meshes.size());
                Part.TextureImageId = Terrain.BaseImage;
                if (Terrain.DetailImage != 0)
                {
                    Part.TextureStageImageIds[1] = Terrain.DetailImage;
                    // Stages blend in linear light, where this is what doubling does to sRGB colours: mid-grey
                    // detail leaves the base nearly as it is.
                    Part.TextureStageBlendModes[1] = 6;
                }

                Vec3 Min = Part.Geometry().Vertices.front();
                Vec3 Max = Min;
                for (const Vec3& Vertex : Part.Geometry().Vertices)
                {
                    Min = {std::min(Min.X, Vertex.X), std::min(Min.Y, Vertex.Y), std::min(Min.Z, Vertex.Z)};
                    Max = {std::max(Max.X, Vertex.X), std::max(Max.Y, Vertex.Y), std::max(Max.Z, Vertex.Z)};
                }
                const Vec3 Half{(Max.X - Min.X) * 0.5f, (Max.Y - Min.Y) * 0.5f, (Max.Z - Min.Z) * 0.5f};
                Terrain.Parts.push_back(
                    {Vec3{Min.X + Half.X, Min.Y + Half.Y, Min.Z + Half.Z}, std::sqrt(Vec3Dot(Half, Half))});
                Object.Meshes.push_back(std::move(Part));
            }
        }
        Object.LimbTransforms.assign(Object.Meshes.size(), LimbTransform{});
        Object.TextureImageId = Terrain.BaseImage;
        const float SizeX = static_cast<float>(Terrain.Width) * Terrain.CellWidth;
        const float SizeZ = static_cast<float>(Terrain.Depth) * Terrain.CellDepth;
        Object.BoundingRadius = std::hypot(SizeX, SizeZ) * 0.5f;
        Object.RenderBoundingRadius = -1.0f;
        Object.TransformDirty = true;
        return true;
    }

    void ObjectManager::UpdateTerrains()
    {
        std::optional<RenderView> CameraView;
        const RenderView& View = ActiveOrCameraView(Kernel_, CameraView);
        for (auto& [ObjectId, Object] : Objects_)
        {
            if (!Object.Terrain || Object.Meshes.size() != Object.Terrain->Parts.size())
                continue;
            // Each part's sphere only follows the object's position and scale: a turned terrain stays whole.
            const bool Turned = Object.RotX != 0.0f || Object.RotY != 0.0f || Object.RotZ != 0.0f ||
                                Object.GluedTargetObject > 0 || Object.LockToScreen;
            const float MaxScale =
                std::max({std::abs(Object.ScaleX), std::abs(Object.ScaleY), std::abs(Object.ScaleZ)});
            for (std::size_t Index = 0; Index < Object.Meshes.size(); ++Index)
            {
                const AdvancedTerrain::Part& Part = Object.Terrain->Parts[Index];
                bool Visible = true;
                if (!Turned)
                {
                    const Vec3 Center = WorldToViewPoint(Kernel_, Object.PosX + Part.Center.X * Object.ScaleX,
                                                         Object.PosY + Part.Center.Y * Object.ScaleY,
                                                         Object.PosZ + Part.Center.Z * Object.ScaleZ);
                    Visible = View.ContainsSphere({Center.X, Center.Y, Center.Z}, Part.Radius * MaxScale);
                }
                Object.Meshes[Index].Visible = Visible;
            }
        }
    }

    bool ObjectManager::GetTerrainGroundHeight(float& Output, int ObjectId, float X, float Z, std::string* Error) const
    {
        const Object3D* Object = RequireObject(ObjectId, "GET TERRAIN GROUND HEIGHT", Error);
        if (!Object)
            return false;
        if (!Object->Terrain || Object->Terrain->Heights.empty())
        {
            if (Error)
                *Error = "GET TERRAIN GROUND HEIGHT requires a built terrain";
            return false;
        }
        if (!std::isfinite(X) || !std::isfinite(Z))
        {
            if (Error)
                *Error = "GET TERRAIN GROUND HEIGHT coordinates must be finite";
            return false;
        }
        const AdvancedTerrain& Terrain = *Object->Terrain;
        const auto GridPosition = [](float World, float Origin, float Cell, float Scale, int Size)
        {
            const float Span = Cell * Scale;
            return Span != 0.0f ? std::clamp((World - Origin) / Span, 0.0f, static_cast<float>(Size)) : 0.0f;
        };
        const float GridX = GridPosition(X, Object->PosX, Terrain.CellWidth, Object->ScaleX, Terrain.Width);
        const float GridZ = GridPosition(Z, Object->PosZ, Terrain.CellDepth, Object->ScaleZ, Terrain.Depth);
        const int CellX = std::min(static_cast<int>(GridX), Terrain.Width - 1);
        const int CellZ = std::min(static_cast<int>(GridZ), Terrain.Depth - 1);
        const float U = GridX - static_cast<float>(CellX);
        const float V = GridZ - static_cast<float>(CellZ);
        const float Near = TerrainHeight(Terrain, CellX, CellZ);
        const float NearRight = TerrainHeight(Terrain, CellX + 1, CellZ);
        const float Far = TerrainHeight(Terrain, CellX, CellZ + 1);
        const float FarRight = TerrainHeight(Terrain, CellX + 1, CellZ + 1);
        // On the triangle the drawn surface has there: each cell is split along this diagonal.
        const float Height = V >= U ? Near + (FarRight - Far) * U + (Far - Near) * V
                                    : Near + (NearRight - Near) * U + (FarRight - NearRight) * V;
        Output = Object->PosY + Height * Object->ScaleY;
        return true;
    }
} // namespace dbc
