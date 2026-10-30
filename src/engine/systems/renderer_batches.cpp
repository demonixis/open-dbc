#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "platform_io.hpp"
#include "renderer_internal.hpp"
#include "sprite_system.hpp"

namespace dbc
{
    [[nodiscard]] SDL_GPUTexture* WhiteGpuTexture(EngineKernel& K)
    {
        if (K.Gpu().WhiteGpuTexture())
        {
            return K.Gpu().WhiteGpuTexture();
        }
        return GraphicsDeviceSystem::GetGpuTextureFromSdlTexture(K.Gpu().WhiteTexture());
    }

    [[nodiscard]] SDL_GPUTexture* ImageGpuTexture(EngineKernel& K, Image& ImageValue, bool TransparentBlack)
    {
        if (!ImageValue.GpuTexture && !ImageValue.SourcePath.empty())
        {
            SDL_Surface* Surface = LoadSurface(ImageValue.SourcePath);
            if (Surface)
            {
                ImageValue.GpuTexture = K.Gpu().CreateGpuTextureFromSurface(Surface);
                SDL_DestroySurface(Surface);
            }
        }
        if (TransparentBlack)
        {
            if (!ImageValue.GpuColorKeyTexture)
            {
                (void)K.ImageStoreRef().EnsureBlackKeyTexture(ImageValue);
            }
            if (ImageValue.GpuColorKeyTexture)
            {
                return ImageValue.GpuColorKeyTexture;
            }
        }
        if (ImageValue.GpuTexture)
        {
            return ImageValue.GpuTexture;
        }
        return GraphicsDeviceSystem::GetGpuTextureFromSdlTexture(ImageValue.Texture);
    }

    // The material rides in the box-colour attributes: ambient, alpha override, specular and emissive.
    void FillSurfaceMaterial(GpuVertex& Out, const SurfaceStyle& Style)
    {
        Out.BoxTrR = PackedChannel(Style.AmbientColor, 16u);
        Out.BoxTrG = PackedChannel(Style.AmbientColor, 8u);
        Out.BoxTrB = PackedChannel(Style.AmbientColor, 0u);
        Out.BoxTrA = Style.AlphaOverride >= 0.0f ? -(Style.AlphaOverride + 1.0f) : 0.0f;
        Out.BoxBrR = PackedChannel(Style.SpecularColor, 16u);
        Out.BoxBrG = PackedChannel(Style.SpecularColor, 8u);
        Out.BoxBrB = PackedChannel(Style.SpecularColor, 0u);
        Out.BoxBrA = Style.SpecularPower;
        Out.BoxBlR = PackedChannel(Style.EmissiveColor, 16u);
        Out.BoxBlG = PackedChannel(Style.EmissiveColor, 8u);
        Out.BoxBlB = PackedChannel(Style.EmissiveColor, 0u);
        Out.BoxBlA = Style.AlphaTestThreshold;
    }

    void AppendGpuTriangle(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                           SDL_GPUTexture* Texture, const float Pos[3][3], const float Normal[3][3],
                           const SDL_FColor Color[3], const SDL_FPoint Uv[3], bool Transparent, bool Is2d,
                           bool Additive, SurfaceStyle Style, const SDL_FColor* BoxColors, bool Sprite,
                           bool AlphaDepthWrite, int MatrixGhostMode, bool DepthWrite, bool DepthTest,
                           const std::array<SDL_GPUTexture*, 7>& AuxiliaryTextures, SDL_GPUTexture* CubeTexture,
                           const std::array<std::array<SDL_FPoint, 3>, 7>* AuxiliaryUvs,
                           const float (*CubeDirections)[3])
    {
        if (Is2d)
            DepthWrite = false;
        SDL_GPUTexture* DrawTexture = Texture ? Texture : WhiteGpuTexture(K);
        if (!DrawTexture)
        {
            return;
        }

        const Uint32 First = static_cast<Uint32>(Vertices.size());
        for (int I = 0; I < 3; ++I)
        {
            GpuVertex Out{};
            Out.X = Pos[I][0];
            Out.Y = Pos[I][1];
            Out.Z = Pos[I][2];
            Out.Nx = Normal[I][0];
            Out.Ny = Normal[I][1];
            Out.Nz = Normal[I][2];
            Out.R = Color[I].r * PackedChannel(Style.DiffuseColor, 16u);
            Out.G = Color[I].g * PackedChannel(Style.DiffuseColor, 8u);
            Out.B = Color[I].b * PackedChannel(Style.DiffuseColor, 0u);
            Out.A = Color[I].a;
            Out.U = Uv[I].x;
            Out.V = Uv[I].y;
            FillSurfaceMaterial(Out, Style);
            for (std::size_t Stage = 0; Stage < 7; ++Stage)
            {
                const SDL_FPoint StageUv = AuxiliaryUvs ? (*AuxiliaryUvs)[Stage][static_cast<std::size_t>(I)] : Uv[I];
                Out.AuxiliaryUvs[Stage][0] = StageUv.x;
                Out.AuxiliaryUvs[Stage][1] = StageUv.y;
            }
            if (CubeDirections)
            {
                Out.CubeDirection[0] = CubeDirections[I][0];
                Out.CubeDirection[1] = CubeDirections[I][1];
                Out.CubeDirection[2] = CubeDirections[I][2];
            }
            if (BoxColors)
            {
                Out.Nx = 0.0f;
                Out.Ny = 0.0f;
                Out.Nz = -1.0f;
                Out.R = BoxColors[0].r;
                Out.G = BoxColors[0].g;
                Out.B = BoxColors[0].b;
                Out.A = BoxColors[0].a;
                Out.BoxTrR = BoxColors[1].r;
                Out.BoxTrG = BoxColors[1].g;
                Out.BoxTrB = BoxColors[1].b;
                Out.BoxTrA = BoxColors[1].a;
                Out.BoxBrR = BoxColors[2].r;
                Out.BoxBrG = BoxColors[2].g;
                Out.BoxBrB = BoxColors[2].b;
                Out.BoxBrA = BoxColors[2].a;
                Out.BoxBlR = BoxColors[3].r;
                Out.BoxBlG = BoxColors[3].g;
                Out.BoxBlB = BoxColors[3].b;
                Out.BoxBlA = BoxColors[3].a;
            }
            Vertices.push_back(Out);
        }

        if (!Batches.empty() && !Batches.back().StaticVertices && !Batches.back().MeshDraw &&
            Batches.back().Texture == DrawTexture && Batches.back().AuxiliaryTextures == AuxiliaryTextures &&
            Batches.back().CubeTexture == CubeTexture && Batches.back().Transparent == Transparent &&
            Batches.back().AlphaDepthWrite == AlphaDepthWrite && Batches.back().Additive == Additive &&
            Batches.back().Is2d == Is2d && Batches.back().Style == Style && Batches.back().Sprite == Sprite &&
            Batches.back().MatrixGhostMode == MatrixGhostMode && Batches.back().DepthTest == DepthTest &&
            Batches.back().DepthWrite == DepthWrite && Batches.back().FirstVertex + Batches.back().VertexCount == First)
        {
            Batches.back().VertexCount += 3;
        }
        else
        {
            Batches.push_back(GpuBatch{DrawTexture, First, 3, Transparent, AlphaDepthWrite, Additive, Is2d, Sprite,
                                       MatrixGhostMode, DepthTest, DepthWrite, Style});
            Batches.back().AuxiliaryTextures = AuxiliaryTextures;
            Batches.back().CubeTexture = CubeTexture;
        }
    }

    void AppendGpuQuad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                       SDL_GPUTexture* Texture, float X, float Y, float W, float H, SDL_FColor Color, float Z,
                       bool Transparent, bool Is2d, SurfaceStyle Style)
    {
        AppendGpuQuadUv(K, Vertices, Batches, Texture, X, Y, W, H, Color, SDL_FPoint{0.0f, 0.0f},
                        SDL_FPoint{1.0f, 1.0f}, Z, Transparent, Is2d, Style);
    }

    void AppendGpuQuadUv(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                         SDL_GPUTexture* Texture, float X, float Y, float W, float H, SDL_FColor Color,
                         const SDL_FPoint& Uv0, const SDL_FPoint& Uv1, float Z, bool Transparent, bool Is2d,
                         SurfaceStyle Style)
    {
        if (W <= 0.0f || H <= 0.0f)
        {
            return;
        }
        const float PosA[3][3]{{X, Y, Z}, {X + W, Y, Z}, {X + W, Y + H, Z}};
        const float ZeroN[3][3]{};
        const SDL_FColor ColA[3]{Color, Color, Color};
        const SDL_FPoint UvA[3]{{Uv0.x, Uv0.y}, {Uv1.x, Uv0.y}, {Uv1.x, Uv1.y}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PosA, ZeroN, ColA, UvA, Transparent, Is2d, false, Style);

        const float PosB[3][3]{{X, Y, Z}, {X + W, Y + H, Z}, {X, Y + H, Z}};
        const SDL_FColor ColB[3]{Color, Color, Color};
        const SDL_FPoint UvB[3]{{Uv0.x, Uv0.y}, {Uv1.x, Uv1.y}, {Uv0.x, Uv1.y}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PosB, ZeroN, ColB, UvB, Transparent, Is2d, false, Style);
    }

    void AppendGpuSpriteQuad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                             SDL_GPUTexture* Texture, const SpriteData& Sprite, float Width, float Height,
                             SDL_FColor Color, bool Transparent)
    {
        if (Width <= 0.0f || Height <= 0.0f)
            return;
        const float Radians = Sprite.Angle * static_cast<float>(M_PI / 180.0);
        const float Cosine = std::cos(Radians);
        const float Sine = std::sin(Radians);
        const auto Position = [&](float LocalX, float LocalY)
        {
            return std::array<float, 3>{Sprite.X + LocalX * Cosine - LocalY * Sine,
                                        Sprite.Y + LocalX * Sine + LocalY * Cosine, 0.0f};
        };
        const float Left = -static_cast<float>(Sprite.OffsetX);
        const float Top = -static_cast<float>(Sprite.OffsetY);
        const auto TopLeft = Position(Left, Top);
        const auto TopRight = Position(Left + Width, Top);
        const auto BottomRight = Position(Left + Width, Top + Height);
        const auto BottomLeft = Position(Left, Top + Height);
        const float PositionsA[3][3]{
            {TopLeft[0], TopLeft[1], 0.0f}, {TopRight[0], TopRight[1], 0.0f}, {BottomRight[0], BottomRight[1], 0.0f}};
        const float PositionsB[3][3]{{TopLeft[0], TopLeft[1], 0.0f},
                                     {BottomRight[0], BottomRight[1], 0.0f},
                                     {BottomLeft[0], BottomLeft[1], 0.0f}};
        const float Normals[3][3]{};
        const SDL_FColor Colors[3]{Color, Color, Color};
        std::array<SpriteData::TextureCoordinate, 4> Coordinates = Sprite.TextureCoordinates;
        if (Sprite.Mirrored)
        {
            std::swap(Coordinates[0], Coordinates[1]);
            std::swap(Coordinates[3], Coordinates[2]);
        }
        if (Sprite.Flipped)
        {
            std::swap(Coordinates[0], Coordinates[3]);
            std::swap(Coordinates[1], Coordinates[2]);
        }
        const SDL_FPoint UvA[3]{{Coordinates[0].U, Coordinates[0].V},
                                {Coordinates[1].U, Coordinates[1].V},
                                {Coordinates[2].U, Coordinates[2].V}};
        const SDL_FPoint UvB[3]{{Coordinates[0].U, Coordinates[0].V},
                                {Coordinates[2].U, Coordinates[2].V},
                                {Coordinates[3].U, Coordinates[3].V}};
        SurfaceStyle Style;
        if (!Sprite.Transparent)
            Style.AlphaOverride = Color.a;
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PositionsA, Normals, Colors, UvA, Transparent, true,
                               Sprite.Additive, Style, nullptr, true);
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PositionsB, Normals, Colors, UvB, Transparent, true,
                               Sprite.Additive, Style, nullptr, true);
    }

    void AppendGpuLine(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches, float X1,
                       float Y1, float X2, float Y2, SDL_FColor Color, float Thickness, float Z, bool Transparent,
                       bool Is2d)
    {
        const float Dx = X2 - X1;
        const float Dy = Y2 - Y1;
        const float Len = std::sqrt(Dx * Dx + Dy * Dy);
        if (Len < 0.0001f)
        {
            dbc::AppendGpuQuad(K, Vertices, Batches, WhiteGpuTexture(K), X1, Y1, Thickness, Thickness, Color, Z,
                               Transparent, Is2d);
            return;
        }

        const float HalfT = std::max(0.5f, Thickness * 0.5f);
        const float Nx = -Dy / Len * HalfT;
        const float Ny = Dx / Len * HalfT;

        const float Px0 = X1 + Nx, Py0 = Y1 + Ny;
        const float Px1 = X2 + Nx, Py1 = Y2 + Ny;
        const float Px2 = X2 - Nx, Py2 = Y2 - Ny;
        const float Px3 = X1 - Nx, Py3 = Y1 - Ny;

        const float PosA[3][3]{{Px0, Py0, Z}, {Px1, Py1, Z}, {Px2, Py2, Z}};
        const float ZeroN[3][3]{};
        const SDL_FColor ColA[3]{Color, Color, Color};
        const SDL_FPoint UvA[3]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, WhiteGpuTexture(K), PosA, ZeroN, ColA, UvA, Transparent, Is2d);

        const float PosB[3][3]{{Px0, Py0, Z}, {Px2, Py2, Z}, {Px3, Py3, Z}};
        const SDL_FColor ColB[3]{Color, Color, Color};
        const SDL_FPoint UvB[3]{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, WhiteGpuTexture(K), PosB, ZeroN, ColB, UvB, Transparent, Is2d);
    }
} // namespace dbc
