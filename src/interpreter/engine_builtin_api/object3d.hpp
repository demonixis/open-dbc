#pragma once

// Runtime inline wrappers -- forward to ObjectManager methods.

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/object_manager.hpp"

#include <string>

namespace dbc
{

    // ── Object CRUD ──────────────────────────────────────────────────

    inline bool LoadObject(EngineKernel& K, const std::string& Path, int ObjectId, int LoadMode = 0,
                           int DivideTextureSize = 0, std::string* Error = nullptr)
    {
        return K.Objects().LoadObject(Path, ObjectId, LoadMode, DivideTextureSize, Error);
    }

    inline bool SaveObject(EngineKernel& K, const std::string& Path, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SaveObject(Path, ObjectId, Error);
    }

    inline bool LoadMesh(EngineKernel& K, const std::string& Path, int MeshId, std::string* Error = nullptr)
    {
        return K.Objects().LoadMesh(Path, MeshId, Error);
    }

    inline bool SaveMesh(EngineKernel& K, const std::string& Path, int MeshId, std::string* Error = nullptr)
    {
        return K.Objects().SaveMesh(Path, MeshId, Error);
    }

    inline bool DeleteMesh(EngineKernel& K, int MeshId, std::string* Error = nullptr)
    {
        return K.Objects().DeleteMesh(MeshId, Error);
    }

    inline int MeshExist(EngineKernel& K, int MeshId)
    {
        return K.Objects().MeshExist(MeshId);
    }

    inline bool AppendObject(EngineKernel& K, const std::string& Path, int ObjectId, int StartFrame,
                             std::string* Error = nullptr)
    {
        return K.Objects().AppendObject(Path, ObjectId, StartFrame, Error);
    }

    inline int TotalObjectFrames(EngineKernel& K, int ObjectId)
    {
        return K.Objects().TotalObjectFrames(ObjectId);
    }

    inline bool MakeObjectPlain(EngineKernel& K, int ObjectId, float Width, float Height, int Orientation = 0,
                                std::string* Error = nullptr)
    {
        return K.Objects().MakeObjectPlain(ObjectId, Width, Height, Orientation, Error);
    }

    inline void MakeObjectCube(EngineKernel& K, int ObjectId, float Size)
    {
        K.Objects().MakeObjectCube(ObjectId, Size);
    }

    inline void MakeObjectBox(EngineKernel& K, int ObjectId, float Width, float Height, float Depth)
    {
        K.Objects().MakeObjectBox(ObjectId, Width, Height, Depth);
    }

    inline void MakeObjectSphere(EngineKernel& K, int ObjectId, float Radius, int Rings = 12, int Segments = 16)
    {
        K.Objects().MakeObjectSphere(ObjectId, Radius, Rings, Segments);
    }

    inline void MakeObjectCylinder(EngineKernel& K, int ObjectId, float Radius, float Height, int Segments = 16)
    {
        K.Objects().MakeObjectCylinder(ObjectId, Radius, Height, Segments);
    }

    inline void MakeObjectCone(EngineKernel& K, int ObjectId, float Radius, float Height, int Segments = 16)
    {
        K.Objects().MakeObjectCone(ObjectId, Radius, Height, Segments);
    }

    inline void MakeObjectTriangle(EngineKernel& K, int ObjectId, float X1, float Y1, float Z1, float X2, float Y2,
                                   float Z2, float X3, float Y3, float Z3)
    {
        K.Objects().MakeObjectTriangle(ObjectId, X1, Y1, Z1, X2, Y2, Z2, X3, Y3, Z3);
    }

    inline bool MakeMeshFromObject(EngineKernel& K, int MeshId, int ObjectId, int IgnoreMode = 0,
                                   std::string* Error = nullptr)
    {
        return K.Objects().MakeMeshFromObject(MeshId, ObjectId, IgnoreMode, Error);
    }

    inline void MakeObject(EngineKernel& K, int ObjectId, int MeshId, int ImageId)
    {
        K.Objects().MakeObject(ObjectId, MeshId, ImageId);
    }

    inline bool MakeObjectFromLimb(EngineKernel& K, int ObjectId, int SourceObjectId, int LimbId, int AllLimbs = 0,
                                   std::string* Error = nullptr)
    {
        return K.Objects().MakeObjectFromLimb(ObjectId, SourceObjectId, LimbId, AllLimbs, Error);
    }

    inline bool AddLimb(EngineKernel& K, int ObjectId, int LimbId, int SourceMeshId, std::string* Error = nullptr)
    {
        return K.Objects().AddLimb(ObjectId, LimbId, SourceMeshId, Error);
    }

    inline bool RemoveLimb(EngineKernel& K, int ObjectId, int LimbId, std::string* Error = nullptr)
    {
        return K.Objects().RemoveLimb(ObjectId, LimbId, Error);
    }

    inline void CloneObject(EngineKernel& K, int DestId, int SourceId)
    {
        K.Objects().CloneObject(DestId, SourceId);
    }

    inline void InstanceObject(EngineKernel& K, int DestId, int SourceId)
    {
        K.Objects().InstanceObject(DestId, SourceId);
    }

    inline void DeleteObject(EngineKernel& K, int ObjectId)
    {
        K.Objects().DeleteObject(ObjectId);
    }

    inline bool DeleteObjects(EngineKernel& K, int FirstObjectId, int LastObjectId, std::string* Error = nullptr)
    {
        return K.Objects().DeleteObjects(FirstObjectId, LastObjectId, Error);
    }

    inline int ObjectExist(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectExist(ObjectId);
    }

    inline bool LockVertexdataForMesh(EngineKernel& K, int MeshId, std::string* Error = nullptr)
    {
        return K.Objects().LockVertexdataForMesh(MeshId, Error);
    }

    inline bool LockVertexdataForLimb(EngineKernel& K, int ObjectId, int LimbId, int LockMode = 0,
                                      std::string* Error = nullptr)
    {
        return K.Objects().LockVertexdataForLimb(ObjectId, LimbId, LockMode, Error);
    }

    inline void UnlockVertexdata(EngineKernel& K)
    {
        K.Objects().UnlockVertexdata();
    }

    // ── Position / rotation / scale ──────────────────────────────────

    inline void PositionObject(EngineKernel& K, int ObjectId, float X, float Y, float Z)
    {
        K.Objects().PositionObject(ObjectId, X, Y, Z);
    }

    inline void RotateObject(EngineKernel& K, int ObjectId, float XDegrees, float YDegrees, float ZDegrees)
    {
        K.Objects().RotateObject(ObjectId, XDegrees, YDegrees, ZDegrees);
    }

    inline void XrotateObject(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().XrotateObject(ObjectId, Degrees);
    }

    inline void YrotateObject(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().YrotateObject(ObjectId, Degrees);
    }

    inline void ZrotateObject(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().ZrotateObject(ObjectId, Degrees);
    }

    inline void ScaleObject(EngineKernel& K, int ObjectId, float XPercent, float YPercent, float ZPercent)
    {
        K.Objects().ScaleObject(ObjectId, XPercent, YPercent, ZPercent);
    }

    inline float ObjectPositionX(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectPositionX(ObjectId);
    }

    inline float ObjectPositionY(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectPositionY(ObjectId);
    }

    inline float ObjectPositionZ(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectPositionZ(ObjectId);
    }

    inline float ObjectAngleX(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectAngleX(ObjectId);
    }

    inline float ObjectAngleY(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectAngleY(ObjectId);
    }

    inline float ObjectAngleZ(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectAngleZ(ObjectId);
    }

    // ── Movement ─────────────────────────────────────────────────────

    inline void MoveObject(EngineKernel& K, int ObjectId, float Step)
    {
        K.Objects().MoveObject(ObjectId, Step);
    }

    inline void MoveObjectUp(EngineKernel& K, int ObjectId, float Step)
    {
        K.Objects().MoveObjectUp(ObjectId, Step);
    }

    inline void MoveObjectDown(EngineKernel& K, int ObjectId, float Step)
    {
        K.Objects().MoveObjectDown(ObjectId, Step);
    }

    inline void MoveObjectLeft(EngineKernel& K, int ObjectId, float Step)
    {
        K.Objects().MoveObjectLeft(ObjectId, Step);
    }

    inline void MoveObjectRight(EngineKernel& K, int ObjectId, float Step)
    {
        K.Objects().MoveObjectRight(ObjectId, Step);
    }

    inline void PointObject(EngineKernel& K, int ObjectId, float X, float Y, float Z)
    {
        K.Objects().PointObject(ObjectId, X, Y, Z);
    }

    inline void TurnObjectLeft(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().TurnObjectLeft(ObjectId, Degrees);
    }

    inline void TurnObjectRight(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().TurnObjectRight(ObjectId, Degrees);
    }

    inline void PitchObjectUp(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().PitchObjectUp(ObjectId, Degrees);
    }

    inline void PitchObjectDown(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().PitchObjectDown(ObjectId, Degrees);
    }

    inline void RollObjectLeft(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().RollObjectLeft(ObjectId, Degrees);
    }

    inline void RollObjectRight(EngineKernel& K, int ObjectId, float Degrees)
    {
        K.Objects().RollObjectRight(ObjectId, Degrees);
    }

    inline bool SetObjectToObjectOrientation(EngineKernel& K, int DestId, int SrcId, int IncludePivot = 0,
                                             std::string* Error = nullptr)
    {
        return K.Objects().SetObjectToObjectOrientation(DestId, SrcId, IncludePivot, Error);
    }

    inline bool SetObjectToCameraOrientation(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectToCameraOrientation(ObjectId, Error);
    }

    // ── Collision ────────────────────────────────────────────────────

    inline int ObjectCollision(EngineKernel& K, int ObjectId, int TargetObjectId)
    {
        return K.Objects().ObjectCollision(ObjectId, TargetObjectId);
    }

    inline bool SetObjectCollisionOff(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectCollisionOff(ObjectId, Error);
    }

    inline bool SetObjectCollisionOn(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectCollisionOn(ObjectId, Error);
    }

    inline bool SetObjectCollisionToBoxes(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectCollisionToBoxes(ObjectId, Error);
    }

    inline bool SetObjectCollisionToSpheres(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectCollisionToSpheres(ObjectId, Error);
    }

    inline bool SetObjectCollisionToPolygons(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectCollisionToPolygons(ObjectId, Error);
    }

    inline bool MakeObjectCollisionBox(EngineKernel& K, int ObjectId, float X1, float Y1, float Z1, float X2, float Y2,
                                       float Z2, int RotatedFlag, std::string* Error = nullptr)
    {
        return K.Objects().MakeObjectCollisionBox(ObjectId, X1, Y1, Z1, X2, Y2, Z2, RotatedFlag, Error);
    }

    inline bool DeleteObjectCollisionBox(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().DeleteObjectCollisionBox(ObjectId, Error);
    }

    inline float GetObjectCollisionX(EngineKernel& K, int ObjectId = 0)
    {
        return K.Objects().GetObjectCollisionX(ObjectId);
    }

    inline float GetObjectCollisionY(EngineKernel& K, int ObjectId = 0)
    {
        return K.Objects().GetObjectCollisionY(ObjectId);
    }

    inline float GetObjectCollisionZ(EngineKernel& K, int ObjectId = 0)
    {
        return K.Objects().GetObjectCollisionZ(ObjectId);
    }

    // ── Static collision ─────────────────────────────────────────────

    inline bool MakeStaticCollisionBox(EngineKernel& K, float X1, float Y1, float Z1, float X2, float Y2, float Z2,
                                       std::string* Error = nullptr)
    {
        return K.Objects().MakeStaticCollisionBox(X1, Y1, Z1, X2, Y2, Z2, Error);
    }

    inline bool MakeStaticObject(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().MakeStaticObject(ObjectId, Error);
    }

    inline bool AttachObjectToStatic(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().AttachObjectToStatic(ObjectId, Error);
    }

    inline bool DetachObjectFromStatic(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().DetachObjectFromStatic(ObjectId, Error);
    }

    inline void DeleteStaticObjects(EngineKernel& K)
    {
        K.Objects().DeleteStaticObjects();
    }

    inline float GetStaticCollisionHitBox(EngineKernel& K, float Ox1, float Oy1, float Oz1, float Ox2, float Oy2,
                                          float Oz2, float Nx1, float Ny1, float Nz1, float Nx2, float Ny2, float Nz2)
    {
        return K.Objects().GetStaticCollisionHitBox(Ox1, Oy1, Oz1, Ox2, Oy2, Oz2, Nx1, Ny1, Nz1, Nx2, Ny2, Nz2);
    }

    inline float GetStaticCollisionHit(EngineKernel& K, float OldX, float OldY, float OldZ, float NewX, float NewY,
                                       float NewZ)
    {
        return K.Objects().GetStaticCollisionHit(OldX, OldY, OldZ, NewX, NewY, NewZ);
    }

    inline float GetStaticCollisionX(EngineKernel& K)
    {
        return K.Objects().GetStaticCollisionX();
    }

    inline float GetStaticCollisionY(EngineKernel& K)
    {
        return K.Objects().GetStaticCollisionY();
    }

    inline float GetStaticCollisionZ(EngineKernel& K)
    {
        return K.Objects().GetStaticCollisionZ();
    }

    inline bool StaticLineOfSight(EngineKernel& K, float& Output, float Sx, float Sy, float Sz, float Dx, float Dy,
                                  float Dz, float Width, float Accuracy, std::string* Error = nullptr)
    {
        return K.Objects().StaticLineOfSight(Output, Sx, Sy, Sz, Dx, Dy, Dz, Width, Accuracy, Error);
    }

    // ── Texture / appearance ─────────────────────────────────────────

    inline void TextureObject(EngineKernel& K, int ObjectId, int ImageId)
    {
        K.Objects().TextureObject(ObjectId, ImageId);
    }

    inline bool SetObject(EngineKernel& K, int ObjectId, int Wireframe, int Transparency, int Cull,
                          std::string* Error = nullptr)
    {
        return K.Objects().SetObject(ObjectId, Wireframe, Transparency, Cull, Error);
    }

    inline bool SetObject(EngineKernel& K, int ObjectId, int Wireframe, int Transparency, int Cull, int Filter,
                          std::string* Error = nullptr)
    {
        return K.Objects().SetObject(ObjectId, Wireframe, Transparency, Cull, Filter, Error);
    }

    inline bool SetObject(EngineKernel& K, int ObjectId, int Wireframe, int Transparency, int Cull, int Filter,
                          int Light, std::string* Error = nullptr)
    {
        return K.Objects().SetObject(ObjectId, Wireframe, Transparency, Cull, Filter, Light, Error);
    }

    inline bool SetObject(EngineKernel& K, int ObjectId, int Wireframe, int Transparency, int Cull, int Filter,
                          int Light, int Fog, std::string* Error = nullptr)
    {
        return K.Objects().SetObject(ObjectId, Wireframe, Transparency, Cull, Filter, Light, Fog, Error);
    }

    inline bool SetObject(EngineKernel& K, int ObjectId, int Wireframe, int Transparency, int Cull, int Filter,
                          int Light, int Fog, int Ambient, std::string* Error = nullptr)
    {
        return K.Objects().SetObject(ObjectId, Wireframe, Transparency, Cull, Filter, Light, Fog, Ambient, Error);
    }

    inline bool ScrollObjectTexture(EngineKernel& K, int ObjectId, float U, float V, std::string* Error = nullptr)
    {
        return K.Objects().ScrollObjectTexture(ObjectId, U, V, Error);
    }

    inline void ColorObject(EngineKernel& K, int ObjectId, int RgbPacked)
    {
        K.Objects().ColorObject(ObjectId, RgbPacked);
    }

    inline void FadeObject(EngineKernel& K, int ObjectId, int Percent)
    {
        K.Objects().FadeObject(ObjectId, Percent);
    }

    inline void GhostObjectOn(EngineKernel& K, int ObjectId, int Mode = 0)
    {
        K.Objects().GhostObjectOn(ObjectId, Mode);
    }

    inline void GhostObjectOff(EngineKernel& K, int ObjectId)
    {
        K.Objects().GhostObjectOff(ObjectId);
    }

    // ── Visibility / screen ──────────────────────────────────────────

    inline void ShowObject(EngineKernel& K, int ObjectId)
    {
        K.Objects().ShowObject(ObjectId);
    }

    inline void HideObject(EngineKernel& K, int ObjectId)
    {
        K.Objects().HideObject(ObjectId);
    }

    inline int ObjectVisible(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectVisible(ObjectId);
    }

    inline bool ObjectScreenX(EngineKernel& K, int& Output, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().ObjectScreenX(Output, ObjectId, Error);
    }

    inline bool ObjectScreenY(EngineKernel& K, int& Output, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().ObjectScreenY(Output, ObjectId, Error);
    }

    inline bool ObjectInScreen(EngineKernel& K, int& Output, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().ObjectInScreen(Output, ObjectId, Error);
    }

    inline bool LockObjectOn(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().LockObjectOn(ObjectId, Error);
    }

    inline bool LockObjectOff(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().LockObjectOff(ObjectId, Error);
    }

    inline bool DisableObjectZdepth(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().DisableObjectZdepth(ObjectId, Error);
    }

    inline bool EnableObjectZdepth(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().EnableObjectZdepth(ObjectId, Error);
    }

    // ── Limb operations ──────────────────────────────────────────────

    inline bool LinkLimb(EngineKernel& K, int ObjectId, int ParentLimbId, int LimbId, std::string* Error = nullptr)
    {
        return K.Objects().LinkLimb(ObjectId, ParentLimbId, LimbId, Error);
    }

    inline bool TextureLimb(EngineKernel& K, int ObjectId, int LimbId, int ImageId, std::string* Error = nullptr)
    {
        return K.Objects().TextureLimb(ObjectId, LimbId, ImageId, Error);
    }

    inline bool TextureLimb(EngineKernel& K, int ObjectId, int LimbId, int Stage, int ImageId,
                            std::string* Error = nullptr)
    {
        return K.Objects().TextureLimb(ObjectId, LimbId, Stage, ImageId, Error);
    }

    inline bool RotateLimb(EngineKernel& K, int ObjectId, int LimbId, float XDegrees, float YDegrees, float ZDegrees,
                           int BoundsFlag = 0, std::string* Error = nullptr)
    {
        return K.Objects().RotateLimb(ObjectId, LimbId, XDegrees, YDegrees, ZDegrees, BoundsFlag, Error);
    }

    inline bool OffsetLimb(EngineKernel& K, int ObjectId, int LimbId, float X, float Y, float Z, int BoundsFlag = 0,
                           std::string* Error = nullptr)
    {
        return K.Objects().OffsetLimb(ObjectId, LimbId, X, Y, Z, BoundsFlag, Error);
    }

    inline bool ScaleLimb(EngineKernel& K, int ObjectId, int LimbId, float XPercent, float YPercent, float ZPercent,
                          int BoundsFlag = 0, std::string* Error = nullptr)
    {
        return K.Objects().ScaleLimb(ObjectId, LimbId, XPercent, YPercent, ZPercent, BoundsFlag, Error);
    }

    inline bool ScrollLimbTexture(EngineKernel& K, int ObjectId, int LimbId, float U, float V,
                                  std::string* Error = nullptr)
    {
        return K.Objects().ScrollLimbTexture(ObjectId, LimbId, U, V, Error);
    }

    inline bool ScrollLimbTexture(EngineKernel& K, int ObjectId, int LimbId, int Stage, float U, float V,
                                  std::string* Error = nullptr)
    {
        return K.Objects().ScrollLimbTexture(ObjectId, LimbId, Stage, U, V, Error);
    }

    inline bool ScaleLimbTexture(EngineKernel& K, int ObjectId, int LimbId, float U, float V,
                                 std::string* Error = nullptr)
    {
        return K.Objects().ScaleLimbTexture(ObjectId, LimbId, U, V, Error);
    }

    inline bool ScaleLimbTexture(EngineKernel& K, int ObjectId, int LimbId, int Stage, float U, float V,
                                 std::string* Error = nullptr)
    {
        return K.Objects().ScaleLimbTexture(ObjectId, LimbId, Stage, U, V, Error);
    }

    inline bool HideLimb(EngineKernel& K, int ObjectId, int LimbId, std::string* Error = nullptr)
    {
        return K.Objects().HideLimb(ObjectId, LimbId, Error);
    }

    inline bool ShowLimb(EngineKernel& K, int ObjectId, int LimbId, std::string* Error = nullptr)
    {
        return K.Objects().ShowLimb(ObjectId, LimbId, Error);
    }

    inline float LimbAngleX(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbAngleX(ObjectId, LimbId);
    }

    inline float LimbAngleY(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbAngleY(ObjectId, LimbId);
    }

    inline float LimbAngleZ(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbAngleZ(ObjectId, LimbId);
    }

    inline float LimbPositionX(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbPositionX(ObjectId, LimbId);
    }

    inline float LimbPositionY(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbPositionY(ObjectId, LimbId);
    }

    inline float LimbPositionZ(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbPositionZ(ObjectId, LimbId);
    }

    inline float LimbOffsetX(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbOffsetX(ObjectId, LimbId);
    }

    inline float LimbOffsetY(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbOffsetY(ObjectId, LimbId);
    }

    inline float LimbOffsetZ(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbOffsetZ(ObjectId, LimbId);
    }

    inline float LimbDirectionX(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbDirectionX(ObjectId, LimbId);
    }

    inline float LimbDirectionY(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbDirectionY(ObjectId, LimbId);
    }

    inline float LimbDirectionZ(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbDirectionZ(ObjectId, LimbId);
    }

    inline int LimbExist(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbExist(ObjectId, LimbId);
    }

    inline int LimbVisible(EngineKernel& K, int ObjectId, int LimbId)
    {
        return K.Objects().LimbVisible(ObjectId, LimbId);
    }

    // ── Glue ─────────────────────────────────────────────────────────

    inline bool GlueObjectToLimb(EngineKernel& K, int ObjectId, int TargetObjectId, int LimbId, int Mode = 0,
                                 std::string* Error = nullptr)
    {
        return K.Objects().GlueObjectToLimb(ObjectId, TargetObjectId, LimbId, Mode, Error);
    }

    inline bool UnglueObject(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().UnglueObject(ObjectId, Error);
    }

    inline bool FixObjectPivot(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().FixObjectPivot(ObjectId, Error);
    }

    inline bool SetObjectRotationXyz(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectRotationXyz(ObjectId, Error);
    }

    inline bool SetObjectRotationZyx(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectRotationZyx(ObjectId, Error);
    }

    // ── Animation ────────────────────────────────────────────────────

    inline bool SetObjectSpeed(EngineKernel& K, int ObjectId, float Speed, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectSpeed(ObjectId, Speed, Error);
    }

    inline float ObjectSpeed(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectSpeed(ObjectId);
    }

    inline bool SetObjectFrame(EngineKernel& K, int ObjectId, int Frame, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectFrame(ObjectId, Frame, Error);
    }

    inline bool SetObjectFrame(EngineKernel& K, int ObjectId, float Frame, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectFrame(ObjectId, Frame, Error);
    }

    inline int ObjectFrame(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectFrame(ObjectId);
    }

    inline bool PlayObject(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().PlayObject(ObjectId, Error);
    }

    inline bool PlayObject(EngineKernel& K, int ObjectId, int StartFrame, int EndFrame, std::string* Error = nullptr)
    {
        return K.Objects().PlayObject(ObjectId, StartFrame, EndFrame, Error);
    }

    inline bool LoopObject(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().LoopObject(ObjectId, Error);
    }

    inline bool LoopObject(EngineKernel& K, int ObjectId, int StartFrame, int EndFrame, std::string* Error = nullptr)
    {
        return K.Objects().LoopObject(ObjectId, StartFrame, EndFrame, Error);
    }

    inline bool StopObject(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().StopObject(ObjectId, Error);
    }

    inline int ObjectPlaying(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectPlaying(ObjectId);
    }

    inline int ObjectLooping(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectLooping(ObjectId);
    }

    inline bool SetObjectInterpolation(EngineKernel& K, int ObjectId, int Value, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectInterpolation(ObjectId, Value, Error);
    }

    inline int ObjectInterpolation(EngineKernel& K, int ObjectId)
    {
        return K.Objects().ObjectInterpolation(ObjectId);
    }

    inline bool SetObjectKeyframe(EngineKernel& K, int ObjectId, int Frame, std::string* Error = nullptr)
    {
        return K.Objects().SetObjectKeyframe(ObjectId, Frame, Error);
    }

    inline bool ClearObjectKeyframe(EngineKernel& K, int ObjectId, int Frame, std::string* Error = nullptr)
    {
        return K.Objects().ClearObjectKeyframe(ObjectId, Frame, Error);
    }

    inline bool ClearAllObjectKeyframes(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        return K.Objects().ClearAllObjectKeyframes(ObjectId, Error);
    }

    // ── Size queries ─────────────────────────────────────────────────

    inline float ObjectSizeX(EngineKernel& K, int ObjectId, int ActualSize = 0)
    {
        return K.Objects().ObjectSizeX(ObjectId, ActualSize);
    }

    inline float ObjectSizeY(EngineKernel& K, int ObjectId, int ActualSize = 0)
    {
        return K.Objects().ObjectSizeY(ObjectId, ActualSize);
    }

    inline float ObjectSizeZ(EngineKernel& K, int ObjectId, int ActualSize = 0)
    {
        return K.Objects().ObjectSizeZ(ObjectId, ActualSize);
    }

    inline float ObjectSize(EngineKernel& K, int ObjectId, int ActualSize = 0)
    {
        return K.Objects().ObjectSize(ObjectId, ActualSize);
    }

} // namespace dbc
