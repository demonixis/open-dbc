#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../engine_types.hpp"

// Windows headers define GetObject as a macro; undef to avoid renaming our method
#ifdef _WIN32
#undef GetObject
#endif

namespace dbc
{
    class EngineKernel;

    class ObjectManager
    {
      public:
        explicit ObjectManager(EngineKernel& Kernel);
        ~ObjectManager();

        // ── Fine-grained data access ────────────────────────────────────
        [[nodiscard]] Object3D* GetObject(int ObjectId);
        [[nodiscard]] const Object3D* GetObject(int ObjectId) const;
        float RenderBoundingRadius(Object3D& Object);
        [[nodiscard]] bool AnyObjectDirty() const;
        void ClearAllDirtyFlags();
        [[nodiscard]] std::vector<Mesh>* GetMeshes(int MeshId);
        [[nodiscard]] const std::vector<Mesh>* GetMeshes(int MeshId) const;
        void StoreMesh(int MeshId, Mesh MeshValue);
        bool LoadMesh(const std::string& Path, int MeshId, std::string* Error = nullptr);
        bool SaveMesh(const std::string& Path, int MeshId, std::string* Error = nullptr) const;
        bool DeleteMesh(int MeshId, std::string* Error = nullptr);
        int MeshExist(int MeshId) const;

        template <typename Fn> void ForEachObject(Fn&& FnValue)
        {
            for (auto& [Id, Object] : Objects_)
                FnValue(Id, Object);
            for (std::size_t Index = 0; Index < StaticObjects_.size(); ++Index)
                FnValue(-static_cast<int>(Index) - 1, StaticObjects_[Index]);
        }

        template <typename Fn> void ForEachObject(Fn&& FnValue) const
        {
            for (const auto& [Id, Object] : Objects_)
                FnValue(Id, Object);
            for (std::size_t Index = 0; Index < StaticObjects_.size(); ++Index)
                FnValue(-static_cast<int>(Index) - 1, StaticObjects_[Index]);
        }

        [[nodiscard]] ParticleEmitter* GetParticles(int ParticleId);
        [[nodiscard]] const ParticleEmitter* GetParticles(int ParticleId) const;

        template <typename Fn> void ForEachParticles(Fn&& FnValue)
        {
            for (auto& [Id, Particles] : Particles_)
                FnValue(Id, Particles);
        }

        template <typename Fn> void ForEachParticles(Fn&& FnValue) const
        {
            for (const auto& [Id, Particles] : Particles_)
                FnValue(Id, Particles);
        }

        void UpdateParticles();

        // Collision subsystem (transitional mutable access)
        SpatialHash& SpatialHashRef();
        std::vector<AABB>& StaticBoxes();
        float& CollisionSlideX();
        float& CollisionSlideY();
        float& CollisionSlideZ();
        float& StaticHitX();
        float& StaticHitY();
        float& StaticHitZ();

        // ── Object CRUD ──────────────────────────────────────────────────
        bool LoadObject(const std::string& Path, int ObjectId, int LoadMode = 0, int DivideTextureSize = 0,
                        std::string* Error = nullptr);
        bool SaveObject(const std::string& Path, int ObjectId, std::string* Error = nullptr) const;
        bool AppendObject(const std::string& Path, int ObjectId, int StartFrame, std::string* Error = nullptr);
        int TotalObjectFrames(int ObjectId);
        bool MakeObjectPlain(int ObjectId, float Width, float Height, int Orientation = 0,
                             std::string* Error = nullptr);
        void MakeObjectCube(int ObjectId, float Size);
        void MakeObjectBox(int ObjectId, float Width, float Height, float Depth);
        void MakeObjectSphere(int ObjectId, float Radius, int Rings, int Segments);
        void MakeObjectCylinder(int ObjectId, float Radius, float Height, int Segments);
        void MakeObjectCone(int ObjectId, float Radius, float Height, int Segments);
        void MakeObjectTriangle(int ObjectId, float X1, float Y1, float Z1, float X2, float Y2, float Z2, float X3,
                                float Y3, float Z3);
        bool MakeMeshFromObject(int MeshId, int ObjectId, int IgnoreMode = 0, std::string* Error = nullptr);
        void MakeObject(int ObjectId, int MeshId, int ImageId);
        bool MakeObjectFromLimb(int ObjectId, int SourceObjectId, int LimbId, int AllLimbs = 0,
                                std::string* Error = nullptr);
        bool AddLimb(int ObjectId, int LimbId, int SourceMeshId, std::string* Error = nullptr);
        bool RemoveLimb(int ObjectId, int LimbId, std::string* Error = nullptr);
        void CloneObject(int DestId, int SourceId);
        void InstanceObject(int DestId, int SourceId);
        void DeleteObject(int ObjectId);
        bool DeleteObjects(int FirstObjectId, int LastObjectId, std::string* Error = nullptr);
        int ObjectExist(int ObjectId);

        // ── Position / rotation / scale ──────────────────────────────────
        void PositionObject(int ObjectId, float X, float Y, float Z);
        void RotateObject(int ObjectId, float XDegrees, float YDegrees, float ZDegrees);
        void XrotateObject(int ObjectId, float Degrees);
        void YrotateObject(int ObjectId, float Degrees);
        void ZrotateObject(int ObjectId, float Degrees);
        void ScaleObject(int ObjectId, float XPercent, float YPercent, float ZPercent);
        float ObjectPositionX(int ObjectId);
        float ObjectPositionY(int ObjectId);
        float ObjectPositionZ(int ObjectId);
        float ObjectAngleX(int ObjectId);
        float ObjectAngleY(int ObjectId);
        float ObjectAngleZ(int ObjectId);

        // ── Movement ─────────────────────────────────────────────────────
        void MoveObject(int ObjectId, float Step);
        void MoveObjectUp(int ObjectId, float Step);
        void MoveObjectDown(int ObjectId, float Step);
        void MoveObjectLeft(int ObjectId, float Step);
        void MoveObjectRight(int ObjectId, float Step);
        void PointObject(int ObjectId, float X, float Y, float Z);
        void TurnObjectLeft(int ObjectId, float Degrees);
        void TurnObjectRight(int ObjectId, float Degrees);
        void PitchObjectUp(int ObjectId, float Degrees);
        void PitchObjectDown(int ObjectId, float Degrees);
        void RollObjectLeft(int ObjectId, float Degrees);
        void RollObjectRight(int ObjectId, float Degrees);
        bool SetObjectToObjectOrientation(int DestId, int SrcId, int IncludePivot = 0, std::string* Error = nullptr);
        bool SetObjectToCameraOrientation(int ObjectId, std::string* Error = nullptr);

        // ── Collision ────────────────────────────────────────────────────
        int ObjectCollision(int ObjectId, int TargetObjectId);
        int ObjectHit(int ObjectId, int TargetObjectId);
        int LimbCollision(int ObjectA, int LimbA, int ObjectB, int LimbB);
        int LimbHit(int ObjectA, int LimbA, int ObjectB, int LimbB);
        bool SetObjectCollisionOff(int ObjectId, std::string* Error = nullptr);
        bool SetObjectCollisionOn(int ObjectId, std::string* Error = nullptr);
        bool SetObjectCollisionToBoxes(int ObjectId, std::string* Error = nullptr);
        bool SetObjectCollisionToSpheres(int ObjectId, std::string* Error = nullptr);
        bool SetObjectCollisionToPolygons(int ObjectId, std::string* Error = nullptr);
        bool MakeObjectCollisionBox(int ObjectId, float X1, float Y1, float Z1, float X2, float Y2, float Z2,
                                    int RotatedFlag, std::string* Error = nullptr);
        bool DeleteObjectCollisionBox(int ObjectId, std::string* Error = nullptr);
        float GetObjectCollisionX(int ObjectId = 0);
        float GetObjectCollisionY(int ObjectId = 0);
        float GetObjectCollisionZ(int ObjectId = 0);
        bool PickObject(int& Output, int ScreenX, int ScreenY, int ObjectStart, int ObjectEnd, int IgnoreCamera = 0,
                        std::string* Error = nullptr);
        void PickScreen(int ScreenX, int ScreenY, float Distance);
        float GetPickDistance() const;
        float GetPickVectorX() const;
        float GetPickVectorY() const;
        float GetPickVectorZ() const;

        // ── Static collision ─────────────────────────────────────────────
        bool MakeStaticCollisionBox(float X1, float Y1, float Z1, float X2, float Y2, float Z2,
                                    std::string* Error = nullptr);
        bool MakeStaticObject(int ObjectId, std::string* Error = nullptr);
        bool AttachObjectToStatic(int ObjectId, std::string* Error = nullptr);
        bool DetachObjectFromStatic(int ObjectId, std::string* Error = nullptr);
        void DeleteStaticObjects();
        void EnableStaticOcclusion(bool Enabled);
        [[nodiscard]] bool StaticOcclusionEnabled() const;
        [[nodiscard]] bool IsStaticRenderObject(int ObjectId) const;
        bool SetStaticObjectsTexture(int AddressMode, std::string* Error = nullptr);
        void SetStaticObjectsWireframe(bool Enabled);
        bool SaveStaticObjects(const std::string& Path, std::string* Error = nullptr) const;
        bool LoadStaticObjects(const std::string& Path, std::string* Error = nullptr);
        void SetGlobalCollision(bool Enabled);
        [[nodiscard]] bool GlobalCollisionEnabled() const;
        bool StaticLineOfSight(float& Output, float SourceX, float SourceY, float SourceZ, float DestinationX,
                               float DestinationY, float DestinationZ, float Width, float Accuracy,
                               std::string* Error = nullptr);
        float StaticLineOfSightX() const;
        float StaticLineOfSightY() const;
        float StaticLineOfSightZ() const;
        float GetStaticCollisionHit(float OldX, float OldY, float OldZ, float NewX, float NewY, float NewZ);
        // The documented form: a box moving from its old extents to its new ones. Returns 1 when
        // the new box overlaps a static collision box and stores the per-axis push-back that
        // undoes the penetration introduced by the move (sliding collision).
        float GetStaticCollisionHitBox(float OldX1, float OldY1, float OldZ1, float OldX2, float OldY2, float OldZ2,
                                       float NewX1, float NewY1, float NewZ1, float NewX2, float NewY2, float NewZ2);
        float GetStaticCollisionX();
        float GetStaticCollisionY();
        float GetStaticCollisionZ();

        // ── Texture / appearance ─────────────────────────────────────────
        void TextureObject(int ObjectId, int ImageId);
        bool SetObjectTexture(int ObjectId, int Stage, int AddressMode, int MipMode, std::string* Error = nullptr);
        bool SetLightMappingOn(int ObjectId, int ImageId, int ReverseStageMode = 0, std::string* Error = nullptr);
        bool SetDetailMappingOn(int ObjectId, int ImageId, std::string* Error = nullptr);
        bool SetSphereMappingOn(int ObjectId, int ImageId, std::string* Error = nullptr);
        bool SetBlendMappingOn(int ObjectId, int Stage, int ImageId, int CoordinateMode, int BlendMode,
                               std::string* Error = nullptr);
        bool SetCubeMappingOn(int ObjectId, const std::array<int, 6>& ImageIds, std::string* Error = nullptr);
        bool SetBumpMappingOn(int ObjectId, int ImageId, std::string* Error = nullptr);
        bool SetCartoonShadingOn(int ObjectId, int ShadeImageId, int EdgeImageId, std::string* Error = nullptr);
        bool SetRainbowShadingOn(int ObjectId, int ImageId, std::string* Error = nullptr);
        bool SetReflectionShadingOn(int ObjectId, std::string* Error = nullptr);
        bool SetShadowShadingOn(int ObjectId, std::string* Error = nullptr);
        // Surface maps. A limb of -1 means every mesh of the object; image or cube map 0 removes that map, and a
        // mesh left without any goes back to the fixed-function path.
        enum class SurfaceMap
        {
            Normal,
            Specular,
            Emissive,
            Reflection
        };
        bool SetSurfaceMap(int ObjectId, int LimbId, SurfaceMap Map, int ImageId, const char* Command,
                           std::string* Error = nullptr, float NormalStrength = 1.0f);
        bool SetSurfaceCubeMap(int ObjectId, int LimbId, int CubeMapId, float Amount, float Fresnel,
                               const char* Command, std::string* Error = nullptr);
        // The normal map's second layer exists once a command has named it.
        bool SetNormalMapScroll(int ObjectId, std::span<const SDL_FPoint> Layers, std::string* Error = nullptr);
        bool SetNormalMapScale(int ObjectId, std::span<const SDL_FPoint> Layers, std::string* Error = nullptr);
        bool ClearObjectMaps(int ObjectId, std::string* Error = nullptr);
        bool MakeCubeMap(int CubeMapId, const std::array<int, 6>& ImageIds, std::string* Error = nullptr);
        // A 4:3 horizontal cross or a 6:1 strip in the order +X, -X, +Y, -Y, +Z, -Z.
        bool LoadCubeMap(const std::string& Path, int CubeMapId, std::string* Error = nullptr);
        bool DeleteCubeMap(int CubeMapId, std::string* Error = nullptr);
        [[nodiscard]] int CubeMapExist(int CubeMapId) const;
        [[nodiscard]] const CubeMap* FindCubeMap(int CubeMapId) const;
        bool SetShadowShadingOn(int ObjectId, int MeshId, int ShadowRange, int UseShader, std::string* Error = nullptr);
        bool SetShadowShadingOff(int ObjectId, std::string* Error = nullptr);
        bool SetShadingOff(int ObjectId, std::string* Error = nullptr);
        bool SetObjectEffect(int ObjectId, int EffectId, std::string* Error = nullptr);
        bool SetLimbEffect(int ObjectId, int LimbId, int EffectId, std::string* Error = nullptr);
        void ClearEffectReferences(int EffectId);
        [[nodiscard]] bool ObjectEffect(int& Output, int ObjectId, std::string* Error = nullptr) const;
        [[nodiscard]] bool LimbEffect(int& Output, int ObjectId, int LimbId, std::string* Error = nullptr) const;
        bool SetObject(int ObjectId, int Wireframe, int Transparency, int Cull, std::string* Error = nullptr);
        bool SetObject(int ObjectId, int Wireframe, int Transparency, int Cull, int Filter,
                       std::string* Error = nullptr);
        bool SetObject(int ObjectId, int Wireframe, int Transparency, int Cull, int Filter, int Light,
                       std::string* Error = nullptr);
        bool SetObject(int ObjectId, int Wireframe, int Transparency, int Cull, int Filter, int Light, int Fog,
                       std::string* Error = nullptr);
        bool SetObject(int ObjectId, int Wireframe, int Transparency, int Cull, int Filter, int Light, int Fog,
                       int Ambient, std::string* Error = nullptr);
        bool SetObjectWireframe(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetObjectCull(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetObjectFilter(int ObjectId, int Filter, std::string* Error = nullptr);
        bool SetObjectLight(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetObjectFog(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetObjectAmbient(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool ScrollObjectTexture(int ObjectId, float U, float V, std::string* Error = nullptr);
        bool ScrollObjectTexture(int ObjectId, int Stage, float U, float V, std::string* Error = nullptr);
        bool RecalculateBounds(int ObjectId, const char* Command, std::string* Error = nullptr);
        bool ScaleObjectTexture(int ObjectId, float U, float V, std::string* Error = nullptr);
        bool ScaleObjectTexture(int ObjectId, int Stage, float U, float V, std::string* Error = nullptr);
        bool SetAlphaMappingOn(int ObjectId, float Percentage, std::string* Error = nullptr);
        void ColorObject(int ObjectId, int RgbPacked);
        bool ColorLimb(int ObjectId, int LimbId, int RgbPacked, std::string* Error = nullptr);
        void FadeObject(int ObjectId, int Percent);
        void GhostObjectOn(int ObjectId, int Mode = 0);
        void GhostObjectOff(int ObjectId);

        // ── Visibility / screen ──────────────────────────────────────────
        void ShowObject(int ObjectId);
        void HideObject(int ObjectId);
        int ObjectVisible(int ObjectId);
        bool ObjectScreenX(int& Output, int ObjectId, std::string* Error = nullptr);
        bool ObjectScreenY(int& Output, int ObjectId, std::string* Error = nullptr);
        bool ObjectInScreen(int& Output, int ObjectId, std::string* Error = nullptr);
        bool LockObjectOn(int ObjectId, std::string* Error = nullptr);
        bool LockObjectOff(int ObjectId, std::string* Error = nullptr);
        bool DisableObjectZdepth(int ObjectId, std::string* Error = nullptr);
        bool EnableObjectZdepth(int ObjectId, std::string* Error = nullptr);
        bool DisableObjectZread(int ObjectId, std::string* Error = nullptr);
        bool EnableObjectZread(int ObjectId, std::string* Error = nullptr);
        bool DisableObjectZwrite(int ObjectId, std::string* Error = nullptr);
        bool EnableObjectZwrite(int ObjectId, std::string* Error = nullptr);
        bool HideObjectBounds(int ObjectId, std::string* Error = nullptr);
        bool ShowObjectBounds(int ObjectId, int BoxOnly = 0, std::string* Error = nullptr);

        // ── DBPro extensions ────────────────────────────────────────────
        void SetObjectRadius(int ObjectId, float Radius);
        float IntersectObject(int ObjectId, float X1, float Y1, float Z1, float X2, float Y2, float Z2);
        float LimbScaleX(int ObjectId, int LimbId);
        float LimbScaleY(int ObjectId, int LimbId);
        float LimbScaleZ(int ObjectId, int LimbId);
        int CheckLimbLink(int ObjectId, int LimbId);
        void DeleteAllObjects();
        bool SetObjectTransparency(int ObjectId, int Mode, std::string* Error = nullptr);
        void AutomaticObjectCollision(int ObjectId, float Radius, int Response);
        float ObjectCollisionRadius(int ObjectId);
        float ObjectCollisionCenterX(int ObjectId);
        float ObjectCollisionCenterY(int ObjectId);
        float ObjectCollisionCenterZ(int ObjectId);
        bool SetObjectDiffuse(int ObjectId, uint32_t Color, int VertexDiffuseMode = 0, std::string* Error = nullptr);
        bool SetObjectAmbience(int ObjectId, uint32_t Color, std::string* Error = nullptr);
        bool SetObjectSpecular(int ObjectId, uint32_t Color, std::string* Error = nullptr);
        bool SetObjectSpecularPower(int ObjectId, float Power, std::string* Error = nullptr);
        bool SetObjectEmissive(int ObjectId, uint32_t Color, std::string* Error = nullptr);
        bool SetObjectNormals(int ObjectId, std::string* Error = nullptr);
        bool SetObjectSmoothing(int ObjectId, float Percentage, std::string* Error = nullptr);

        // ── DBPro particles ─────────────────────────────────────────────
        bool MakeParticles(int ParticleId, int ImageId, int Frequency, float Radius, std::string* Error = nullptr);
        bool DeleteParticles(int ParticleId, std::string* Error = nullptr);
        bool PositionParticles(int ParticleId, float X, float Y, float Z, std::string* Error = nullptr);
        bool RotateParticles(int ParticleId, float X, float Y, float Z, std::string* Error = nullptr);
        bool PositionParticleEmissions(int ParticleId, float X, float Y, float Z, std::string* Error = nullptr);
        bool ColorParticles(int ParticleId, int Red, int Green, int Blue, std::string* Error = nullptr);
        bool SetParticleSpeed(int ParticleId, float SecondsPerFrame, std::string* Error = nullptr);
        bool SetParticleEmissions(int ParticleId, int Emissions, std::string* Error = nullptr);
        bool SetParticleVelocity(int ParticleId, float Velocity, std::string* Error = nullptr);
        bool SetParticleGravity(int ParticleId, float Gravity, std::string* Error = nullptr);
        bool SetParticleLife(int ParticleId, int LifePercent, std::string* Error = nullptr);
        bool SetParticleFloor(int ParticleId, int Enabled, std::string* Error = nullptr);
        bool SetParticleChaos(int ParticleId, float Chaos, std::string* Error = nullptr);
        bool GhostParticlesOn(int ParticleId, int Mode, float Percentage = -1.0f, std::string* Error = nullptr);
        bool GhostParticlesOff(int ParticleId, std::string* Error = nullptr);
        bool HideParticles(int ParticleId, std::string* Error = nullptr);
        bool ShowParticles(int ParticleId, std::string* Error = nullptr);
        int ParticlesExist(int ParticleId) const;
        bool ParticlesPositionX(float& Output, int ParticleId, std::string* Error = nullptr) const;
        bool ParticlesPositionY(float& Output, int ParticleId, std::string* Error = nullptr) const;
        bool ParticlesPositionZ(float& Output, int ParticleId, std::string* Error = nullptr) const;
        bool ParticlesRotationX(float& Output, int ParticleId, std::string* Error = nullptr) const;
        bool ParticlesRotationY(float& Output, int ParticleId, std::string* Error = nullptr) const;
        bool ParticlesRotationZ(float& Output, int ParticleId, std::string* Error = nullptr) const;
        bool ParticleCount(std::size_t& Output, int ParticleId, std::string* Error = nullptr) const;

        // ── Limb operations ──────────────────────────────────────────────
        bool LinkLimb(int ObjectId, int ParentLimbId, int LimbId, std::string* Error = nullptr);
        bool TextureLimb(int ObjectId, int LimbId, int ImageId, std::string* Error = nullptr);
        bool TextureLimb(int ObjectId, int LimbId, int Stage, int ImageId, std::string* Error = nullptr);
        bool RotateLimb(int ObjectId, int LimbId, float XDegrees, float YDegrees, float ZDegrees, int BoundsFlag = 0,
                        std::string* Error = nullptr);
        bool OffsetLimb(int ObjectId, int LimbId, float X, float Y, float Z, int BoundsFlag = 0,
                        std::string* Error = nullptr);
        bool ScaleLimb(int ObjectId, int LimbId, float XPercent, float YPercent, float ZPercent, int BoundsFlag = 0,
                       std::string* Error = nullptr);
        bool SetLimbNormals(int ObjectId, int LimbId, std::string* Error = nullptr);
        bool SetLimbSmoothing(int ObjectId, int LimbId, float Percentage, std::string* Error = nullptr);
        bool ScrollLimbTexture(int ObjectId, int LimbId, float U, float V, std::string* Error = nullptr);
        bool ScrollLimbTexture(int ObjectId, int LimbId, int Stage, float U, float V, std::string* Error = nullptr);
        bool ScaleLimbTexture(int ObjectId, int LimbId, float U, float V, std::string* Error = nullptr);
        bool ScaleLimbTexture(int ObjectId, int LimbId, int Stage, float U, float V, std::string* Error = nullptr);
        bool HideLimb(int ObjectId, int LimbId, std::string* Error = nullptr);
        bool SetObjectExcluded(int ObjectId, bool Excluded, const char* Command, std::string* Error = nullptr);
        bool SetLimbExcluded(int ObjectId, int LimbId, bool Excluded, const char* Command,
                             std::string* Error = nullptr);
        bool SetObjectCameraMask(int ObjectId, int Mask, std::string* Error = nullptr);
        bool SetObjectFov(int ObjectId, float Degrees, std::string* Error = nullptr);
        bool EnableObjectDepthBias(int ObjectId, float Slope, float Constant, std::string* Error = nullptr);
        bool DisableObjectDepthBias(int ObjectId, std::string* Error = nullptr);
        bool CalculateObjectBounds(int ObjectId, std::string* Error = nullptr);
        bool LimbName(std::string& Output, int ObjectId, int LimbId, std::string* Error = nullptr) const;
        bool ShowLimb(int ObjectId, int LimbId, std::string* Error = nullptr);
        float LimbAngleX(int ObjectId, int LimbId);
        float LimbAngleY(int ObjectId, int LimbId);
        float LimbAngleZ(int ObjectId, int LimbId);
        float LimbPositionX(int ObjectId, int LimbId);
        float LimbPositionY(int ObjectId, int LimbId);
        float LimbPositionZ(int ObjectId, int LimbId);
        float LimbOffsetX(int ObjectId, int LimbId);
        float LimbOffsetY(int ObjectId, int LimbId);
        float LimbOffsetZ(int ObjectId, int LimbId);
        float LimbDirectionX(int ObjectId, int LimbId);
        float LimbDirectionY(int ObjectId, int LimbId);
        float LimbDirectionZ(int ObjectId, int LimbId);
        int LimbExist(int ObjectId, int LimbId);
        int LimbVisible(int ObjectId, int LimbId);

        // ── Mutable vertex data ─────────────────────────────────────────
        bool LockVertexdataForMesh(int MeshId, std::string* Error = nullptr);
        bool LockVertexdataForLimb(int ObjectId, int LimbId, int LockMode = 0, std::string* Error = nullptr);
        void UnlockVertexdata();
        bool AddMeshToVertexdata(int MeshId, std::string* Error = nullptr);
        bool DeleteMeshFromVertexdata(int VertexStart, int VertexEnd, int IndexStart, int IndexEnd,
                                      std::string* Error = nullptr);
        bool ConvertMeshToVertexdata(int MeshId, std::string* Error = nullptr);
        bool VertexdataVertexCount(int& Output, std::string* Error = nullptr) const;
        bool VertexdataIndexCount(int& Output, std::string* Error = nullptr) const;
        bool SetVertexdataPosition(int Index, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetVertexdataNormals(int Index, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetVertexdataDiffuse(int Index, uint32_t Color, std::string* Error = nullptr);
        bool SetVertexdataUv(int Index, int Stage, float U, float V, std::string* Error = nullptr);
        bool SetIndexdata(int Index, int Value, std::string* Error = nullptr);
        bool VertexdataPositionX(float& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataPositionY(float& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataPositionZ(float& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataNormalsX(float& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataNormalsY(float& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataNormalsZ(float& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataDiffuse(int& Output, int Index, std::string* Error = nullptr) const;
        bool VertexdataU(float& Output, int Index, int Stage = 0, std::string* Error = nullptr) const;
        bool VertexdataV(float& Output, int Index, int Stage = 0, std::string* Error = nullptr) const;
        bool Indexdata(int& Output, int Index, std::string* Error = nullptr) const;

        // ── Glue ─────────────────────────────────────────────────────────
        bool GlueObjectToLimb(int ObjectId, int TargetObjectId, int LimbId, int Mode = 0, std::string* Error = nullptr);
        bool UnglueObject(int ObjectId, std::string* Error = nullptr);
        bool FixObjectPivot(int ObjectId, std::string* Error = nullptr);
        bool SetObjectRotationXyz(int ObjectId, std::string* Error = nullptr);
        bool SetObjectRotationZyx(int ObjectId, std::string* Error = nullptr);

        // ── Animation ────────────────────────────────────────────────────
        bool SetObjectSpeed(int ObjectId, float Speed, std::string* Error = nullptr);
        float ObjectSpeed(int ObjectId);
        bool SetObjectFrame(int ObjectId, int Frame, std::string* Error = nullptr);
        bool SetObjectFrame(int ObjectId, float Frame, std::string* Error = nullptr);
        int ObjectFrame(int ObjectId);
        bool PlayObject(int ObjectId, std::string* Error = nullptr);
        bool PlayObject(int ObjectId, int StartFrame, int EndFrame, std::string* Error = nullptr);
        bool LoopObject(int ObjectId, std::string* Error = nullptr);
        bool LoopObject(int ObjectId, int StartFrame, int EndFrame, std::string* Error = nullptr);
        bool StopObject(int ObjectId, std::string* Error = nullptr);
        int ObjectPlaying(int ObjectId);
        int ObjectLooping(int ObjectId);
        bool SetObjectInterpolation(int ObjectId, int Value, std::string* Error = nullptr);
        int ObjectInterpolation(int ObjectId);
        bool SetObjectKeyframe(int ObjectId, int Frame, std::string* Error = nullptr);
        bool ClearObjectKeyframe(int ObjectId, int Frame, std::string* Error = nullptr);
        bool ClearAllObjectKeyframes(int ObjectId, std::string* Error = nullptr);
        bool SaveObjectAnimation(const std::string& Path, int ObjectId, std::string* Error = nullptr) const;
        bool AppendObjectAnimation(const std::string& ModelPath, const std::string& AnimationPath, int ObjectId,
                                   int StartFrame, std::string* Error = nullptr);
        bool UpdateObjectKeyframeAnimation(Object3D& Object, double ElapsedSeconds);

        // ── Size queries ─────────────────────────────────────────────────
        float ObjectSizeX(int ObjectId, int ActualSize = 0);
        float ObjectSizeY(int ObjectId, int ActualSize = 0);
        float ObjectSizeZ(int ObjectId, int ActualSize = 0);
        float ObjectSize(int ObjectId, int ActualSize = 0);

      private:
        [[nodiscard]] std::vector<AABB> CurrentStaticCollisionBoxes() const;

        // The kind of a MAKE OBJECT primitive (0 to 4), then the bits of its parameters; or 5 for a mesh loaded
        // from a file, then the hash of its path and the mesh's index in the file.
        using GeometryKey = std::array<uint32_t, 5>;

        // A geometry that the objects made the same way share while one of them holds it unchanged, so that they
        // draw as instances of one mesh.
        struct SharedGeometryEntry
        {
            std::weak_ptr<const MeshGeometry> Geometry;
            uint64_t Revision = 0;
        };

        // The live, unchanged geometry recorded for a key, if any.
        [[nodiscard]] std::shared_ptr<const MeshGeometry> FindSharedGeometry(const GeometryKey& Key) const;
        void RememberSharedGeometry(const GeometryKey& Key, const Mesh& Value);
        template <typename Build> [[nodiscard]] Mesh PrimitiveMesh(const GeometryKey& Key, Build&& Make);
        // Gives a loaded mesh the geometry an earlier load of the same file and mesh still holds, when both are
        // equal.
        void ShareLoadedGeometry(const std::string& Path, std::size_t MeshIndex, Mesh& Loaded);

        EngineKernel& Kernel_;
        std::unordered_map<int, Object3D> Objects_;
        std::unordered_map<int, CubeMap> CubeMaps_;
        bool AddCubeMap(int CubeMapId, const std::array<SDL_Surface*, 6>& Faces, const char* Command,
                        std::string* Error);
        std::vector<Object3D> StaticObjects_;
        std::unordered_set<int> AttachedStaticObjectIds_;
        std::unordered_map<int, std::vector<Mesh>> Meshes_;
        std::map<GeometryKey, SharedGeometryEntry> SharedGeometries_;
        // Entries of geometries no object holds any more are dropped once the map reaches this size.
        std::size_t SharedGeometryPruneSize_ = 256;
        std::unordered_map<int, ParticleEmitter> Particles_;
        std::unique_ptr<SpatialHash> SpatialHash_;
        std::vector<AABB> StaticBoxes_;
        float CollisionSlideX_ = 0.0f;
        float CollisionSlideY_ = 0.0f;
        float CollisionSlideZ_ = 0.0f;
        std::unordered_map<int, Vec3> CollisionFeedback_;
        int LastCollisionObject_ = 0;
        std::unordered_map<int, int> ObjectHitHistory_;
        std::unordered_map<int64_t, int> LimbHitHistory_;
        float PickDistance_ = 0.0f;
        float PickVectorX_ = 0.0f;
        float PickVectorY_ = 0.0f;
        float PickVectorZ_ = 0.0f;
        float StaticHitX_ = 0.0f;
        float StaticHitY_ = 0.0f;
        float StaticHitZ_ = 0.0f;
        bool StaticOcclusionEnabled_ = true;
        bool GlobalCollisionEnabled_ = false;
        enum class VertexLockKind
        {
            None,
            Mesh,
            Limb,
        };
        VertexLockKind VertexLockKind_ = VertexLockKind::None;
        int VertexLockId_ = 0;
        int VertexLockLimb_ = 0;

        // ── Private helpers ──────────────────────────────────────────────
        [[nodiscard]] Object3D NewObjectState() const;
        void NotifyObjectCreated(int ObjectId);
        Object3D* RequireObject(int ObjectId, const char* Command, std::string* Error = nullptr);
        const Object3D* RequireObject(int ObjectId, const char* Command, std::string* Error = nullptr) const;
        ParticleEmitter* RequireParticles(int ParticleId, const char* Command, std::string* Error = nullptr);
        const ParticleEmitter* RequireParticles(int ParticleId, const char* Command,
                                                std::string* Error = nullptr) const;
        bool RequireLimb(const Object3D& Object, int ObjectId, int LimbId, const char* Command,
                         std::string* Error = nullptr) const;
        void RecalculateObjectBounds(Object3D& Object);
        Mesh* RequireLockedVertexMesh(const char* Command, std::string* Error = nullptr);
        const Mesh* RequireLockedVertexMesh(const char* Command, std::string* Error = nullptr) const;
        bool SetObjectFlags(int ObjectId, int Wireframe, int Transparency, int Cull, int Filter, int Light, int Fog,
                            int Ambient, std::string* Error = nullptr);
    };

} // namespace dbc
