#pragma once

// The transforms object meshes apply per vertex, in a header so that they inline into their callers.

#include "engine_kernel.hpp"
#include "renderer_internal.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace dbc
{
    inline Vec3 ApplyAiTransformPoint(const aiMatrix4x4& M, const Vec3& P)
    {
        return Vec3{
            M.a1 * P.X + M.a2 * P.Y + M.a3 * P.Z + M.a4,
            M.b1 * P.X + M.b2 * P.Y + M.b3 * P.Z + M.b4,
            M.c1 * P.X + M.c2 * P.Y + M.c3 * P.Z + M.c4,
        };
    }

    inline Vec3 ApplyAiTransformVector(const aiMatrix4x4& M, const Vec3& V)
    {
        return Vec3{
            M.a1 * V.X + M.a2 * V.Y + M.a3 * V.Z,
            M.b1 * V.X + M.b2 * V.Y + M.b3 * V.Z,
            M.c1 * V.X + M.c2 * V.Y + M.c3 * V.Z,
        };
    }

    [[nodiscard]] inline int ProceduralMeshLimbIndex(const Object3D& Object, const Mesh& MeshValue)
    {
        for (std::size_t Index = 0; Index < Object.Meshes.size(); ++Index)
        {
            if (&Object.Meshes[Index] == &MeshValue)
                return static_cast<int>(Index);
        }
        return -1;
    }

    // A limb's own transform, with its rotation sines and cosines computed once.
    struct LimbSteps
    {
        Vec3 Scale{};
        Vec3 NormalDivisor{};
        SinCos X{};
        SinCos Y{};
        SinCos Z{};
        Vec3 Offset{};
    };

    [[nodiscard]] inline LimbSteps MakeLimbSteps(const Mesh& Limb)
    {
        return LimbSteps{
            {Limb.LimbScaleX, Limb.LimbScaleY, Limb.LimbScaleZ},
            {std::max(0.0001f, std::abs(Limb.LimbScaleX)), std::max(0.0001f, std::abs(Limb.LimbScaleY)),
             std::max(0.0001f, std::abs(Limb.LimbScaleZ))},
            SinCosDegrees(Limb.LimbRotX),
            SinCosDegrees(Limb.LimbRotY),
            SinCosDegrees(Limb.LimbRotZ),
            {Limb.LimbOffX, Limb.LimbOffY, Limb.LimbOffZ},
        };
    }

    // Scaled and rotated, without the offset: how a glued object's directions follow its limb.
    [[nodiscard]] inline Vec3 ApplyLimbRotation(const LimbSteps& Limb, Vec3 Point)
    {
        Point.X *= Limb.Scale.X;
        Point.Y *= Limb.Scale.Y;
        Point.Z *= Limb.Scale.Z;
        Point = RotateX(Point, Limb.X);
        Point = RotateY(Point, Limb.Y);
        return RotateZ(Point, Limb.Z);
    }

    [[nodiscard]] inline Vec3 ApplyLimbPoint(const LimbSteps& Limb, Vec3 Point)
    {
        Point = ApplyLimbRotation(Limb, Point);
        Point.X += Limb.Offset.X;
        Point.Y += Limb.Offset.Y;
        Point.Z += Limb.Offset.Z;
        return Point;
    }

    [[nodiscard]] inline Vec3 ApplyLimbNormal(const LimbSteps& Limb, Vec3 Normal)
    {
        Normal.X /= Limb.NormalDivisor.X;
        Normal.Y /= Limb.NormalDivisor.Y;
        Normal.Z /= Limb.NormalDivisor.Z;
        Normal = RotateX(Normal, Limb.X);
        Normal = RotateY(Normal, Limb.Y);
        return RotateZ(Normal, Limb.Z);
    }

    // Calls Visit with each limb a procedural mesh's vertices pass through, its own first, then its parents.
    template <typename Fn> void ForEachProceduralLimb(const Object3D& Object, const Mesh& MeshValue, Fn&& Visit)
    {
        int LimbIndex = ProceduralMeshLimbIndex(Object, MeshValue);
        if (LimbIndex < 0)
        {
            Visit(MeshValue);
            return;
        }
        for (std::size_t Depth = 0; LimbIndex >= 0 && Depth < Object.Meshes.size(); ++Depth)
        {
            const std::size_t Index = static_cast<std::size_t>(LimbIndex);
            if (Index >= Object.Meshes.size())
                break;
            Visit(Object.Meshes[Index]);
            LimbIndex = Index < Object.LimbTransforms.size() ? Object.LimbTransforms[Index].ParentLimb : -1;
        }
    }

    // An object's own transform, with its rotation sines and cosines computed once.
    struct ObjectSteps
    {
        Vec3 Scale{};
        Vec3 VectorDivisor{};
        bool ApplyPivot = false;
        Mat3 Pivot{};
        bool ZyxRotation = false;
        SinCos X{};
        SinCos Y{};
        SinCos Z{};
        Vec3 Position{};
    };

    [[nodiscard]] inline ObjectSteps MakeObjectSteps(const Object3D& Object)
    {
        return ObjectSteps{
            {Object.ScaleX, Object.ScaleY, Object.ScaleZ},
            {std::max(0.0001f, std::abs(Object.ScaleX)), std::max(0.0001f, std::abs(Object.ScaleY)),
             std::max(0.0001f, std::abs(Object.ScaleZ))},
            Object.ApplyPivot,
            Object.PivotMatrix,
            Object.UseZyxRotation,
            SinCosDegrees(Object.RotX),
            SinCosDegrees(Object.RotY),
            SinCosDegrees(Object.RotZ),
            {Object.PosX, Object.PosY, Object.PosZ},
        };
    }

    [[nodiscard]] inline Vec3 ApplyObjectRotation(const ObjectSteps& Steps, Vec3 P)
    {
        if (Steps.ApplyPivot)
            P = Mat3Apply(P, Steps.Pivot);
        if (Steps.ZyxRotation)
        {
            P = RotateZ(P, Steps.Z);
            P = RotateY(P, Steps.Y);
            return RotateX(P, Steps.X);
        }
        P = RotateX(P, Steps.X);
        P = RotateY(P, Steps.Y);
        return RotateZ(P, Steps.Z);
    }

    [[nodiscard]] inline Vec3 ApplyObjectPoint(const ObjectSteps& Steps, const Vec3& In, bool IncludePosition)
    {
        Vec3 P = ApplyObjectRotation(Steps, Vec3{In.X * Steps.Scale.X, In.Y * Steps.Scale.Y, In.Z * Steps.Scale.Z});
        if (IncludePosition)
        {
            P.X += Steps.Position.X;
            P.Y += Steps.Position.Y;
            P.Z += Steps.Position.Z;
        }
        return P;
    }

    [[nodiscard]] inline Vec3 ApplyObjectVector(const ObjectSteps& Steps, Vec3 P)
    {
        P.X /= Steps.VectorDivisor.X;
        P.Y /= Steps.VectorDivisor.Y;
        P.Z /= Steps.VectorDivisor.Z;
        return ApplyObjectRotation(Steps, P);
    }

    // The matrices of the steps above, for the vertex shader. Column-major: rotation matrix columns are the images
    // of the axes under RotateX, RotateY and RotateZ.
    [[nodiscard]] inline glm::mat4 AiMatrix(const aiMatrix4x4& M)
    {
        return glm::transpose(glm::make_mat4(&M.a1));
    }

    [[nodiscard]] inline glm::mat3 RotationMatrixX(SinCos R)
    {
        return glm::mat3(1.0f, 0.0f, 0.0f, 0.0f, R.C, R.S, 0.0f, -R.S, R.C);
    }

    [[nodiscard]] inline glm::mat3 RotationMatrixY(SinCos R)
    {
        return glm::mat3(R.C, 0.0f, -R.S, 0.0f, 1.0f, 0.0f, R.S, 0.0f, R.C);
    }

    [[nodiscard]] inline glm::mat3 RotationMatrixZ(SinCos R)
    {
        return glm::mat3(R.C, R.S, 0.0f, -R.S, R.C, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    [[nodiscard]] inline glm::mat3 ScaleMatrix(const Vec3& Scale)
    {
        return glm::mat3(Scale.X, 0.0f, 0.0f, 0.0f, Scale.Y, 0.0f, 0.0f, 0.0f, Scale.Z);
    }

    [[nodiscard]] inline glm::mat3 InverseScaleMatrix(const Vec3& Divisor)
    {
        return ScaleMatrix(Vec3{1.0f / Divisor.X, 1.0f / Divisor.Y, 1.0f / Divisor.Z});
    }

    [[nodiscard]] inline glm::mat4 AffineMatrix(const glm::mat3& Linear, const Vec3& Offset)
    {
        glm::mat4 Result(Linear);
        Result[3] = glm::vec4(Offset.X, Offset.Y, Offset.Z, 1.0f);
        return Result;
    }

    [[nodiscard]] inline glm::mat3 LimbRotationMatrix(const LimbSteps& Limb)
    {
        return RotationMatrixZ(Limb.Z) * RotationMatrixY(Limb.Y) * RotationMatrixX(Limb.X);
    }

    [[nodiscard]] inline glm::mat3 ObjectRotationMatrix(const ObjectSteps& Steps)
    {
        const glm::mat3 Rotation = Steps.ZyxRotation
                                       ? RotationMatrixX(Steps.X) * RotationMatrixY(Steps.Y) * RotationMatrixZ(Steps.Z)
                                       : RotationMatrixZ(Steps.Z) * RotationMatrixY(Steps.Y) * RotationMatrixX(Steps.X);
        if (!Steps.ApplyPivot)
            return Rotation;
        // Mat3Apply multiplies a row vector by the pivot, so its rows are the columns here.
        glm::mat3 Pivot;
        for (int Column = 0; Column < 3; ++Column)
            for (int Row = 0; Row < 3; ++Row)
                Pivot[Column][Row] = Steps.Pivot.M[Column][Row];
        return Rotation * Pivot;
    }

    // Takes one mesh's points and normals to world space as TransformObjectPoint and TransformObjectNormal do,
    // with the limb chain and every sine and cosine computed once. A glued object follows its glue chain per call.
    class MeshWorldTransform
    {
      public:
        MeshWorldTransform(EngineKernel& K, const Object3D& Object, const Mesh& MeshValue,
                           const aiMatrix4x4* ImportedTransform)
            : K_(K), Object_(Object), Bind_(MeshValue.Geometry().BindTransform), Imported_(ImportedTransform),
              Steps_(MakeObjectSteps(Object)), Glued_(Object.GluedTargetObject > 0),
              NormalizeNormals_(K.Renderer().NormalizeNormals())
        {
            if (!ImportedTransform)
                ForEachProceduralLimb(Object, MeshValue,
                                      [&](const Mesh& Limb) { Limbs_.push_back(MakeLimbSteps(Limb)); });
        }

        [[nodiscard]] Vec3 Point(const Vec3& Local, bool ObjectSpace) const
        {
            Vec3 P = Local;
            if (!ObjectSpace && Imported_)
                P = ApplyAiTransformPoint(*Imported_, Local);
            else if (!ObjectSpace)
            {
                P = ApplyAiTransformPoint(Bind_, Local);
                for (const LimbSteps& Limb : Limbs_)
                    P = ApplyLimbPoint(Limb, P);
            }
            return Glued_ ? dbc::TransformObjectSpacePoint(K_, Object_, P)
                          : ApplyObjectPoint(Steps_, P, Object_.GlueMode != 2);
        }

        [[nodiscard]] Vec3 Normal(const Vec3& Local, bool ObjectSpace) const
        {
            Vec3 N = Local;
            if (!ObjectSpace && Imported_)
                N = ApplyAiTransformVector(*Imported_, Local);
            else if (!ObjectSpace)
            {
                N = ApplyAiTransformVector(Bind_, Local);
                for (const LimbSteps& Limb : Limbs_)
                    N = ApplyLimbNormal(Limb, N);
            }
            if (Glued_)
                return dbc::TransformObjectSpaceVector(K_, Object_, N);
            const Vec3 World = ApplyObjectVector(Steps_, N);
            return NormalizeNormals_ ? Vec3Normalize(World) : World;
        }

        // Point and Normal as matrices, for a mesh the vertex shader transforms; never for a glued object. Normal
        // leaves out the normalization.
        [[nodiscard]] glm::mat4 PointMatrix() const
        {
            assert(!Glued_);
            glm::mat4 Mesh = AiMatrix(Imported_ ? *Imported_ : Bind_);
            if (!Imported_)
                for (const LimbSteps& Limb : Limbs_)
                    Mesh = AffineMatrix(LimbRotationMatrix(Limb) * ScaleMatrix(Limb.Scale), Limb.Offset) * Mesh;
            const Vec3 Position = Object_.GlueMode != 2 ? Steps_.Position : Vec3{};
            return AffineMatrix(ObjectRotationMatrix(Steps_) * ScaleMatrix(Steps_.Scale), Position) * Mesh;
        }

        [[nodiscard]] glm::mat3 NormalMatrix() const
        {
            assert(!Glued_);
            glm::mat3 Mesh(AiMatrix(Imported_ ? *Imported_ : Bind_));
            if (!Imported_)
                for (const LimbSteps& Limb : Limbs_)
                    Mesh = LimbRotationMatrix(Limb) * InverseScaleMatrix(Limb.NormalDivisor) * Mesh;
            return ObjectRotationMatrix(Steps_) * InverseScaleMatrix(Steps_.VectorDivisor) * Mesh;
        }

      private:
        EngineKernel& K_;
        const Object3D& Object_;
        const aiMatrix4x4& Bind_;
        const aiMatrix4x4* Imported_;
        ObjectSteps Steps_;
        bool Glued_;
        bool NormalizeNormals_;
        std::vector<LimbSteps> Limbs_;
    };
} // namespace dbc
