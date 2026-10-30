#include "bsp_world.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "matrix_manager.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"
#include "renderer_transforms.hpp"
#include "terrain_mesh.hpp"

namespace dbc
{
    void RenderMatrix(EngineKernel& K, const Matrix& MatrixValue, std::vector<Triangle>& Tris)
    {
        if (MatrixValue.SegX <= 0 || MatrixValue.SegZ <= 0)
            return;
        Image* Img = MatrixValue.ImageId != 0 ? K.ImageStoreRef().GetImage(MatrixValue.ImageId) : nullptr;
        SDL_GPUTexture* GpuTexture = Img ? dbc::ImageGpuTexture(K, *Img, false) : nullptr;

        std::optional<RenderView> CameraView;
        const RenderView* View = &ActiveOrCameraView(K, CameraView);
        const Vec3 Camera = ActiveCameraPosition(K);
        const bool CullBackFaces = MatrixValue.Cull && !K.Renderer().ReflectionViewEnabled();

        const int SegX = MatrixValue.SegX;
        const int SegZ = MatrixValue.SegZ;
        const float StepX = MatrixValue.Width / static_cast<float>(SegX);
        const float StepZ = MatrixValue.Depth / static_cast<float>(SegZ);
        const float SignZ = MatrixValue.IsTerrain ? -1.0f : 1.0f;
        const float HeightScale = MatrixValue.IsTerrain ? MatrixValue.HeightScale : 1.0f;
        const float* Heights = MatrixValue.Heights.data();
        const auto Height = [&](int X, int Z)
        { return Heights[static_cast<size_t>(Z) * static_cast<size_t>(SegX + 1) + static_cast<size_t>(X)]; };

        const int TileCount = std::max(1, MatrixValue.TilesX * MatrixValue.TilesY);
        const float TileW = 1.0f / static_cast<float>(MatrixValue.TilesX);
        const float TileH = 1.0f / static_cast<float>(MatrixValue.TilesY);

        SurfaceStyle Style{};
        Style.Wireframe = MatrixValue.Wireframe;
        Style.Fog = MatrixValue.Fog;
        Style.BlackFog = MatrixValue.Fog && MatrixValue.Ghost;
        Style.Ambient = MatrixValue.Ambient;
        Style.Filter = MatrixValue.Filter;
        Style.TextureAddressModes[0] = MatrixValue.TextureMode;
        Style.TextureMipModes[0] = MatrixValue.MipMode;
        const int AlphaPhase = MatrixValue.Ghost ? 2 : (MatrixValue.Transparency ? 1 : 0);
        const int GhostMode = MatrixValue.Ghost ? MatrixValue.GhostMode : -1;
        const int Priority = MatrixValue.RenderAfterObjects ? 1 : -1;
        const auto FrontFacing = [&](const Vec3& A, const Vec3& B, const Vec3& C)
        {
            if (!CullBackFaces)
                return true;
            const Vec3 Normal = Vec3Cross(Vec3Sub(C, A), Vec3Sub(B, A));
            const Vec3 Center{(A.X + B.X + C.X) / 3.0f, (A.Y + B.Y + C.Y) / 3.0f, (A.Z + B.Z + C.Z) / 3.0f};
            const Vec3 ToCamera{Camera.X - Center.X, Camera.Y - Center.Y, Camera.Z - Center.Z};
            return Vec3Dot(Normal, ToCamera) > 0.0f;
        };

        // First pass: the world bounds of every block decide whether it is visible and how coarse it is.
        const int BlocksX = (SegX + MatrixBlockCells - 1) / MatrixBlockCells;
        const int BlocksZ = (SegZ + MatrixBlockCells - 1) / MatrixBlockCells;
        const bool Lod = MatrixValue.IsTerrain && SegX % MatrixBlockCells == 0 && SegZ % MatrixBlockCells == 0;

        struct Block
        {
            bool Visible = false;
            int Step = 1;
        };

        std::vector<Block> Blocks(static_cast<size_t>(BlocksX) * static_cast<size_t>(BlocksZ));
        for (int Bz = 0; Bz < BlocksZ; ++Bz)
        {
            for (int Bx = 0; Bx < BlocksX; ++Bx)
            {
                const int X0 = Bx * MatrixBlockCells, X1 = std::min(X0 + MatrixBlockCells, SegX);
                const int Z0 = Bz * MatrixBlockCells, Z1 = std::min(Z0 + MatrixBlockCells, SegZ);
                float Low = Height(X0, Z0), High = Low;
                for (int Z = Z0; Z <= Z1; ++Z)
                {
                    for (int X = X0; X <= X1; ++X)
                    {
                        const float H = Height(X, Z);
                        Low = std::min(Low, H);
                        High = std::max(High, H);
                    }
                }
                const float Ya = MatrixValue.PosY + Low * HeightScale, Yb = MatrixValue.PosY + High * HeightScale;
                const Vec3 Minimum{MatrixValue.PosX + static_cast<float>(X0) * StepX, std::min(Ya, Yb),
                                   MatrixValue.PosZ + SignZ * static_cast<float>(SignZ > 0.0f ? Z0 : Z1) * StepZ};
                const Vec3 Maximum{MatrixValue.PosX + static_cast<float>(X1) * StepX, std::max(Ya, Yb),
                                   MatrixValue.PosZ + SignZ * static_cast<float>(SignZ > 0.0f ? Z1 : Z0) * StepZ};
                const Vec3 Center{(Minimum.X + Maximum.X) * 0.5f, (Minimum.Y + Maximum.Y) * 0.5f,
                                  (Minimum.Z + Maximum.Z) * 0.5f};
                const Vec3 Half{Maximum.X - Center.X, Maximum.Y - Center.Y, Maximum.Z - Center.Z};
                const Vec3 ViewCenter = dbc::WorldToViewPoint(K, Center.X, Center.Y, Center.Z);
                Block& Current = Blocks[static_cast<size_t>(Bz) * static_cast<size_t>(BlocksX) + Bx];
                Current.Visible = View->ContainsSphere(glm::vec3(ViewCenter.X, ViewCenter.Y, ViewCenter.Z),
                                                       std::sqrt(Vec3Dot(Half, Half)));
                if (Lod)
                {
                    const Vec3 Nearest{std::clamp(Camera.X, Minimum.X, Maximum.X),
                                       std::clamp(Camera.Y, Minimum.Y, Maximum.Y),
                                       std::clamp(Camera.Z, Minimum.Z, Maximum.Z)};
                    const Vec3 Offset = Vec3Sub(Nearest, Camera);
                    Current.Step = TerrainBlockStep(std::sqrt(Vec3Dot(Offset, Offset)),
                                                    static_cast<float>(MatrixBlockCells) * std::max(StepX, StepZ));
                }
            }
        }
        const auto StepOf = [&](int Bx, int Bz)
        {
            if (Bx < 0 || Bz < 0 || Bx >= BlocksX || Bz >= BlocksZ)
                return 1;
            return Blocks[static_cast<size_t>(Bz) * static_cast<size_t>(BlocksX) + Bx].Step;
        };

        // Second pass: transform each grid vertex of a visible block once, then emit its cells.
        struct GridVertex
        {
            Vec3 World;
            Vec3 View;
            Vec3 Normal;
        };

        std::array<GridVertex, (MatrixBlockCells + 1) * (MatrixBlockCells + 1)> Grid;
        for (int Bz = 0; Bz < BlocksZ; ++Bz)
        {
            for (int Bx = 0; Bx < BlocksX; ++Bx)
            {
                const Block& Current = Blocks[static_cast<size_t>(Bz) * static_cast<size_t>(BlocksX) + Bx];
                if (!Current.Visible)
                    continue;
                const int Step = Current.Step;
                const int X0 = Bx * MatrixBlockCells, X1 = std::min(X0 + MatrixBlockCells, SegX);
                const int Z0 = Bz * MatrixBlockCells, Z1 = std::min(Z0 + MatrixBlockCells, SegZ);
                const int Columns = (X1 - X0) / Step + 1;
                const int Rows = (Z1 - Z0) / Step + 1;
                // An edge shared with a coarser block follows that block's straight edge, so no crack opens.
                const int StepBelow = StepOf(Bx, Bz - 1), StepAbove = StepOf(Bx, Bz + 1);
                const int StepLeft = StepOf(Bx - 1, Bz), StepRight = StepOf(Bx + 1, Bz);
                for (int Row = 0; Row < Rows; ++Row)
                {
                    for (int Column = 0; Column < Columns; ++Column)
                    {
                        const int X = X0 + Column * Step;
                        const int Z = Z0 + Row * Step;
                        float H = Height(X, Z);
                        const int EdgeStepX = Row == 0 ? StepBelow : (Row == Rows - 1 ? StepAbove : 0);
                        const int EdgeStepZ = Column == 0 ? StepLeft : (Column == Columns - 1 ? StepRight : 0);
                        if (EdgeStepX > Step && X % EdgeStepX != 0)
                        {
                            const int Left = X - X % EdgeStepX;
                            const float T = static_cast<float>(X - Left) / static_cast<float>(EdgeStepX);
                            H = Height(Left, Z) + (Height(Left + EdgeStepX, Z) - Height(Left, Z)) * T;
                        }
                        else if (EdgeStepZ > Step && Z % EdgeStepZ != 0)
                        {
                            const int Below = Z - Z % EdgeStepZ;
                            const float T = static_cast<float>(Z - Below) / static_cast<float>(EdgeStepZ);
                            H = Height(X, Below) + (Height(X, Below + EdgeStepZ) - Height(X, Below)) * T;
                        }
                        GridVertex& Vertex = Grid[static_cast<size_t>(Row) * (MatrixBlockCells + 1) + Column];
                        Vertex.World = {MatrixValue.PosX + static_cast<float>(X) * StepX,
                                        MatrixValue.PosY + H * HeightScale,
                                        MatrixValue.PosZ + SignZ * static_cast<float>(Z) * StepZ};
                        Vertex.View = dbc::WorldToViewPoint(K, Vertex.World.X, Vertex.World.Y, Vertex.World.Z);
                        Vertex.Normal = Vec3{};
                        if (MatrixValue.Light)
                        {
                            const Vec3 Normal = MatrixVertexNormal(MatrixValue, X, Z, StepX, StepZ);
                            Vertex.Normal = dbc::WorldToViewDirection(K, Normal.X, Normal.Y, Normal.Z);
                        }
                    }
                }

                for (int Row = 0; Row + 1 < Rows; ++Row)
                {
                    for (int Column = 0; Column + 1 < Columns; ++Column)
                    {
                        const int X = X0 + Column * Step;
                        const int Z = Z0 + Row * Step;
                        const auto At = [&](int C, int R) -> const GridVertex&
                        { return Grid[static_cast<size_t>(R) * (MatrixBlockCells + 1) + C]; };
                        const GridVertex& P00 = At(Column, Row);
                        const GridVertex& P10 = At(Column + 1, Row);
                        const GridVertex& P11 = At(Column + 1, Row + 1);
                        const GridVertex& P01 = At(Column, Row + 1);

                        float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
                        if (MatrixValue.IsTerrain)
                        {
                            U0 = static_cast<float>(X) / static_cast<float>(SegX) * MatrixValue.TilesX;
                            V0 = static_cast<float>(Z) / static_cast<float>(SegZ) * MatrixValue.TilesY;
                            U1 = static_cast<float>(X + Step) / static_cast<float>(SegX) * MatrixValue.TilesX;
                            V1 = static_cast<float>(Z + Step) / static_cast<float>(SegZ) * MatrixValue.TilesY;
                        }
                        else
                        {
                            const int TileId =
                                std::clamp(MatrixManager::MatrixTile(MatrixValue, X, Z) - 1, 0, TileCount - 1);
                            U0 = static_cast<float>(TileId % MatrixValue.TilesX) * TileW + MatrixValue.TrimX;
                            V0 = static_cast<float>(TileId / MatrixValue.TilesX) * TileH + MatrixValue.TrimY;
                            U1 = static_cast<float>(TileId % MatrixValue.TilesX + 1) * TileW - MatrixValue.TrimX;
                            V1 = static_cast<float>(TileId / MatrixValue.TilesX + 1) * TileH - MatrixValue.TrimY;
                        }

                        // White vertex colour: the shader lights the surface from normals and uniforms.
                        const auto Corner = [](const GridVertex& P, float U, float V)
                        {
                            ViewVertex Result{};
                            Result.X = P.View.X;
                            Result.Y = P.View.Y;
                            Result.Z = P.View.Z;
                            Result.Nx = P.Normal.X;
                            Result.Ny = P.Normal.Y;
                            Result.Nz = P.Normal.Z;
                            Result.Uv = SDL_FPoint{U, V};
                            return Result;
                        };
                        const ViewVertex Q0 = Corner(P00, U0, V0);
                        const ViewVertex Q1 = Corner(P10, U1, V0);
                        const ViewVertex Q2 = Corner(P11, U1, V1);
                        const ViewVertex Q3 = Corner(P01, U0, V1);
                        if (MatrixValue.IsTerrain)
                        {
                            if (FrontFacing(P00.World, P11.World, P10.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q2, Q1, GpuTexture, false, Tris, false, Style,
                                                               AlphaPhase, false, 0.0f, GhostMode, Priority);
                            if (FrontFacing(P00.World, P01.World, P11.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q3, Q2, GpuTexture, false, Tris, false, Style,
                                                               AlphaPhase, false, 0.0f, GhostMode, Priority);
                        }
                        else
                        {
                            if (FrontFacing(P00.World, P10.World, P11.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q1, Q2, GpuTexture, false, Tris, false, Style,
                                                               AlphaPhase, false, 0.0f, GhostMode, Priority);
                            if (FrontFacing(P00.World, P11.World, P01.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q2, Q3, GpuTexture, false, Tris, false, Style,
                                                               AlphaPhase, false, 0.0f, GhostMode, Priority);
                        }
                    }
                }
            }
        }
    }

    // DBPro draws only the leaves the BSP camera's leaf can see (PVS) that also meet the view frustum.
    void RenderBspWorld(EngineKernel& K, std::vector<Triangle>& Tris)
    {
        const BspWorldSystem& World = K.BspWorld();
        if (!World.HasWorld())
            return;
        const BspMap& Map = World.Map();
        const bool Reflection = K.Renderer().ReflectionViewEnabled();
        const int Leaf = FindBspLeaf(Map, Reflection ? ActiveCameraPosition(K) : World.CullingPosition());
        if (Leaf < 0)
            return;
        std::vector<uint8_t> Visible;
        DecodeBspPvs(Map, Leaf, Visible);

        std::optional<RenderView> CameraView;
        const RenderView* View = &ActiveOrCameraView(K, CameraView);
        const Vec3 Camera = ActiveCameraPosition(K);

        std::vector<SDL_GPUTexture*> GpuTextures(Map.TextureNames.size(), nullptr);
        for (std::size_t Index = 0; Index < Map.TextureNames.size(); ++Index)
        {
            if (Image* Img = K.ImageStoreRef().GetImage(World.TextureImage(static_cast<int>(Index))))
            {
                GpuTextures[Index] = dbc::ImageGpuTexture(K, *Img, false);
            }
        }

        const SurfaceStyle Style{};
        for (std::size_t LeafIndex = 0; LeafIndex < Map.Leaves.size(); ++LeafIndex)
        {
            if (!Visible[LeafIndex])
                continue;
            const BspLeaf& LeafValue = Map.Leaves[LeafIndex];
            const Vec3 Center{(LeafValue.BoxMin.X + LeafValue.BoxMax.X) * 0.5f,
                              (LeafValue.BoxMin.Y + LeafValue.BoxMax.Y) * 0.5f,
                              (LeafValue.BoxMin.Z + LeafValue.BoxMax.Z) * 0.5f};
            const Vec3 Half = Vec3Sub(LeafValue.BoxMax, Center);
            const Vec3 ViewCenter = dbc::WorldToViewPoint(K, Center.X, Center.Y, Center.Z);
            if (!View->ContainsSphere(glm::vec3(ViewCenter.X, ViewCenter.Y, ViewCenter.Z),
                                      std::sqrt(Vec3Dot(Half, Half))))
                continue;
            for (int32_t PolygonIndex = LeafValue.FirstPolygon; PolygonIndex < LeafValue.EndPolygon; ++PolygonIndex)
            {
                const BspPolygon& Polygon = Map.Polygons[static_cast<std::size_t>(PolygonIndex)];
                if (Polygon.Vertices.empty())
                    continue;
                // Stored normals face open space; DBPro culled the other side except in reflections.
                const Vec3 ToCamera = Vec3Sub(Camera, Polygon.Vertices.front().Position);
                if (!Reflection && Vec3Dot(Polygon.Normal, ToCamera) <= 0.0f)
                    continue;
                const Vec3 Normal = dbc::WorldToViewDirection(K, Polygon.Normal.X, Polygon.Normal.Y, Polygon.Normal.Z);
                std::array<ViewVertex, 3> Corners{};
                for (std::size_t Index = 0; Index + 2 < Polygon.Indices.size(); Index += 3)
                {
                    for (std::size_t Corner = 0; Corner < 3; ++Corner)
                    {
                        const BspVertex& Source = Polygon.Vertices[Polygon.Indices[Index + Corner]];
                        const Vec3 Point =
                            dbc::WorldToViewPoint(K, Source.Position.X, Source.Position.Y, Source.Position.Z);
                        ViewVertex& Target = Corners[Corner];
                        Target.X = Point.X;
                        Target.Y = Point.Y;
                        Target.Z = Point.Z;
                        Target.Nx = Normal.X;
                        Target.Ny = Normal.Y;
                        Target.Nz = Normal.Z;
                        Target.Uv = SDL_FPoint{Source.U, Source.V};
                        Target.Color = SDL_FColor{static_cast<float>((Source.Color >> 16) & 0xFFu) / 255.0f,
                                                  static_cast<float>((Source.Color >> 8) & 0xFFu) / 255.0f,
                                                  static_cast<float>(Source.Color & 0xFFu) / 255.0f,
                                                  static_cast<float>((Source.Color >> 24) & 0xFFu) / 255.0f};
                    }
                    dbc::AppendClippedViewTriangle(K, Corners[0], Corners[1], Corners[2], GpuTextures[Polygon.Texture],
                                                   false, Tris, false, Style);
                }
            }
        }
    }

    void RenderObjectBounds(EngineKernel& K, const Object3D& Object, std::vector<Triangle>& Tris)
    {
        if (!Object.DrawBounds || Object.Meshes.empty() || Object.LockToScreen)
            return;

        Vec3 Minimum{};
        Vec3 Maximum{};
        bool First = true;
        for (const Mesh& MeshValue : Object.Meshes)
        {
            for (const Vec3& Vertex : MeshValue.Geometry().Vertices)
            {
                Vec3 Local = ApplyAiTransformPoint(MeshValue.Geometry().BindTransform, Vertex);
                Local.X *= MeshValue.LimbScaleX;
                Local.Y *= MeshValue.LimbScaleY;
                Local.Z *= MeshValue.LimbScaleZ;
                Local = RotateX(Local, MeshValue.LimbRotX);
                Local = RotateY(Local, MeshValue.LimbRotY);
                Local = RotateZ(Local, MeshValue.LimbRotZ);
                Local.X += MeshValue.LimbOffX;
                Local.Y += MeshValue.LimbOffY;
                Local.Z += MeshValue.LimbOffZ;
                if (First)
                {
                    Minimum = Maximum = Local;
                    First = false;
                }
                else
                {
                    Minimum.X = std::min(Minimum.X, Local.X);
                    Minimum.Y = std::min(Minimum.Y, Local.Y);
                    Minimum.Z = std::min(Minimum.Z, Local.Z);
                    Maximum.X = std::max(Maximum.X, Local.X);
                    Maximum.Y = std::max(Maximum.Y, Local.Y);
                    Maximum.Z = std::max(Maximum.Z, Local.Z);
                }
            }
        }
        if (First)
            return;

        const Vec3 Center{(Minimum.X + Maximum.X) * 0.5f, (Minimum.Y + Maximum.Y) * 0.5f,
                          (Minimum.Z + Maximum.Z) * 0.5f};
        const float Width = std::max(Maximum.X - Minimum.X, 0.0001f);
        const float Height = std::max(Maximum.Y - Minimum.Y, 0.0001f);
        const float Depth = std::max(Maximum.Z - Minimum.Z, 0.0001f);
        const float Radius = std::sqrt(Width * Width + Height * Height + Depth * Depth) * 0.5f;

        SurfaceStyle Style;
        Style.Wireframe = true;
        Style.Fog = false;
        Style.Ambient = false;
        const auto AppendMesh = [&](const Mesh& BoundsMesh, const SDL_FColor& Color)
        {
            for (std::size_t Index = 0; Index + 2 < BoundsMesh.Geometry().Indices.size(); Index += 3)
            {
                ViewVertex Vertices[3]{};
                bool Valid = true;
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t VertexIndex =
                        BoundsMesh.Geometry().Indices[Index + static_cast<std::size_t>(Corner)];
                    if (VertexIndex >= BoundsMesh.Geometry().Vertices.size())
                    {
                        Valid = false;
                        break;
                    }
                    const Vec3& Source = BoundsMesh.Geometry().Vertices[VertexIndex];
                    const Vec3 World = TransformObjectSpacePoint(
                        K, Object, Vec3{Center.X + Source.X, Center.Y + Source.Y, Center.Z + Source.Z});
                    const Vec3 View = dbc::WorldToViewPoint(K, World.X, World.Y, World.Z);
                    Vertices[Corner].X = View.X;
                    Vertices[Corner].Y = View.Y;
                    Vertices[Corner].Z = View.Z;
                    Vertices[Corner].Color = Color;
                }
                if (Valid)
                {
                    dbc::AppendClippedViewTriangle(K, Vertices[0], Vertices[1], Vertices[2], nullptr, false, Tris,
                                                   false, Style, 0, false, -0.0001f, -1, 2, false, true);
                }
            }
        };

        AppendMesh(MakeBoxMesh(Width, Height, Depth), SDL_FColor{1.0f, 0.25f, 0.1f, 1.0f});
        if (!Object.BoundsBoxOnly)
            AppendMesh(MakeSphereMesh(Radius, 8, 12), SDL_FColor{0.2f, 1.0f, 0.35f, 1.0f});
    }

    void RenderParticles(EngineKernel& K, std::vector<Triangle>& WorldTris)
    {
        K.Objects().ForEachParticles(
            [&](int /*id*/, const ParticleEmitter& Emitter)
            {
                if (!Emitter.Visible)
                    return;

                SDL_GPUTexture* GpuTexture = nullptr;
                if (Image* ParticleImage = K.ImageStoreRef().GetImage(Emitter.ImageId))
                {
                    GpuTexture = dbc::ImageGpuTexture(K, *ParticleImage, false);
                }

                SurfaceStyle Style;
                Style.Fog = true;
                Style.BlackFog = true;
                Style.Ambient = false;
                Style.Filter = false;
                Style.TextureMipModes[0] = 1;
                Style.AlphaOverride = Emitter.GhostPercentage >= 0.0f ? Emitter.GhostPercentage / 100.0f : -1.0f;
                const int GhostMode = Emitter.Ghost && Emitter.GhostPercentage < 0.0f && Emitter.GhostMode != 5
                                          ? std::clamp(Emitter.GhostMode < 0 ? 0 : Emitter.GhostMode, 0, 4)
                                          : -1;
                const Mat3 Rotation = Mat3FromEulerXyz(Emitter.RotX, Emitter.RotY, Emitter.RotZ);
                const float HalfSize = Emitter.Radius * 0.05f;

                for (const ParticleInstance& Particle : Emitter.Particles)
                {
                    Vec3 LocalPosition{Particle.Position.X * Emitter.Radius, Particle.Position.Y * Emitter.Radius,
                                       Particle.Position.Z * Emitter.Radius};
                    LocalPosition = Mat3Apply(LocalPosition, Rotation);
                    Vec3 WorldPosition{Emitter.PosX + LocalPosition.X, Emitter.PosY + LocalPosition.Y,
                                       Emitter.PosZ + LocalPosition.Z};
                    Vec3 WorldVelocity{Particle.Velocity.X * Emitter.Radius, Particle.Velocity.Y * Emitter.Radius,
                                       Particle.Velocity.Z * Emitter.Radius};
                    WorldVelocity = Mat3Apply(WorldVelocity, Rotation);
                    const float SpeedSquared = Vec3Dot(WorldVelocity, WorldVelocity);
                    const int Steps = SpeedSquared < 1.0f     ? 2
                                      : SpeedSquared < 4.0f   ? 3
                                      : SpeedSquared < 9.0f   ? 4
                                      : SpeedSquared < 12.25f ? 5
                                      : SpeedSquared < 16.0f  ? 6
                                      : SpeedSquared < 20.25f ? 7
                                                              : 8;
                    const Vec3 TrailStep{-WorldVelocity.X * 0.04f / static_cast<float>(Steps),
                                         -WorldVelocity.Y * 0.04f / static_cast<float>(Steps),
                                         -WorldVelocity.Z * 0.04f / static_cast<float>(Steps)};
                    SDL_FColor Color = Particle.Diffuse;

                    for (int StepIndex = 0; StepIndex < Steps; ++StepIndex)
                    {
                        const Vec3 Center = dbc::WorldToViewPoint(K, WorldPosition.X, WorldPosition.Y, WorldPosition.Z);
                        const ViewVertex TopLeft{
                            Center.X - HalfSize, Center.Y + HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f}, Color};
                        const ViewVertex TopRight{
                            Center.X + HalfSize, Center.Y + HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {1.0f, 0.0f}, Color};
                        const ViewVertex BottomRight{
                            Center.X + HalfSize, Center.Y - HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {1.0f, 1.0f}, Color};
                        const ViewVertex BottomLeft{
                            Center.X - HalfSize, Center.Y - HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {0.0f, 1.0f}, Color};
                        dbc::AppendClippedViewTriangle(K, TopLeft, BottomRight, TopRight, GpuTexture, Emitter.Ghost,
                                                       WorldTris, false, Style, -1, false, 0.0f, GhostMode, 1, false);
                        dbc::AppendClippedViewTriangle(K, TopLeft, BottomLeft, BottomRight, GpuTexture, Emitter.Ghost,
                                                       WorldTris, false, Style, -1, false, 0.0f, GhostMode, 1, false);
                        WorldPosition.X += TrailStep.X;
                        WorldPosition.Y += TrailStep.Y;
                        WorldPosition.Z += TrailStep.Z;
                    }
                }
            });
    }
} // namespace dbc
