#include "camera_system.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "light_manager.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"
#include "renderer_transforms.hpp"

namespace dbc
{
    [[nodiscard]] SurfaceStyle BuildSurfaceStyle(const Object3D& Object, const Mesh& MeshValue)
    {
        SurfaceStyle Style;
        Style.Wireframe = Object.Wireframe;
        Style.Fog = Object.UseFog;
        Style.Ambient = Object.UseAmbient;
        Style.Filter = Object.TextureFilter;
        for (const auto& [Stage, BlendMode] : MeshValue.TextureStageBlendModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureBlendModes.size()))
                Style.TextureBlendModes[static_cast<std::size_t>(Stage)] = BlendMode;
        }
        for (const auto& [Stage, AddressMode] : MeshValue.TextureStageAddressModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureAddressModes.size()))
                Style.TextureAddressModes[static_cast<std::size_t>(Stage)] =
                    AddressMode == 2 ? 1 : (AddressMode >= 3 ? 2 : 0);
        }
        for (const auto& [Stage, MipMode] : MeshValue.TextureStageMipModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureMipModes.size()))
                Style.TextureMipModes[static_cast<std::size_t>(Stage)] = MipMode;
        }
        for (const auto& [Stage, CubeMode] : MeshValue.TextureStageCubeModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureCubeModes.size()))
                Style.TextureCubeModes[static_cast<std::size_t>(Stage)] = CubeMode;
        }
        Style.BuiltInEffect = MeshValue.BuiltInEffect;
        Style.EffectId = MeshValue.EffectId >= 0 ? MeshValue.EffectId : Object.EffectId;
        Style.DiffuseColor = Object.DiffuseColor;
        Style.AmbientColor = Object.AmbientColor;
        Style.SpecularColor = Object.SpecularColor;
        Style.EmissiveColor = Object.EmissiveColor;
        Style.SpecularPower = Object.SpecularPower;
        Style.AlphaTestThreshold =
            (Object.TransparencyMode == 4 || Object.TransparencyMode == 6) ? (207.0f / 255.0f) : 0.0f;
        Style.AlphaOverride = Object.AlphaMapping ? Object.AlphaMappingAlpha : -1.0f;
        Style.ReflectionSurface = Object.ReflectionShading;
        return Style;
    }

    // What drawing an object's mesh takes from the object and the mesh, the same for all its triangles.
    struct ObjectMeshSurface
    {
        SDL_Texture* Texture = nullptr;
        SDL_GPUTexture* GpuTexture = nullptr;
        std::array<SDL_GPUTexture*, 7> AuxiliaryGpuTextures{};
        SDL_GPUTexture* CubeGpuTexture = nullptr;
        bool BlendTransparent = false;
        bool BlendAdditive = false;
        SDL_FColor Color{};
        bool UsesVertexAlpha = false;
        SurfaceStyle Style{};
        uint32_t StyleHash = 0;
        std::array<int, 8> CoordinateModes{};
        std::array<const std::vector<SDL_FPoint>*, 8> StageUvs{};
        // Drawn by Blinn-Phong: a normal map in stage 1, a cube's reflection, or both.
        bool BlinnPhong = false;
    };

    [[nodiscard]] ObjectMeshSurface ResolveObjectMeshSurface(EngineKernel& K, const Object3D& Object,
                                                             const Mesh& MeshValue)
    {
        ObjectMeshSurface Surface;
        Surface.CubeGpuTexture = Object.CubeGpuTexture.get();
        int TextureImageId = Object.TextureImageId;
        if (TextureImageId == 0)
        {
            TextureImageId = MeshValue.TextureImageId;
        }
        if (const auto StageZero = MeshValue.TextureStageImageIds.find(0);
            StageZero != MeshValue.TextureStageImageIds.end())
            TextureImageId = StageZero->second;
        if (TextureImageId != 0)
        {
            Image* TexImg = K.ImageStoreRef().GetImage(TextureImageId);
            if (TexImg)
            {
                Surface.Texture =
                    Object.TransparentBlack ? K.ImageStoreRef().EnsureBlackKeyTexture(*TexImg) : TexImg->Texture;
                Surface.GpuTexture = dbc::ImageGpuTexture(K, *TexImg, Object.TransparentBlack);
                // transparent_black textures have alpha=0 for black pixels.
                // The fragment shader discards fragments with alpha <= 0.001,
                // so these objects stay in the OPAQUE pass (depth write ON):
                //  • black pixels → alpha=0 → discard → no depth/color write
                //  • non-black pixels → alpha=1 → normal depth write
                // This correctly blocks additive objects (ghost water, etc.)
                // behind opaque regions while allowing see-through on keyed areas.
                if (!Object.TransparentBlack)
                {
                    Surface.BlendTransparent = TexImg->HasPartialAlpha;
                }
            }
        }
        for (int Stage = 1; Stage < 8; ++Stage)
        {
            if (const auto StageImage = MeshValue.TextureStageImageIds.find(Stage);
                StageImage != MeshValue.TextureStageImageIds.end())
            {
                if (Image* AuxiliaryImage = K.ImageStoreRef().GetImage(StageImage->second))
                    Surface.AuxiliaryGpuTextures[static_cast<std::size_t>(Stage - 1)] =
                        dbc::ImageGpuTexture(K, *AuxiliaryImage, false);
            }
        }
        // Ghost mode 0 = additive blending, mode 1 = alpha blending.
        Surface.BlendAdditive = Object.Ghosted && Object.GhostMode == 0;
        if (Object.Ghosted || Object.Alpha < 0.999f || Object.AlphaMapping)
        {
            Surface.BlendTransparent = true;
        }

        // LOAD OBJECT modes 2-5 preserve the imported diffuse material when texturing.
        const bool HasTexture = (Surface.GpuTexture != nullptr || Surface.Texture != nullptr);
        const bool UseMaterial = !HasTexture || MeshValue.TextureUsesMaterialColor;
        Surface.Color = SDL_FColor{
            std::clamp(Object.TintR * MeshValue.LimbTint.r * (UseMaterial ? MeshValue.MaterialColor.r : 1.0f), 0.0f,
                       1.0f),
            std::clamp(Object.TintG * MeshValue.LimbTint.g * (UseMaterial ? MeshValue.MaterialColor.g : 1.0f), 0.0f,
                       1.0f),
            std::clamp(Object.TintB * MeshValue.LimbTint.b * (UseMaterial ? MeshValue.MaterialColor.b : 1.0f), 0.0f,
                       1.0f),
            std::clamp(Object.Alpha, 0.0f, 1.0f)};
        // As in DBPro's fixed pipeline, vertex alpha only matters to objects that blend or
        // alpha-test. An opaque object stays opaque when RGB() leaves the alpha byte at zero.
        Surface.UsesVertexAlpha =
            Object.Ghosted || Object.Alpha < 0.999f || Object.AlphaMapping || Object.TransparentBlack;
        Surface.Style = BuildSurfaceStyle(Object, MeshValue);
        // SET BUMP MAPPING ON, or SET CUBE MAPPING ON on an object that SET OBJECT SPECULAR says reflects.
        const bool Bump = MeshValue.BuiltInEffect == 1;
        const bool Reflects = Surface.CubeGpuTexture && Object.UseLighting && (Object.SpecularColor & 0x00FFFFFFu) != 0;
        Surface.BlinnPhong =
            !Object.LockToScreen && (Bump || (Reflects && MeshValue.BuiltInEffect == 0 && Surface.Style.EffectId == 0));
        if (Surface.BlinnPhong)
        {
            SurfaceStyle& Style = Surface.Style;
            Style.BuiltInEffect = 4;
            // The reflection takes the place of the stage that multiplies by the cube.
            if (Style.TextureCubeModes[1])
            {
                Style.TextureBlendModes[1] = 0;
                Style.TextureCubeModes[1] = false;
            }
            if (Surface.CubeGpuTexture)
                Style.CubeLevels = static_cast<float>(Object.CubeLevels);
        }
        Surface.StyleHash = SurfaceStyleHash(Surface.Style);
        for (int Stage = 0; Stage < 8; ++Stage)
        {
            const auto Index = static_cast<std::size_t>(Stage);
            if (const auto Mode = MeshValue.TextureStageCoordinateModes.find(Stage);
                Mode != MeshValue.TextureStageCoordinateModes.end())
                Surface.CoordinateModes[Index] = Mode->second;
            if (const auto Uvs = MeshValue.DrawGeometry().UvStages.find(Stage);
                Uvs != MeshValue.DrawGeometry().UvStages.end())
                Surface.StageUvs[Index] = &Uvs->second;
        }
        return Surface;
    }

    // What one triangle corner of an object takes from its vertex and normal.
    struct ObjectCorner
    {
        Vec3 ViewNormal{};
        SDL_FColor Color{};
        SDL_FPoint Uv{};
        std::array<SDL_FPoint, 7> AuxiliaryUvs{};
        // Reflection direction for cube mapping.
        Vec3 Direction{};
    };

    bool NodeHiddenByLimb(const Object3D& Object, const aiNode* Node)
    {
        for (; Node != nullptr; Node = Node->mParent)
        {
            const auto Limb = Object.LimbIndexByNode.find(Node);
            if (Limb != Object.LimbIndexByNode.end() && Limb->second >= 0 &&
                Limb->second < static_cast<int>(Object.LimbTransforms.size()) &&
                Object.LimbTransforms[static_cast<std::size_t>(Limb->second)].Hidden)
                return true;
        }
        return false;
    }

    // Moves a corner along its line of sight until its [0, 1] depth has changed by Bias: where it lands on the screen
    // does not change, so the projection need not know.
    [[nodiscard]] Vec3 DepthBiasedPoint(const Vec3& Point, float Bias, const glm::mat4& Projection)
    {
        if (Point.Z <= 0.0f)
            return Point;
        // Depth is A + B / z with B negative, so the bias acts on B / z alone.
        const float B = Projection[3][2];
        const float Biased = B / Point.Z + Bias;
        if (!(Biased < 0.0f))
            return Point;
        const float Scale = B / Biased / Point.Z;
        return Vec3{Point.X * Scale, Point.Y * Scale, Point.Z * Scale};
    }

    // The steepest change of depth per pixel across a triangle, which a slope-scaled bias multiplies.
    [[nodiscard]] float TriangleDepthSlope(const Vec3& V0, const Vec3& V1, const Vec3& V2, const RenderView& View)
    {
        if (V0.Z <= 0.0f || V1.Z <= 0.0f || V2.Z <= 0.0f)
            return 0.0f;
        const glm::vec2 Half(static_cast<float>(View.PhysicalWidth) * View.Projection[0][0] * 0.5f,
                             static_cast<float>(View.PhysicalHeight) * View.Projection[1][1] * 0.5f);
        const auto OnScreen = [&](const Vec3& V)
        { return glm::vec3(V.X / V.Z * Half.x, V.Y / V.Z * Half.y, View.Projection[3][2] / V.Z); };
        const glm::vec3 A = OnScreen(V0);
        const glm::vec3 E1 = OnScreen(V1) - A;
        const glm::vec3 E2 = OnScreen(V2) - A;
        const float Area = E1.x * E2.y - E2.x * E1.y;
        if (std::abs(Area) < 1e-6f)
            return 0.0f;
        return std::max(std::abs((E1.z * E2.y - E2.z * E1.y) / Area), std::abs((E2.z * E1.x - E1.z * E2.x) / Area));
    }

    // ADD LOD TO OBJECT: the last level whose distance the camera is past draws, each view choosing for itself.
    void SelectObjectLod(Object3D& Object, float Distance)
    {
        const ObjectLod* Chosen = nullptr;
        for (const ObjectLod& Lod : Object.Lods)
            if (!Lod.Geometries.empty() && Distance > Lod.Distance)
                Chosen = &Lod;
        for (std::size_t Index = 0; Index < Object.Meshes.size(); ++Index)
            Object.Meshes[Index].LodGeometry =
                Chosen && Index < Chosen->Geometries.size() ? Chosen->Geometries[Index] : nullptr;
    }

    // Whether an object draws opaque triangles whose result depends on their place among the other objects' opaque
    // triangles, ordered by depth: drawing meshes from GPU buffers apart from them would change it.
    [[nodiscard]] bool ObjectsNeedDepthOrder(EngineKernel& K)
    {
        bool Found = false;
        K.Objects().ForEachObject(
            [&](int, const Object3D& Object)
            {
                if (Found || !Object.Visible || Object.LockToScreen || Object.Ghosted)
                    return;
                const int Mode = Object.TransparencyMode;
                // Transparent, or drawn before every other opaque triangle.
                if (Mode == 2 || Mode == 3 || Mode == 5 || Mode == 6 || Mode == 7)
                    return;
                // Alpha-tested modes blend in the opaque phase.
                if (Mode > 0)
                    Found = true;
                else if ((!Object.DepthTest || !Object.DepthWrite) &&
                         (Mode == 0 || (Object.Alpha >= 0.999f && !Object.AlphaMapping)))
                    Found = true;
            });
        return Found;
    }

    // Records a mesh that the vertex shader draws from its GPU buffers exactly as the CPU path would: one opaque
    // material and no per-vertex work that depends on the view or the frame (stage coordinates, built-in effects,
    // skinning). False leaves the mesh to the CPU path.
    [[nodiscard]] bool AppendGpuMeshDraw(EngineKernel& K, const Object3D& Object, const Mesh& MeshValue,
                                         const MeshWorldTransform& ToWorld, std::vector<GpuMeshDraw>& Draws)
    {
        const MeshGeometry& Geometry = MeshValue.DrawGeometry();
        const std::size_t VertexCount = Geometry.Vertices.size();
        if (!Geometry.Uvs.empty() && Geometry.Uvs.size() != VertexCount)
            return false;
        // Without vertex normals each triangle is lit by its world face normal, which the inverse transpose
        // takes from the object-space one up to the sign of the determinant.
        const bool FaceNormals = Geometry.Normals.size() != VertexCount;
        const glm::mat4 World = ToWorld.PointMatrix();
        const float Determinant = glm::determinant(glm::mat3(World));
        if (FaceNormals && !(std::abs(Determinant) > 1e-12f))
            return false;
        const ObjectMeshSurface Surface = ResolveObjectMeshSurface(K, Object, MeshValue);
        const SurfaceStyle& Style = Surface.Style;
        const auto Set = [](const auto& Values, std::size_t First = 0)
        {
            return std::any_of(std::next(Values.begin(), static_cast<std::ptrdiff_t>(First)), Values.end(),
                               [](const auto& Value) { return Value != std::decay_t<decltype(Value)>{}; });
        };
        // Blinn-Phong's normal map and cube are bindings of the instanced draw, and instances take the base
        // coordinates: what stage 1 asks for until the map moves apart from them, and what the cube's stage no longer
        // uses.
        std::array<int, 8> CoordinateModes = Surface.CoordinateModes;
        if (Surface.BlinnPhong && (CoordinateModes[1] == 2 || CoordinateModes[1] == 3))
            CoordinateModes[1] = 0;
        else if (Surface.BlinnPhong && CoordinateModes[1] == 0 && Surface.StageUvs[1])
            return false;
        // Stage 1 at a multiple of the base coordinates, unless the object's texture transform would move it with them.
        const bool Stage1Scaled = !Surface.BlinnPhong && MeshValue.Stage1UvScale[0] != 0.0f &&
                                  Object.TexScaleU == 1.0f && Object.TexScaleV == 1.0f && Object.TexScrollU == 0.0f &&
                                  Object.TexScrollV == 0.0f;
        if ((MeshValue.BuiltInEffect != 0 && !Surface.BlinnPhong) || Surface.BlendTransparent ||
            Surface.BlendAdditive || (Surface.CubeGpuTexture && !Surface.BlinnPhong) || Style.EffectId != 0 ||
            Style.ReflectionSurface || Set(Surface.AuxiliaryGpuTextures, Surface.BlinnPhong || Stage1Scaled ? 1 : 0) ||
            Set(CoordinateModes) || Set(Style.TextureCubeModes) || Set(Style.TextureBlendModes, Stage1Scaled ? 2 : 1) ||
            !K.Renderer().PrepareGpuMesh(MeshValue.SharedDrawGeometry()))
            return false;

        GpuMeshDraw Draw;
        Draw.Geometry = MeshValue.SharedDrawGeometry();
        GpuMeshInstance& Instance = Draw.Instance;
        const glm::mat3 Normal =
            FaceNormals ? glm::transpose(glm::inverse(glm::mat3(World))) * (Determinant < 0.0f ? -1.0f : 1.0f)
                        : ToWorld.NormalMatrix();
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 4; ++Column)
                Instance.World[Row][Column] = World[Column][Row];
            for (int Column = 0; Column < 3; ++Column)
                Instance.Normal[Row][Column] = Normal[Column][Row];
        }
        Instance.Normal[0][3] = std::max(Object.FadeNormalScale, 0.015f);
        Instance.Normal[1][3] = Object.UseLighting || Surface.BlinnPhong ? 1.0f : 0.0f;
        Instance.Normal[2][3] = FaceNormals || K.Renderer().NormalizeNormals() ? 1.0f : 0.0f;
        // As AppendGpuTriangle scales the corner colours by the diffuse material.
        Instance.ColorScale[0] = Surface.Color.r * PackedChannel(Style.DiffuseColor, 16u);
        Instance.ColorScale[1] = Surface.Color.g * PackedChannel(Style.DiffuseColor, 8u);
        Instance.ColorScale[2] = Surface.Color.b * PackedChannel(Style.DiffuseColor, 0u);
        Instance.ColorScale[3] = Surface.Color.a;
        // A mesh without texture coordinates takes the scroll offset alone, as the CPU path does.
        Instance.UvTransform[0] = Object.TexScaleU;
        Instance.UvTransform[1] = Object.TexScaleV;
        Instance.UvTransform[2] = Object.TexScrollU;
        Instance.UvTransform[3] = Object.TexScrollV;
        GpuVertex Material{};
        FillSurfaceMaterial(Material, Style);
        const float BoxTr[4]{Material.BoxTrR, Material.BoxTrG, Material.BoxTrB, Material.BoxTrA};
        const float BoxBr[4]{Material.BoxBrR, Material.BoxBrG, Material.BoxBrB, Material.BoxBrA};
        const float BoxBl[4]{Material.BoxBlR, Material.BoxBlG, Material.BoxBlB, Material.BoxBlA};
        std::copy(std::begin(BoxTr), std::end(BoxTr), Instance.BoxTr);
        std::copy(std::begin(BoxBr), std::end(BoxBr), Instance.BoxBr);
        std::copy(std::begin(BoxBl), std::end(BoxBl), Instance.BoxBl);
        Draw.Cull = Object.Cull;
        Draw.Texture = Surface.GpuTexture;
        if (Surface.BlinnPhong)
        {
            Draw.AuxiliaryTextures = Surface.AuxiliaryGpuTextures;
            Draw.CubeTexture = Surface.CubeGpuTexture;
        }
        else if (Stage1Scaled)
        {
            Draw.AuxiliaryTextures = Surface.AuxiliaryGpuTextures;
        }
        Draw.Style = Style;
        Draw.StyleHash = Surface.StyleHash;
        if (Stage1Scaled)
        {
            Draw.Style.Stage1UvScale = MeshValue.Stage1UvScale;
            Draw.StyleHash = SurfaceStyleHash(Draw.Style);
        }
        Draws.push_back(std::move(Draw));
        return true;
    }

    // Scene, when given, receives how many objects the pass went through, showed and culled and, with GpuMeshes,
    // the meshes drawn from GPU buffers instead of triangles.
    void RenderObjects(EngineKernel& K, std::vector<Triangle>& WorldTris, std::vector<Triangle>& OverlayTris,
                       SceneTriangles* Scene, bool GpuMeshes)
    {
        std::optional<RenderView> CameraView;
        const RenderView& View = ActiveOrCameraView(K, CameraView);

        const Vec3 Camera = ActiveCameraPosition(K);
        const bool Reflecting = K.Renderer().ReflectionViewEnabled();
        // Per-vertex scratch, reused from mesh to mesh.
        std::vector<Vec3> WorldPositions;
        std::vector<Vec3> ViewPositions;
        std::vector<ObjectCorner> Corners;
        std::vector<uint8_t> CornerReady;

        bool HasWaterline = false;
        float WaterlineY = std::numeric_limits<float>::max();
        K.Objects().ForEachObject(
            [&](int, const Object3D& Object)
            {
                if (Object.Visible && Object.TransparencyMode == 5)
                {
                    WaterlineY = std::min(WaterlineY, dbc::WorldObjectOrigin(K, Object).Y);
                    HasWaterline = true;
                }
            });

        K.Objects().ForEachObject(
            [&](int Id, Object3D& Object)
            {
                if (Scene)
                    ++Scene->Objects;
                if (!Object.Visible || Object.Excluded)
                    return;
                if (const int CameraId = K.Camera().CurrentCameraId();
                    CameraId >= 0 && CameraId < 32 && ((Object.CameraMask >> CameraId) & 1u) == 0)
                    return;
                if (Scene)
                    ++Scene->VisibleObjects;
                if (!Object.Lods[0].Geometries.empty() || !Object.Lods[1].Geometries.empty())
                {
                    const Vec3 Origin = dbc::WorldObjectOrigin(K, Object);
                    SelectObjectLod(Object, std::hypot(Origin.X - Camera.X, Origin.Y - Camera.Y, Origin.Z - Camera.Z));
                }

                // ── Frustum culling (sphere test) ────────────────────────────────────
                // Skip objects whose bounding sphere lies entirely outside the view frustum.
                // lock_to_screen objects live in screen-space and are never culled.
                if (!Object.LockToScreen && Object.ViewFov == 0.0f &&
                    (!K.Objects().IsStaticRenderObject(Id) || K.Objects().StaticOcclusionEnabled()))
                {
                    const float MaxScale =
                        std::max({std::abs(Object.ScaleX), std::abs(Object.ScaleY), std::abs(Object.ScaleZ)});
                    const float WorldRadius = K.Objects().RenderBoundingRadius(Object) * MaxScale;
                    // For glued objects, use the actual world position (accounting for the glue chain).
                    const Vec3 WorldCenter = (Object.GluedTargetObject > 0)
                                                 ? dbc::WorldObjectOrigin(K, Object)
                                                 : Vec3{Object.PosX, Object.PosY, Object.PosZ};
                    const Vec3 Vc = dbc::WorldToViewPoint(K, WorldCenter.X, WorldCenter.Y, WorldCenter.Z);

                    if (!View.ContainsSphere({Vc.X, Vc.Y, Vc.Z}, WorldRadius, Object.Ghosted && Object.GhostMode == 0))
                    {
                        if (Scene)
                            ++Scene->CulledObjects;
                        return;
                    }
                }
                const auto& NodeGlobals = Object.CachedNodeGlobals;

                int AlphaPhase = -1;
                if (Object.TransparencyMode >= 0)
                {
                    if (Object.TransparencyMode == 0 || Object.TransparencyMode == 7)
                        AlphaPhase = 0;
                    else if (Object.TransparencyMode == 2 || Object.TransparencyMode == 3 ||
                             Object.TransparencyMode == 5 || Object.TransparencyMode == 6)
                        AlphaPhase = 2;
                    else
                        AlphaPhase = 1;
                }
                if (Object.Ghosted)
                    AlphaPhase = 2;
                const bool VeryEarly = Object.TransparencyMode == 7;
                float DepthBias = 0.0f;
                if (HasWaterline && AlphaPhase == 2)
                {
                    bool BelowWaterline = Object.TransparencyMode == 5;
                    if (!BelowWaterline)
                    {
                        float LocalMinY = std::numeric_limits<float>::max();
                        float LocalMaxY = std::numeric_limits<float>::lowest();
                        for (const Mesh& BoundMesh : Object.Meshes)
                        {
                            for (const Vec3& Vertex : BoundMesh.Geometry().Vertices)
                            {
                                const Vec3 BoundVertex =
                                    ApplyAiTransformPoint(BoundMesh.Geometry().BindTransform, Vertex);
                                LocalMinY = std::min(LocalMinY, BoundVertex.Y);
                                LocalMaxY = std::max(LocalMaxY, BoundVertex.Y);
                            }
                        }
                        const float Height = LocalMaxY >= LocalMinY ? LocalMaxY - LocalMinY : 0.0f;
                        const float BaseY = dbc::WorldObjectOrigin(K, Object).Y - Height * std::abs(Object.ScaleY);
                        BelowWaterline = BaseY < WaterlineY;
                    }
                    if (BelowWaterline)
                    {
                        const glm::vec4 FarPlane = View.ClipPlane(5);
                        DepthBias = FarPlane.z < 0.0f ? -FarPlane.w / FarPlane.z : 1e7f;
                    }
                }

                // An object locked to the screen lives in camera space, where the eye is the origin.
                const Vec3 EyePoint = Object.LockToScreen ? Vec3{0.0f, 0.0f, 0.0f} : Camera;
                // Opaque, depth-tested objects in the world, outside any glue chain, with no reflection. Casting a
                // shadow does not matter: volumes come from the whole mesh, not from what is drawn.
                const bool GpuObject =
                    GpuMeshes && Scene && AlphaPhase < 0 && !Object.LockToScreen && Object.GluedTargetObject <= 0 &&
                    Object.Alpha >= 0.999f && !Object.AlphaMapping && !Object.TransparentBlack && Object.DepthTest &&
                    Object.DepthWrite && !Object.ReflectionShading && Object.ViewFov == 0.0f && !Object.DepthBiased;
                // Scaling x and y in view space lands a vertex where a projection with that field of view would.
                const float FovScale =
                    Object.ViewFov > 0.0f ? std::tan(glm::radians(std::clamp(K.Camera().Fov(), 10.0f, 170.0f)) * 0.5f) /
                                                std::tan(glm::radians(Object.ViewFov) * 0.5f)
                                          : 1.0f;
                for (const Mesh& MeshValue : Object.Meshes)
                {
                    if (!MeshValue.Visible || MeshValue.Excluded)
                    {
                        continue;
                    }
                    const aiMatrix4x4* MeshTransform = nullptr;
                    const aiNode* MeshNode = nullptr;
                    if (!NodeGlobals.empty() && MeshValue.SourceMeshIndex >= 0)
                    {
                        const auto MnIt =
                            Object.MeshNodeByIndex.find(static_cast<unsigned int>(MeshValue.SourceMeshIndex));
                        if (MnIt != Object.MeshNodeByIndex.end())
                        {
                            MeshNode = MnIt->second;
                            const auto MtIt = NodeGlobals.find(MnIt->second);
                            if (MtIt != NodeGlobals.end())
                            {
                                MeshTransform = &MtIt->second;
                            }
                        }
                    }
                    if (MeshNode && NodeHiddenByLimb(Object, MeshNode))
                    {
                        continue;
                    }
                    const std::vector<aiMatrix4x4> BonePalette = BuildBonePalette(Object, MeshValue, NodeGlobals);
                    const MeshWorldTransform ToWorld(K, Object, MeshValue, MeshTransform);
                    if (GpuObject && BonePalette.empty() &&
                        AppendGpuMeshDraw(K, Object, MeshValue, ToWorld, Scene->MeshDraws))
                        continue;
                    const MeshGeometry& Geometry = MeshValue.DrawGeometry();
                    std::vector<Vec3> SkinnedPositions;
                    std::vector<Vec3> SkinnedNormals;
                    std::vector<uint8_t> SkinnedVertices;
                    if (!BonePalette.empty())
                    {
                        // Only the vertices SkinMeshVertex accepts are read from these.
                        SkinnedPositions.resize(Geometry.Vertices.size());
                        SkinnedNormals.resize(Geometry.Vertices.size());
                        SkinnedVertices.resize(Geometry.Vertices.size());
                        for (std::size_t VertexIndex = 0; VertexIndex < Geometry.Vertices.size(); ++VertexIndex)
                        {
                            SkinnedVertices[VertexIndex] =
                                SkinMeshVertex(MeshValue, VertexIndex, BonePalette, SkinnedPositions[VertexIndex],
                                               SkinnedNormals[VertexIndex])
                                    ? 1
                                    : 0;
                        }
                    }
                    // Each vertex is transformed once, not once per triangle that uses it.
                    const std::size_t VertexCount = Geometry.Vertices.size();
                    WorldPositions.resize(VertexCount);
                    ViewPositions.resize(VertexCount);
                    for (std::size_t Vertex = 0; Vertex < VertexCount; ++Vertex)
                    {
                        const bool Skinned = !SkinnedVertices.empty() && SkinnedVertices[Vertex] != 0;
                        const Vec3 World =
                            ToWorld.Point(Skinned ? SkinnedPositions[Vertex] : Geometry.Vertices[Vertex], Skinned);
                        WorldPositions[Vertex] = World;
                        ViewPositions[Vertex] =
                            Object.LockToScreen ? World : dbc::WorldToViewPoint(K, World.X, World.Y, World.Z);
                        ViewPositions[Vertex].X *= FovScale;
                        ViewPositions[Vertex].Y *= FovScale;
                    }

                    // Resolved by the first visible triangle, so a mesh that shows none loads no texture.
                    std::optional<ObjectMeshSurface> Surface;
                    const bool VertexNormals = Geometry.Normals.size() == VertexCount;
                    const bool Lit = Object.UseLighting || MeshValue.BuiltInEffect != 0;
                    const auto ReflectionDirection = [&](const Vec3& WorldPoint, const Vec3& WorldNormal)
                    {
                        const Vec3 Incident = Vec3Normalize(
                            Vec3{WorldPoint.X - EyePoint.X, WorldPoint.Y - EyePoint.Y, WorldPoint.Z - EyePoint.Z});
                        const Vec3 Normal = Vec3Normalize(WorldNormal);
                        const float Projection = 2.0f * Vec3Dot(Incident, Normal);
                        return Vec3Normalize(Vec3{Incident.X - Projection * Normal.X,
                                                  Incident.Y - Projection * Normal.Y,
                                                  Incident.Z - Projection * Normal.Z});
                    };
                    const auto ResolveStageUv = [&](uint32_t VertexIndex, int Stage, SDL_FPoint BaseUv,
                                                    const Vec3& WorldNormal, const Vec3& Reflection)
                    {
                        const int CoordinateMode = Surface->CoordinateModes[static_cast<std::size_t>(Stage)];
                        if (CoordinateMode == 1)
                        {
                            const Vec3 ViewNormal = Vec3Normalize(
                                dbc::WorldToViewDirection(K, WorldNormal.X, WorldNormal.Y, WorldNormal.Z));
                            return SDL_FPoint{ViewNormal.X * 0.5f + 0.5f, ViewNormal.Y * -0.5f + 0.5f};
                        }
                        if (CoordinateMode == 2)
                            return SDL_FPoint{Reflection.X * 0.5f + 0.5f, Reflection.Y * -0.5f + 0.5f};
                        int SourceStage = Stage;
                        if (CoordinateMode == 3)
                            SourceStage = 0;
                        else if (CoordinateMode >= 10 && CoordinateMode <= 17)
                            SourceStage = CoordinateMode - 10;
                        if (SourceStage == 0)
                            return BaseUv;
                        const std::vector<SDL_FPoint>* StageUvs =
                            Surface->StageUvs[static_cast<std::size_t>(SourceStage)];
                        if (StageUvs && VertexIndex < StageUvs->size())
                            return (*StageUvs)[VertexIndex];
                        return BaseUv;
                    };
                    // Everything a triangle corner takes from its vertex and normal.
                    const auto MakeCorner = [&](uint32_t Index, const Vec3& WorldPoint, const Vec3& WorldNormal)
                    {
                        ObjectCorner Corner;
                        // Transform normals to view-space for GPU lighting.
                        // For unlit objects, pass zero normals so the shader skips lighting.
                        if (Lit)
                        {
                            Corner.ViewNormal =
                                dbc::WorldToViewDirection(K, WorldNormal.X, WorldNormal.Y, WorldNormal.Z);
                            // DBC FADE OBJECT: normal magnitude encodes the fade multiplier.
                            // The shader uses length(N) as a diffuse/specular multiplier:
                            //   0.015 → ambient only,  1.0 → normal,  2.0 → 2× bright.
                            // Clamp minimum to 0.015 so lit objects never hit the shader's
                            // unlit path (length < 0.01) which outputs full brightness.
                            const float Fs = std::max(Object.FadeNormalScale, 0.015f);
                            if (Fs != 1.0f)
                            {
                                Corner.ViewNormal.X *= Fs;
                                Corner.ViewNormal.Y *= Fs;
                                Corner.ViewNormal.Z *= Fs;
                            }
                        }

                        const SDL_FColor& Col = Surface->Color;
                        const uint32_t Packed =
                            Index < Geometry.DiffuseColors.size() ? Geometry.DiffuseColors[Index] : 0xFFFFFFFFu;
                        Corner.Color = SDL_FColor{
                            Col.r * static_cast<float>((Packed >> 16u) & 0xFFu) / 255.0f,
                            Col.g * static_cast<float>((Packed >> 8u) & 0xFFu) / 255.0f,
                            Col.b * static_cast<float>(Packed & 0xFFu) / 255.0f,
                            Surface->UsesVertexAlpha ? Col.a * static_cast<float>((Packed >> 24u) & 0xFFu) / 255.0f
                                                     : Col.a,
                        };

                        SDL_FPoint Uv{Object.TexScrollU, Object.TexScrollV};
                        if (!Geometry.Uvs.empty())
                        {
                            if (Index < Geometry.Uvs.size())
                                Uv = Geometry.Uvs[Index];
                            Uv.x = Uv.x * Object.TexScaleU + Object.TexScrollU;
                            Uv.y = Uv.y * Object.TexScaleV + Object.TexScrollV;
                        }
                        const Vec3 Reflection = ReflectionDirection(WorldPoint, WorldNormal);
                        Corner.Uv = ResolveStageUv(Index, 0, Uv, WorldNormal, Reflection);
                        for (int Stage = 1; Stage < 8; ++Stage)
                            Corner.AuxiliaryUvs[static_cast<std::size_t>(Stage - 1)] =
                                ResolveStageUv(Index, Stage, Corner.Uv, WorldNormal, Reflection);
                        if (MeshValue.BuiltInEffect == 2)
                        {
                            const Vec3 Normal = Vec3Normalize(WorldNormal);
                            Vec3 ToLight{};
                            if (K.Lights().LightVisible(0) != 0)
                            {
                                if (K.Lights().LightType(0) == 1 || K.Lights().LightType(0) == 2)
                                {
                                    ToLight = Vec3Normalize(Vec3{K.Lights().LightPositionX(0) - WorldPoint.X,
                                                                 K.Lights().LightPositionY(0) - WorldPoint.Y,
                                                                 K.Lights().LightPositionZ(0) - WorldPoint.Z});
                                }
                                else
                                {
                                    ToLight = Vec3Normalize(Vec3{-K.Lights().LightDirectionX(0),
                                                                 -K.Lights().LightDirectionY(0),
                                                                 -K.Lights().LightDirectionZ(0)});
                                }
                            }
                            const Vec3 ToEye = Vec3Normalize(
                                Vec3{EyePoint.X - WorldPoint.X, EyePoint.Y - WorldPoint.Y, EyePoint.Z - WorldPoint.Z});
                            Corner.Uv = SDL_FPoint{std::clamp(Vec3Dot(Normal, ToLight), 0.0f, 1.0f), 0.5f};
                            Corner.AuxiliaryUvs[0] = SDL_FPoint{std::clamp(Vec3Dot(Normal, ToEye), 0.0f, 1.0f), 0.5f};
                        }
                        else if (MeshValue.BuiltInEffect == 3)
                        {
                            const Vec3 Normal = Vec3Normalize(WorldNormal);
                            const Vec3 Eye = Vec3Normalize(
                                Vec3{EyePoint.X - WorldPoint.X, EyePoint.Y - WorldPoint.Y, EyePoint.Z - WorldPoint.Z});
                            const Vec3 Half = Vec3Normalize(Vec3{Normal.X + Eye.X, Normal.Y + Eye.Y, Normal.Z + Eye.Z});
                            Corner.Uv = SDL_FPoint{Vec3Dot(Normal, Eye), Vec3Dot(Half, Normal)};
                            Corner.AuxiliaryUvs[0] = SDL_FPoint{Eye.X, Half.Y};
                        }
                        Corner.Direction = Reflection;
                        return Corner;
                    };
                    // With vertex normals a corner depends on its vertex only, so it is built once per vertex.
                    if (VertexNormals)
                    {
                        Corners.resize(VertexCount);
                        CornerReady.assign(VertexCount, 0);
                    }
                    const auto VertexCorner = [&](uint32_t Index) -> const ObjectCorner&
                    {
                        if (!CornerReady[Index])
                        {
                            const bool Skinned = !SkinnedVertices.empty() && SkinnedVertices[Index] != 0;
                            Corners[Index] = MakeCorner(
                                Index, WorldPositions[Index],
                                ToWorld.Normal(Skinned ? SkinnedNormals[Index] : Geometry.Normals[Index], Skinned));
                            CornerReady[Index] = 1;
                        }
                        return Corners[Index];
                    };

                    for (size_t I = 0; I + 2 < Geometry.Indices.size(); I += 3)
                    {
                        const uint32_t I0 = Geometry.Indices[I];
                        const uint32_t I1 = Geometry.Indices[I + 1];
                        const uint32_t I2 = Geometry.Indices[I + 2];
                        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
                        {
                            continue;
                        }

                        const Vec3& W0 = WorldPositions[I0];
                        const Vec3& W1 = WorldPositions[I1];
                        const Vec3& W2 = WorldPositions[I2];
                        const Vec3& V0 = ViewPositions[I0];
                        const Vec3& V1 = ViewPositions[I1];
                        const Vec3& V2 = ViewPositions[I2];

                        const Vec3 E1 = Vec3Sub(W1, W0);
                        const Vec3 E2 = Vec3Sub(W2, W0);
                        const Vec3 FaceNormal = Vec3Normalize(Vec3Cross(E1, E2));
                        if (Object.Cull && !Reflecting)
                        {
                            const float Cx = (W0.X + W1.X + W2.X) / 3.0f;
                            const float Cy = (W0.Y + W1.Y + W2.Y) / 3.0f;
                            const float Cz = (W0.Z + W1.Z + W2.Z) / 3.0f;
                            const Vec3 ToCam{EyePoint.X - Cx, EyePoint.Y - Cy, EyePoint.Z - Cz};
                            const float Facing = Vec3Dot(FaceNormal, ToCam);
                            if (Facing <= 0.0f)
                            {
                                continue;
                            }
                        }
                        if (!Surface)
                            Surface = ResolveObjectMeshSurface(K, Object, MeshValue);

                        // Without vertex normals every corner takes the face normal.
                        ObjectCorner Face0;
                        ObjectCorner Face1;
                        ObjectCorner Face2;
                        if (!VertexNormals)
                        {
                            Face0 = MakeCorner(I0, W0, FaceNormal);
                            Face1 = MakeCorner(I1, W1, FaceNormal);
                            Face2 = MakeCorner(I2, W2, FaceNormal);
                        }
                        const ObjectCorner& C0 = VertexNormals ? VertexCorner(I0) : Face0;
                        const ObjectCorner& C1 = VertexNormals ? VertexCorner(I1) : Face1;
                        const ObjectCorner& C2 = VertexNormals ? VertexCorner(I2) : Face2;

                        Vec3 ShaderDirection0 = C0.Direction;
                        Vec3 ShaderDirection1 = C1.Direction;
                        Vec3 ShaderDirection2 = C2.Direction;
                        if (MeshValue.BuiltInEffect == 1 && !Surface->BlinnPhong)
                        {
                            const Vec3 Edge1 = Vec3Sub(V1, V0);
                            const Vec3 Edge2 = Vec3Sub(V2, V0);
                            const float DeltaU1 = C1.Uv.x - C0.Uv.x;
                            const float DeltaV1 = C1.Uv.y - C0.Uv.y;
                            const float DeltaU2 = C2.Uv.x - C0.Uv.x;
                            const float DeltaV2 = C2.Uv.y - C0.Uv.y;
                            const float Determinant = DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1;
                            Vec3 Tangent{};
                            if (std::abs(Determinant) > 0.000001f)
                            {
                                const float Inverse = 1.0f / Determinant;
                                Tangent = Vec3Normalize(Vec3{(Edge1.X * DeltaV2 - Edge2.X * DeltaV1) * Inverse,
                                                             (Edge1.Y * DeltaV2 - Edge2.Y * DeltaV1) * Inverse,
                                                             (Edge1.Z * DeltaV2 - Edge2.Z * DeltaV1) * Inverse});
                            }
                            if (Vec3Dot(Tangent, Tangent) < 0.000001f)
                            {
                                const Vec3 Normal = Vec3Normalize(C0.ViewNormal);
                                const Vec3 Axis =
                                    std::abs(Normal.Y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
                                Tangent = Vec3Normalize(Vec3Cross(Axis, Normal));
                            }
                            ShaderDirection0 = Tangent;
                            ShaderDirection1 = Tangent;
                            ShaderDirection2 = Tangent;
                        }

                        if (Object.LockToScreen)
                        {
                            SDL_FPoint Screen0{};
                            SDL_FPoint Screen1{};
                            SDL_FPoint Screen2{};
                            float Pd0 = 0.0f;
                            float Pd1 = 0.0f;
                            float Pd2 = 0.0f;
                            if (!dbc::ProjectPointLocked(K, V0.X, V0.Y, V0.Z, Screen0, Pd0) ||
                                !dbc::ProjectPointLocked(K, V1.X, V1.Y, V1.Z, Screen1, Pd1) ||
                                !dbc::ProjectPointLocked(K, V2.X, V2.Y, V2.Z, Screen2, Pd2))
                            {
                                continue;
                            }

                            Triangle Tri;
                            Tri.GpuTexture = Surface->GpuTexture;
                            Tri.Depth = (Pd0 + Pd1 + Pd2) / 3.0f;
                            const bool AutomaticTransparency = Surface->BlendTransparent || C0.Color.a < 0.999f ||
                                                               C1.Color.a < 0.999f || C2.Color.a < 0.999f;
                            Tri.Transparent = AlphaPhase == 2 || (AlphaPhase < 0 && AutomaticTransparency);
                            Tri.AlphaDepthWrite = AlphaPhase == 1;
                            Tri.Additive = Surface->BlendAdditive;
                            Tri.VeryEarly = VeryEarly;
                            Tri.Is2d = true;
                            Tri.DepthTest = Object.DepthTest;
                            Tri.DepthWrite = Object.DepthWrite && !Surface->BlendAdditive;
                            Tri.Style = Surface->Style;
                            Tri.Pos[0][0] = Screen0.x;
                            Tri.Pos[0][1] = Screen0.y;
                            Tri.Pos[0][2] = 0.0f;
                            Tri.Pos[1][0] = Screen1.x;
                            Tri.Pos[1][1] = Screen1.y;
                            Tri.Pos[1][2] = 0.0f;
                            Tri.Pos[2][0] = Screen2.x;
                            Tri.Pos[2][1] = Screen2.y;
                            Tri.Pos[2][2] = 0.0f;
                            Tri.Color[0] = C0.Color;
                            Tri.Color[1] = C1.Color;
                            Tri.Color[2] = C2.Color;
                            Tri.Uv[0] = C0.Uv;
                            Tri.Uv[1] = C1.Uv;
                            Tri.Uv[2] = C2.Uv;
                            Tri.AuxiliaryGpuTextures = Surface->AuxiliaryGpuTextures;
                            Tri.CubeGpuTexture = Surface->CubeGpuTexture;
                            for (std::size_t Stage = 0; Stage < Tri.AuxiliaryUvs.size(); ++Stage)
                            {
                                Tri.AuxiliaryUvs[Stage][0] = C0.AuxiliaryUvs[Stage];
                                Tri.AuxiliaryUvs[Stage][1] = C1.AuxiliaryUvs[Stage];
                                Tri.AuxiliaryUvs[Stage][2] = C2.AuxiliaryUvs[Stage];
                            }
                            Tri.CubeDirection[0][0] = ShaderDirection0.X;
                            Tri.CubeDirection[0][1] = ShaderDirection0.Y;
                            Tri.CubeDirection[0][2] = ShaderDirection0.Z;
                            Tri.CubeDirection[1][0] = ShaderDirection1.X;
                            Tri.CubeDirection[1][1] = ShaderDirection1.Y;
                            Tri.CubeDirection[1][2] = ShaderDirection1.Z;
                            Tri.CubeDirection[2][0] = ShaderDirection2.X;
                            Tri.CubeDirection[2][1] = ShaderDirection2.Y;
                            Tri.CubeDirection[2][2] = ShaderDirection2.Z;
                            OverlayTris.push_back(Tri);
                            continue;
                        }

                        const auto ViewCorner = [](const Vec3& View, const ObjectCorner& Corner, const Vec3& Direction)
                        {
                            ViewVertex Vertex{};
                            Vertex.X = View.X;
                            Vertex.Y = View.Y;
                            Vertex.Z = View.Z;
                            Vertex.Nx = Corner.ViewNormal.X;
                            Vertex.Ny = Corner.ViewNormal.Y;
                            Vertex.Nz = Corner.ViewNormal.Z;
                            Vertex.Uv = Corner.Uv;
                            Vertex.AuxiliaryUvs = Corner.AuxiliaryUvs;
                            Vertex.CubeX = Direction.X;
                            Vertex.CubeY = Direction.Y;
                            Vertex.CubeZ = Direction.Z;
                            Vertex.Color = Corner.Color;
                            return Vertex;
                        };
                        const float Bias = Object.DepthBiased
                                               ? Object.DepthBiasConstant +
                                                     Object.DepthBiasSlope * TriangleDepthSlope(V0, V1, V2, View)
                                               : 0.0f;
                        const auto Biased = [&](const Vec3& Point)
                        { return Bias != 0.0f ? DepthBiasedPoint(Point, Bias, View.Projection) : Point; };
                        dbc::AppendClippedViewTriangle(
                            K, ViewCorner(Biased(V0), C0, ShaderDirection0),
                            ViewCorner(Biased(V1), C1, ShaderDirection1), ViewCorner(Biased(V2), C2, ShaderDirection2),
                            Surface->GpuTexture, Surface->BlendTransparent, WorldTris, Surface->BlendAdditive,
                            Surface->Style, AlphaPhase, VeryEarly, DepthBias, -1, 0, Object.DepthWrite,
                            Object.DepthTest, Surface->AuxiliaryGpuTextures, Surface->CubeGpuTexture,
                            Surface->StyleHash);
                    }
                }
                dbc::RenderObjectBounds(K, Object, WorldTris);
            });
    }
} // namespace dbc
