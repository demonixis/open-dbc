#include "camera_system.hpp"
#include "core_system.hpp"
#include "draw_system.hpp"
#include "engine_kernel.hpp"
#include "light_manager.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    [[nodiscard]] Vec3 ActiveCameraPosition(EngineKernel& K)
    {
        const RenderView* View = K.Renderer().ActiveView();
        const Vec3 Position = View ? Vec3{View->CameraPosition.x, View->CameraPosition.y, View->CameraPosition.z}
                                   : Vec3{K.Camera().PositionX(), K.Camera().PositionY(), K.Camera().PositionZ()};
        return K.Renderer().ReflectionViewEnabled() ? K.Renderer().ReflectWorldPoint(Position) : Position;
    }

    [[nodiscard]] float FogFactor(EngineKernel& K, float Z)
    {
        if (!K.Draw().FogOn())
        {
            return 0.0f;
        }
        const float Start = K.Draw().FogStartDistance();
        const float Range = K.Draw().FogDistance() - Start;
        if (std::abs(Range) <= std::numeric_limits<float>::epsilon())
            return Z >= Start ? 1.0f : 0.0f;
        return std::clamp((Z - Start) / Range, 0.0f, 1.0f);
    }

    [[nodiscard]] Vec3 WorldToViewPoint(EngineKernel& K, float Wx, float Wy, float Wz)
    {
        const auto& Cam = K.Camera();
        if (K.Renderer().ReflectionViewEnabled())
        {
            const Vec3 Reflected = K.Renderer().ReflectWorldPoint(Vec3{Wx, Wy, Wz});
            Wx = Reflected.X;
            Wy = Reflected.Y;
            Wz = Reflected.Z;
        }
        if (const RenderView* View = K.Renderer().ActiveView())
        {
            const glm::vec4 Point = View->WorldToView * glm::vec4(Wx, Wy, Wz, 1.0f);
            return {Point.x, Point.y, Point.z};
        }
        float X = Wx - Cam.PositionX();
        float Y = Wy - Cam.PositionY();
        float Z = Wz - Cam.PositionZ();

        if (Cam.IsZyxRotation())
        {
            // ZYX forward → inverse: X^-1, Y^-1, Z^-1
            CamInvX(Y, Z, Cam.AngleX());
            CamInvY(X, Z, Cam.AngleY());
            CamInvZ(X, Y, Cam.AngleZ());
        }
        else
        {
            // Default order: Y, X, Z (matches original DBC FPS convention)
            CamInvY(X, Z, Cam.AngleY());
            CamInvX(Y, Z, Cam.AngleX());
            CamInvZ(X, Y, Cam.AngleZ());
        }

        return Vec3{X, Y, Z};
    }

    [[nodiscard]] Vec3 WorldToViewDirection(EngineKernel& K, float Dx, float Dy, float Dz)
    {
        const auto& Cam = K.Camera();
        if (K.Renderer().ReflectionViewEnabled())
        {
            const Vec3 Reflected = K.Renderer().ReflectWorldDirection(Vec3{Dx, Dy, Dz});
            Dx = Reflected.X;
            Dy = Reflected.Y;
            Dz = Reflected.Z;
        }
        if (const RenderView* View = K.Renderer().ActiveView())
        {
            const glm::vec4 Direction = View->WorldToView * glm::vec4(Dx, Dy, Dz, 0.0f);
            return {Direction.x, Direction.y, Direction.z};
        }
        float X = Dx, Y = Dy, Z = Dz;

        if (Cam.IsZyxRotation())
        {
            CamInvX(Y, Z, Cam.AngleX());
            CamInvY(X, Z, Cam.AngleY());
            CamInvZ(X, Y, Cam.AngleZ());
        }
        else
        {
            CamInvY(X, Z, Cam.AngleY());
            CamInvX(Y, Z, Cam.AngleX());
            CamInvZ(X, Y, Cam.AngleZ());
        }

        return Vec3{X, Y, Z};
    }

    [[nodiscard]] GpuFragmentUniforms BuildFragmentUniforms(EngineKernel& K)
    {
        GpuFragmentUniforms Ubo{};

        Ubo.Flags[0] = 1.0f; // use_lighting (overridden per-vertex via zero normal)
        Ubo.Flags[1] = K.Draw().FogOn() ? 1.0f : 0.0f;
        Ubo.Flags[2] = 1.0f;  // use_ambient
        Ubo.Flags[3] = 32.0f; // retained custom-effect ABI value; stock materials carry their own power

        {
            const int AmbCol = K.Lights().AmbientColor();
            const float AmbLvl = K.Lights().AmbientLight();
            const SDL_FColor Ac = UnpackColor(AmbCol);
            Ubo.AmbientColor[0] = Ac.r;
            Ubo.AmbientColor[1] = Ac.g;
            Ubo.AmbientColor[2] = Ac.b;
            Ubo.AmbientColor[3] = std::clamp(AmbLvl, 0.0f, 1.0f);
        }

        if (K.Draw().FogOn())
        {
            const SDL_FColor Fc = UnpackColor(K.Draw().FogColor());
            Ubo.FogColorDist[0] = Fc.r;
            Ubo.FogColorDist[1] = Fc.g;
            Ubo.FogColorDist[2] = Fc.b;
            Ubo.FogColorDist[3] = K.Draw().FogDistance();
            Ubo.DirLightDirection[3] = K.Draw().FogStartDistance();
        }

        int NumPoint = 0;
        int NumSpot = 0;
        bool HasDir = false;

        K.Lights().ForEachLight(
            [&](int /*id*/, const Light3D& Light)
            {
                if (!Light.Visible)
                    return;

                if (Light.Type == 3 && !HasDir)
                {
                    const Vec3 Vd = dbc::WorldToViewDirection(K, Light.DirX, Light.DirY, Light.DirZ);
                    Ubo.DirLightDirection[0] = Vd.X;
                    Ubo.DirLightDirection[1] = Vd.Y;
                    Ubo.DirLightDirection[2] = Vd.Z;
                    Ubo.DirLightDirection[3] = K.Draw().FogStartDistance();
                    const SDL_FColor Lc = UnpackColor(Light.Color);
                    Ubo.DirLightColor[0] = Lc.r;
                    Ubo.DirLightColor[1] = Lc.g;
                    Ubo.DirLightColor[2] = Lc.b;
                    Ubo.DirLightColor[3] = 1.0f;
                    HasDir = true;
                }
                else if (Light.Type == 1 && NumPoint < 12)
                {
                    const Vec3 Vp = dbc::WorldToViewPoint(K, Light.PosX, Light.PosY, Light.PosZ);
                    const SDL_FColor Lc = UnpackColor(Light.Color);
                    const int Base = NumPoint * 2;
                    Ubo.PointLights[Base][0] = Vp.X;
                    Ubo.PointLights[Base][1] = Vp.Y;
                    Ubo.PointLights[Base][2] = Vp.Z;
                    Ubo.PointLights[Base][3] = Light.Range;
                    Ubo.PointLights[Base + 1][0] = Lc.r;
                    Ubo.PointLights[Base + 1][1] = Lc.g;
                    Ubo.PointLights[Base + 1][2] = Lc.b;
                    Ubo.PointLights[Base + 1][3] = 1.0f;
                    ++NumPoint;
                }
                else if (Light.Type == 2 && NumSpot < 6)
                {
                    const Vec3 Vp = dbc::WorldToViewPoint(K, Light.PosX, Light.PosY, Light.PosZ);
                    const Vec3 Vd = dbc::WorldToViewDirection(K, Light.DirX, Light.DirY, Light.DirZ);
                    const SDL_FColor Lc = UnpackColor(Light.Color);
                    const float CosInner = std::cos(Light.SpotInner * static_cast<float>(M_PI / 180.0));
                    const float CosOuter = std::cos(Light.SpotOuter * static_cast<float>(M_PI / 180.0));
                    const int Base = NumSpot * 3;
                    Ubo.SpotLights[Base][0] = Vp.X;
                    Ubo.SpotLights[Base][1] = Vp.Y;
                    Ubo.SpotLights[Base][2] = Vp.Z;
                    Ubo.SpotLights[Base][3] = Light.Range;
                    Ubo.SpotLights[Base + 1][0] = Vd.X;
                    Ubo.SpotLights[Base + 1][1] = Vd.Y;
                    Ubo.SpotLights[Base + 1][2] = Vd.Z;
                    Ubo.SpotLights[Base + 1][3] = CosInner;
                    Ubo.SpotLights[Base + 2][0] = Lc.r;
                    Ubo.SpotLights[Base + 2][1] = Lc.g;
                    Ubo.SpotLights[Base + 2][2] = Lc.b;
                    Ubo.SpotLights[Base + 2][3] = CosOuter;
                    ++NumSpot;
                }
            });

        Ubo.LightCounts[0] = NumPoint;
        Ubo.LightCounts[1] = NumSpot;
        Ubo.LightCounts[2] = 0;
        Ubo.LightCounts[3] = 0;

        return Ubo;
    }

    [[nodiscard]] bool ProjectViewPoint(EngineKernel& K, float X, float Y, float Z, SDL_FPoint& OutPos, float& OutDepth)
    {
        OutDepth = Z;
        if (const RenderView* View = K.Renderer().ActiveView())
            return View->Project({X, Y, Z}, OutPos);
        return K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight()).Project({X, Y, Z}, OutPos);
    }

    [[nodiscard]] bool ProjectPoint(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth)
    {
        const Vec3 View = dbc::WorldToViewPoint(K, Wx, Wy, Wz);
        return dbc::ProjectViewPoint(K, View.X, View.Y, View.Z, OutPos, OutDepth);
    }

    [[nodiscard]] bool ProjectPointLocked(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos,
                                          float& OutDepth)
    {
        return dbc::ProjectViewPoint(K, Wx, Wy, Wz, OutPos, OutDepth);
    }

    [[nodiscard]] ViewVertex InterpolateViewVertex(const ViewVertex& A, const ViewVertex& B, float T)
    {
        const float ClampedT = std::clamp(T, 0.0f, 1.0f);
        ViewVertex Out{};
        Out.X = A.X + (B.X - A.X) * ClampedT;
        Out.Y = A.Y + (B.Y - A.Y) * ClampedT;
        Out.Z = A.Z + (B.Z - A.Z) * ClampedT;
        Out.Nx = A.Nx + (B.Nx - A.Nx) * ClampedT;
        Out.Ny = A.Ny + (B.Ny - A.Ny) * ClampedT;
        Out.Nz = A.Nz + (B.Nz - A.Nz) * ClampedT;
        Out.Uv.x = A.Uv.x + (B.Uv.x - A.Uv.x) * ClampedT;
        Out.Uv.y = A.Uv.y + (B.Uv.y - A.Uv.y) * ClampedT;
        for (std::size_t Stage = 0; Stage < Out.AuxiliaryUvs.size(); ++Stage)
        {
            Out.AuxiliaryUvs[Stage].x =
                A.AuxiliaryUvs[Stage].x + (B.AuxiliaryUvs[Stage].x - A.AuxiliaryUvs[Stage].x) * ClampedT;
            Out.AuxiliaryUvs[Stage].y =
                A.AuxiliaryUvs[Stage].y + (B.AuxiliaryUvs[Stage].y - A.AuxiliaryUvs[Stage].y) * ClampedT;
        }
        Out.CubeX = A.CubeX + (B.CubeX - A.CubeX) * ClampedT;
        Out.CubeY = A.CubeY + (B.CubeY - A.CubeY) * ClampedT;
        Out.CubeZ = A.CubeZ + (B.CubeZ - A.CubeZ) * ClampedT;
        Out.Color.r = A.Color.r + (B.Color.r - A.Color.r) * ClampedT;
        Out.Color.g = A.Color.g + (B.Color.g - A.Color.g) * ClampedT;
        Out.Color.b = A.Color.b + (B.Color.b - A.Color.b) * ClampedT;
        Out.Color.a = A.Color.a + (B.Color.a - A.Color.a) * ClampedT;
        return Out;
    }

    [[nodiscard]] int ClipPolygonAgainstPlane(const ViewVertex* Input, int Count, ViewVertex* Output,
                                              const glm::vec4& Plane)
    {
        if (Count <= 0)
            return 0;
        const auto Distance = [&](const ViewVertex& Vertex)
        { return glm::dot(Plane, glm::vec4(Vertex.X, Vertex.Y, Vertex.Z, 1.0f)); };
        int OutputCount = 0;
        ViewVertex Previous = Input[Count - 1];
        float PreviousDistance = Distance(Previous);
        for (int Index = 0; Index < Count; ++Index)
        {
            const ViewVertex Current = Input[Index];
            const float CurrentDistance = Distance(Current);
            if ((PreviousDistance >= 0.0f) != (CurrentDistance >= 0.0f))
            {
                const float Ratio = PreviousDistance / (PreviousDistance - CurrentDistance);
                Output[OutputCount++] = InterpolateViewVertex(Previous, Current, Ratio);
            }
            if (CurrentDistance >= 0.0f)
                Output[OutputCount++] = Current;
            Previous = Current;
            PreviousDistance = CurrentDistance;
        }
        return OutputCount;
    }

    const RenderView& ActiveOrCameraView(EngineKernel& K, std::optional<RenderView>& Storage)
    {
        if (const RenderView* View = K.Renderer().ActiveView())
            return *View;
        return Storage.emplace(K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight()));
    }

    void AppendClippedViewTriangle(EngineKernel& K, const ViewVertex& V0, const ViewVertex& V1, const ViewVertex& V2,
                                   SDL_GPUTexture* GpuTexture, bool Transparent, std::vector<Triangle>& Tris,
                                   bool Additive, const SurfaceStyle& Style, int AlphaPhase, bool VeryEarly,
                                   float DepthBias, int MatrixGhostMode, int RenderPriority, bool DepthWrite,
                                   bool DepthTest, const std::array<SDL_GPUTexture*, 7>& AuxiliaryGpuTextures,
                                   SDL_GPUTexture* CubeGpuTexture, std::optional<uint32_t> StyleHash)
    {
        std::optional<RenderView> CameraView;
        const RenderView* View = &ActiveOrCameraView(K, CameraView);

        ViewVertex ClippedA[8]{};
        ViewVertex ClippedB[8]{};
        ClippedA[0] = V0;
        ClippedA[1] = V1;
        ClippedA[2] = V2;

        int Count = 3;
        Count = ClipPolygonAgainstPlane(ClippedA, Count, ClippedB, View->ClipPlane(4, Additive));
        if (Count < 3)
        {
            return;
        }
        Count = ClipPolygonAgainstPlane(ClippedB, Count, ClippedA, View->ClipPlane(5, Additive));
        if (Count < 3)
        {
            return;
        }

        for (int I = 1; I + 1 < Count; ++I)
        {
            const ViewVertex A = ClippedA[0];
            const ViewVertex B = ClippedA[I];
            const ViewVertex C = ClippedA[I + 1];

            Triangle Tri;
            Tri.GpuTexture = GpuTexture;
            Tri.AuxiliaryGpuTextures = AuxiliaryGpuTextures;
            Tri.CubeGpuTexture = CubeGpuTexture;
            Tri.Depth = (A.Z + B.Z + C.Z) / 3.0f + DepthBias;
            const bool AutomaticTransparency =
                Transparent || A.Color.a < 0.999f || B.Color.a < 0.999f || C.Color.a < 0.999f;
            Tri.Transparent = AlphaPhase == 2 || (AlphaPhase < 0 && AutomaticTransparency);
            Tri.AlphaDepthWrite = AlphaPhase == 1;
            Tri.Additive = Additive;
            Tri.VeryEarly = VeryEarly;
            Tri.MatrixGhostMode = MatrixGhostMode;
            Tri.DepthTest = DepthTest;
            Tri.DepthWrite = DepthWrite && !Additive;
            Tri.RenderPriority = RenderPriority;
            Tri.Is2d = false;
            Tri.Style = Style;
            Tri.Pos[0][0] = A.X;
            Tri.Pos[0][1] = A.Y;
            Tri.Pos[0][2] = A.Z;
            Tri.Pos[1][0] = B.X;
            Tri.Pos[1][1] = B.Y;
            Tri.Pos[1][2] = B.Z;
            Tri.Pos[2][0] = C.X;
            Tri.Pos[2][1] = C.Y;
            Tri.Pos[2][2] = C.Z;
            Tri.Normal[0][0] = A.Nx;
            Tri.Normal[0][1] = A.Ny;
            Tri.Normal[0][2] = A.Nz;
            Tri.Normal[1][0] = B.Nx;
            Tri.Normal[1][1] = B.Ny;
            Tri.Normal[1][2] = B.Nz;
            Tri.Normal[2][0] = C.Nx;
            Tri.Normal[2][1] = C.Ny;
            Tri.Normal[2][2] = C.Nz;
            Tri.Color[0] = A.Color;
            Tri.Color[1] = B.Color;
            Tri.Color[2] = C.Color;
            Tri.Uv[0] = A.Uv;
            Tri.Uv[1] = B.Uv;
            Tri.Uv[2] = C.Uv;
            for (std::size_t Stage = 0; Stage < Tri.AuxiliaryUvs.size(); ++Stage)
            {
                Tri.AuxiliaryUvs[Stage][0] = A.AuxiliaryUvs[Stage];
                Tri.AuxiliaryUvs[Stage][1] = B.AuxiliaryUvs[Stage];
                Tri.AuxiliaryUvs[Stage][2] = C.AuxiliaryUvs[Stage];
            }
            Tri.CubeDirection[0][0] = A.CubeX;
            Tri.CubeDirection[0][1] = A.CubeY;
            Tri.CubeDirection[0][2] = A.CubeZ;
            Tri.CubeDirection[1][0] = B.CubeX;
            Tri.CubeDirection[1][1] = B.CubeY;
            Tri.CubeDirection[1][2] = B.CubeZ;
            Tri.CubeDirection[2][0] = C.CubeX;
            Tri.CubeDirection[2][1] = C.CubeY;
            Tri.CubeDirection[2][2] = C.CubeZ;
            if (!StyleHash)
                StyleHash = SurfaceStyleHash(Style);
            Tri.StateHash = DrawStateHash(Tri, *StyleHash);
            Tris.push_back(Tri);
        }
    }
} // namespace dbc
