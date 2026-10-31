#include "physics_system.hpp"

#include "../engine_math.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "object_manager.hpp"
#include "renderer_system.hpp"

// Jolt requires its configuration header before all other Jolt headers.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dbc
{
    namespace
    {
        constexpr JPH::ObjectLayer NonMovingLayer = 0;
        constexpr JPH::ObjectLayer MovingLayer = 1;
        constexpr JPH::ObjectLayer DisabledLayer = 2;

        constexpr float DefaultStepSeconds = 1.0f / 60.0f;
        constexpr float MinimumStepSeconds = 1.0f / 1000.0f;
        constexpr float MaximumStepSeconds = 1.0f / 10.0f;
        constexpr float MaximumManualUpdateSeconds = 1.0f;
        constexpr float MaximumFrameUpdateSeconds = 0.1f;
        // Absorbs float rounding, so N updates of one step run exactly N steps.
        constexpr float StepToleranceSeconds = 1.0e-5f;

        constexpr float MinimumShapeExtent = 0.01f;
        constexpr float MinimumHullVolume = 1.0e-8f;
        constexpr float DefaultMass = 1.0f;
        constexpr float DefaultFriction = 0.9f;
        constexpr float DefaultLinearDamping = 0.001f;
        constexpr float DefaultAngularDamping = 0.005f;

        // Jolt's friction works from the impulses of an earlier iteration, so one iteration is not enough.
        constexpr int MinimumVelocitySteps = 2;
        constexpr int MaximumVelocitySteps = 255;
        // ODE SET STEP MODE 1 asked for ODE's slower exact solver; Jolt's nearest equivalent is many more iterations.
        constexpr int ExactVelocitySteps = 50;
        constexpr int ExactPositionSteps = 10;

        class JoltRuntime
        {
          public:
            JoltRuntime()
            {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }

            ~JoltRuntime()
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        };

        JoltRuntime& Runtime()
        {
            static JoltRuntime Value;
            return Value;
        }

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
          public:
            BroadPhaseLayers()
            {
                Layers_[NonMovingLayer] = JPH::BroadPhaseLayer(0);
                Layers_[MovingLayer] = JPH::BroadPhaseLayer(1);
                Layers_[DisabledLayer] = JPH::BroadPhaseLayer(1);
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return 2;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override
            {
                return Layers_[Layer];
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer Layer) const override
            {
                return Layer == Layers_[NonMovingLayer] ? "non-moving" : "moving";
            }
#endif

          private:
            JPH::BroadPhaseLayer Layers_[3];
        };

        class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter
        {
          public:
            bool ShouldCollide(JPH::ObjectLayer First, JPH::ObjectLayer Second) const override
            {
                if (First == DisabledLayer || Second == DisabledLayer)
                    return false;
                return First == MovingLayer || Second == MovingLayer;
            }
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
          public:
            bool ShouldCollide(JPH::ObjectLayer Object, JPH::BroadPhaseLayer BroadPhase) const override
            {
                if (Object == DisabledLayer)
                    return false;
                return Object == MovingLayer || BroadPhase == JPH::BroadPhaseLayer(1);
            }
        };

        constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

        // The default object order is Rz * Ry * Rx, Jolt's Euler convention. The inverse of the ZYX order
        // Rx * Ry * Rz has that same form with negated angles.
        JPH::Quat ToPhysicsRotation(float XDegrees, float YDegrees, float ZDegrees, bool ZyxOrder)
        {
            const JPH::Vec3 Angles(XDegrees * DegreesToRadians, YDegrees * DegreesToRadians,
                                   ZDegrees * DegreesToRadians);
            return ZyxOrder ? JPH::Quat::sEulerAngles(-Angles).Conjugated() : JPH::Quat::sEulerAngles(Angles);
        }

        JPH::Quat ToPhysicsRotation(const Object3D& Object)
        {
            return ToPhysicsRotation(Object.RotX, Object.RotY, Object.RotZ, Object.UseZyxRotation);
        }

        JPH::Vec3 ToObjectDegrees(JPH::QuatArg Rotation, bool ZyxOrder)
        {
            const JPH::Vec3 Angles = ZyxOrder ? -Rotation.Conjugated().GetEulerAngles() : Rotation.GetEulerAngles();
            return Angles / DegreesToRadians;
        }

        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

        bool ValidateFinite(std::initializer_list<float> Values, const char* Command, std::string* Error)
        {
            for (const float Value : Values)
            {
                if (!std::isfinite(Value))
                    return Fail(Error, std::string(Command) + " requires finite values");
            }
            return true;
        }

        // The extents of an object's vertices around its own origin, after its scale and pivot.
        struct LocalBounds
        {
            JPH::Vec3 Min = JPH::Vec3::sZero();
            JPH::Vec3 Max = JPH::Vec3::sZero();
            float Radius = 0.0f;

            [[nodiscard]] JPH::Vec3 Center() const
            {
                return 0.5f * (Min + Max);
            }

            [[nodiscard]] JPH::Vec3 Size() const
            {
                return Max - Min;
            }

            [[nodiscard]] JPH::Vec3 HalfExtent() const
            {
                return JPH::Vec3::sMax(0.5f * Size(), JPH::Vec3::sReplicate(MinimumShapeExtent));
            }
        };

        // Jolt requires every half extent of a box to reach its convex radius.
        JPH::RefConst<JPH::Shape> MakeBoxShape(JPH::Vec3Arg HalfExtent)
        {
            return new JPH::BoxShape(HalfExtent, std::min(JPH::cDefaultConvexRadius, HalfExtent.ReduceMin()));
        }

        JPH::RefConst<JPH::Shape> MakeCylinderShape(float HalfHeight, float Radius)
        {
            return new JPH::CylinderShape(HalfHeight, Radius,
                                          std::min({JPH::cDefaultConvexRadius, HalfHeight, Radius}));
        }

        // The proportions the plugin measured from an object's size.
        struct CapsuleSize
        {
            float HalfHeight = 0.0f;
            float Radius = 0.0f;
        };

        CapsuleSize CapsuleSizeFor(JPH::Vec3Arg Size, float ScaleModifier)
        {
            return {std::max(0.5f * Size.GetY() * 0.55f * ScaleModifier, MinimumShapeExtent),
                    std::max(0.25f * (Size.GetX() + Size.GetZ()) * 0.967f, MinimumShapeExtent)};
        }

        // A body's position is its object's origin, so a shape off that origin moves inside the body.
        JPH::RefConst<JPH::Shape> OffsetShape(JPH::RefConst<JPH::Shape> Shape, JPH::Vec3Arg Offset)
        {
            if (Offset.IsNearZero(1.0e-8f))
                return Shape;
            return new JPH::RotatedTranslatedShape(Offset, JPH::Quat::sIdentity(), Shape.GetPtr());
        }
    } // namespace

    struct PhysicsSystem::Impl
    {
        struct BodyRecord
        {
            int ObjectId = 0;
            JPH::BodyID Id;
            bool Moving = false;
            bool Enabled = true;
            bool ZyxRotation = false;
            // A capsule's body never turns, so what the object shows stays the program's own choice.
            bool SyncRotation = true;
            bool KinematicRequested = false;
            bool PseudoStatic = false;
            bool HasPendingForce = false;
            JPH::Vec3 PendingForce = JPH::Vec3::sZero();
            JPH::Vec3 PendingTorque = JPH::Vec3::sZero();

            [[nodiscard]] bool Kinematic() const
            {
                return KinematicRequested || PseudoStatic;
            }
        };

        struct BodyOptions
        {
            bool Dynamic = false;
            bool Sensor = false;
            bool LockRotation = false;
            float Mass = DefaultMass;
            float Friction = DefaultFriction;
            float Restitution = 0.0f;
            JPH::RVec3 Position = JPH::RVec3::sZero();
            JPH::Quat Rotation = JPH::Quat::sIdentity();
        };

        struct ObjectGeometry
        {
            std::vector<JPH::Vec3> Points;
            std::vector<std::uint32_t> Indices;
        };

        explicit Impl(EngineKernel& Kernel) : Kernel(Kernel) {}

        [[nodiscard]] JPH::BodyInterface& Bodies() const
        {
            // Every call happens on the program's thread and outside World->Update, so nothing competes for a body.
            return World->GetBodyInterfaceNoLock();
        }

        void Start()
        {
            static_cast<void>(Runtime());
            TempAllocator = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(16 * 1024 * 1024);
            const int Workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 3);
            Jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, Workers);
            World = std::make_unique<JPH::PhysicsSystem>();
            World->Init(65536, 0, 65536, 10240, BroadPhase, ObjectVsBroadPhaseFilter, ObjectLayerPairFilter);
            World->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
            const JPH::PhysicsSettings& Defaults = World->GetPhysicsSettings();
            PositionCorrection = Defaults.mBaumgarte;
            VelocitySteps = static_cast<int>(Defaults.mNumVelocitySteps);
            PositionSteps = static_cast<int>(Defaults.mNumPositionSteps);
            ExactStepMode = false;
            AnyPendingForce = false;
            StepSeconds = DefaultStepSeconds;
            AccumulatedSeconds = 0.0f;
            LastUpdateNanoseconds = SDL_GetTicksNS();
            LastPresentTicks = Kernel.Core().LastRenderTicks();
            Started = true;
        }

        void End()
        {
            if (!World)
                return;
            for (auto& [ObjectId, Record] : Records)
            {
                static_cast<void>(ObjectId);
                RemoveBody(Record);
            }
            Records.clear();
            Started = false;
            World.reset();
            Jobs.reset();
            TempAllocator.reset();
        }

        bool RequireStarted(const char* Command, std::string* Error) const
        {
            if (!Started)
                return Fail(Error, std::string(Command) + " requires ODE START");
            return true;
        }

        BodyRecord* RequireBody(int ObjectId, const char* Command, std::string* Error)
        {
            if (!RequireStarted(Command, Error))
                return nullptr;
            const auto It = Records.find(ObjectId);
            if (It == Records.end())
            {
                Fail(Error, std::string(Command) + " requires an ODE body for object " + std::to_string(ObjectId));
                return nullptr;
            }
            return &It->second;
        }

        BodyRecord* RequireMovingBody(int ObjectId, const char* Command, std::string* Error)
        {
            BodyRecord* Record = RequireBody(ObjectId, Command, Error);
            if (Record && !Record->Moving)
            {
                Fail(Error, std::string(Command) + " requires a moving body, but object " + std::to_string(ObjectId) +
                                " is static");
                return nullptr;
            }
            return Record;
        }

        // Sleeping neighbours would otherwise keep resting on a body that has gone.
        void WakeTouchingBodies(const BodyRecord& Record)
        {
            JPH::AABox Bounds;
            {
                const JPH::BodyLockRead Lock(World->GetBodyLockInterfaceNoLock(), Record.Id);
                if (!Lock.Succeeded())
                    return;
                Bounds = Lock.GetBody().GetWorldSpaceBounds();
            }
            Bounds.ExpandBy(JPH::Vec3::sReplicate(0.1f));
            Bodies().ActivateBodiesInAABox(Bounds, World->GetDefaultBroadPhaseLayerFilter(MovingLayer),
                                           World->GetDefaultLayerFilter(MovingLayer));
        }

        void RemoveBody(const BodyRecord& Record)
        {
            JPH::BodyInterface& Interface = Bodies();
            Interface.RemoveBody(Record.Id);
            Interface.DestroyBody(Record.Id);
        }

        void DestroyRecord(std::unordered_map<int, BodyRecord>::iterator It)
        {
            WakeTouchingBodies(It->second);
            RemoveBody(It->second);
            Records.erase(It);
        }

        // Deletion paths other than the DELETE OBJECT forms still end here, before the next step can move a ghost.
        void RemoveOrphanedBodies()
        {
            for (auto It = Records.begin(); It != Records.end();)
            {
                const auto Current = It++;
                if (!Kernel.Objects().GetObject(Current->first))
                    DestroyRecord(Current);
            }
        }

        // Every vertex of an object, or of one limb, where it sits relative to the object's origin. Limb -1 is the
        // whole object.
        bool GatherGeometry(const Object3D& Object, int ObjectId, int Limb, const char* Command, ObjectGeometry& Output,
                            std::string* Error)
        {
            std::vector<Vec3> Vertices;
            const auto AddMesh = [&](const Mesh& MeshValue)
            {
                const auto Base = static_cast<std::uint32_t>(Output.Points.size());
                Kernel.Renderer().ObjectMeshVertices(Object, MeshValue, Vertices);
                for (const Vec3& Local : Vertices)
                {
                    Vec3 Point{Local.X * Object.ScaleX, Local.Y * Object.ScaleY, Local.Z * Object.ScaleZ};
                    if (Object.ApplyPivot)
                        Point = Mat3Apply(Point, Object.PivotMatrix);
                    Output.Points.emplace_back(Point.X, Point.Y, Point.Z);
                }
                const std::vector<std::uint32_t>& Indices = MeshValue.Geometry().Indices;
                for (const std::uint32_t Index : Indices)
                    Output.Indices.push_back(Base + Index);
                if (Indices.empty())
                {
                    for (std::uint32_t Index = 0; Index < Vertices.size(); ++Index)
                        Output.Indices.push_back(Base + Index);
                }
            };

            if (Limb != -1)
            {
                const Mesh* LimbMesh = Kernel.Renderer().FindLimbMesh(Object, Limb);
                if (!LimbMesh)
                    return Fail(Error, std::string(Command) + " requires limb " + std::to_string(Limb) + " of object " +
                                           std::to_string(ObjectId));
                AddMesh(*LimbMesh);
            }
            else
            {
                for (const Mesh& MeshValue : Object.Meshes)
                    AddMesh(MeshValue);
            }
            if (Output.Points.empty())
                return Fail(Error, std::string(Command) + " requires object " + std::to_string(ObjectId) +
                                       " to have mesh geometry");
            return true;
        }

        bool ObjectBounds(const Object3D& Object, int ObjectId, int Limb, const char* Command, LocalBounds& Output,
                          std::string* Error)
        {
            ObjectGeometry Geometry;
            if (!GatherGeometry(Object, ObjectId, Limb, Command, Geometry, Error))
                return false;
            JPH::Vec3 Min = JPH::Vec3::sReplicate(FLT_MAX);
            JPH::Vec3 Max = JPH::Vec3::sReplicate(-FLT_MAX);
            for (JPH::Vec3Arg Point : Geometry.Points)
            {
                Min = JPH::Vec3::sMin(Min, Point);
                Max = JPH::Vec3::sMax(Max, Point);
            }
            Output.Min = Min;
            Output.Max = Max;
            const JPH::Vec3 Center = Output.Center();
            for (JPH::Vec3Arg Point : Geometry.Points)
                Output.Radius = std::max(Output.Radius, (Point - Center).Length());
            return true;
        }

        // A collision box a program set on the object replaces the extents of its vertices.
        bool CollisionBounds(const Object3D& Object, int ObjectId, int Limb, const char* Command, LocalBounds& Output,
                             std::string* Error)
        {
            if (Limb != -1 || !Object.CollisionAabbExplicit)
                return ObjectBounds(Object, ObjectId, Limb, Command, Output, Error);
            const JPH::Vec3 Scale(Object.ScaleX, Object.ScaleY, Object.ScaleZ);
            const AABB& Box = Object.CollisionAabb;
            const JPH::Vec3 First = Scale * JPH::Vec3(Box.MinX, Box.MinY, Box.MinZ);
            const JPH::Vec3 Second = Scale * JPH::Vec3(Box.MaxX, Box.MaxY, Box.MaxZ);
            Output.Min = JPH::Vec3::sMin(First, Second);
            Output.Max = JPH::Vec3::sMax(First, Second);
            return true;
        }

        const Object3D* RequireObject(int ObjectId, const char* Command, std::string* Error)
        {
            if (!RequireStarted(Command, Error))
                return nullptr;
            const Object3D* Object = Kernel.Objects().GetObject(ObjectId);
            if (!Object)
                Fail(Error, std::string(Command) + " requires object " + std::to_string(ObjectId));
            return Object;
        }

        [[nodiscard]] static BodyOptions OptionsAt(const Object3D& Object, bool Dynamic)
        {
            BodyOptions Options;
            Options.Dynamic = Dynamic;
            Options.Position = JPH::RVec3(Object.PosX, Object.PosY, Object.PosZ);
            Options.Rotation = ToPhysicsRotation(Object);
            return Options;
        }

        BodyRecord* AddBody(int ObjectId, const Object3D& Object, const char* Command, JPH::RefConst<JPH::Shape> Shape,
                            const BodyOptions& Options, std::string* Error)
        {
            if (const auto Existing = Records.find(ObjectId); Existing != Records.end())
                DestroyRecord(Existing);

            JPH::BodyCreationSettings Settings(Shape.GetPtr(), Options.Position, Options.Rotation,
                                               Options.Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                               Options.Dynamic ? MovingLayer : NonMovingLayer);
            Settings.mFriction = Options.Friction;
            Settings.mRestitution = Options.Restitution;
            Settings.mLinearDamping = DefaultLinearDamping;
            Settings.mAngularDamping = DefaultAngularDamping;
            Settings.mIsSensor = Options.Sensor;
            if (Options.Dynamic)
            {
                // Mass does not follow the volume, so a force means the same on bodies of any size.
                Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                Settings.mMassPropertiesOverride.mMass = Options.Mass;
                Settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
                if (Options.LockRotation)
                    Settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                                            JPH::EAllowedDOFs::TranslationZ;
            }
            const JPH::BodyID BodyId = Bodies().CreateAndAddBody(
                Settings, Options.Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (BodyId.IsInvalid())
            {
                Fail(Error, std::string(Command) + " exhausted the physics body pool");
                return nullptr;
            }
            BodyRecord& Record = Records[ObjectId];
            Record.ObjectId = ObjectId;
            Record.Id = BodyId;
            Record.Moving = Options.Dynamic;
            Record.ZyxRotation = Object.UseZyxRotation;
            return &Record;
        }

        bool CreateSphere(int ObjectId, bool Dynamic, const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !ObjectBounds(*Object, ObjectId, -1, Command, Bounds, Error))
                return false;
            return AddBody(
                       ObjectId, *Object, Command,
                       OffsetShape(new JPH::SphereShape(std::max(Bounds.Radius, MinimumShapeExtent)), Bounds.Center()),
                       OptionsAt(*Object, Dynamic), Error) != nullptr;
        }

        bool CreateBox(int ObjectId, bool Dynamic, int Limb, const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !CollisionBounds(*Object, ObjectId, Limb, Command, Bounds, Error))
                return false;
            return AddBody(ObjectId, *Object, Command, OffsetShape(MakeBoxShape(Bounds.HalfExtent()), Bounds.Center()),
                           OptionsAt(*Object, Dynamic), Error) != nullptr;
        }

        bool CreateTunedBox(int ObjectId, int Limb, int TipOverMode, float Weight, float Friction, float Restitution,
                            const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !CollisionBounds(*Object, ObjectId, Limb, Command, Bounds, Error))
                return false;

            BodyOptions Options = OptionsAt(*Object, true);
            Options.Sensor = TipOverMode == 11;
            // -1 is the plugin's marker for a default.
            if (Weight != -1.0f)
                Options.Mass = DefaultMass * Weight / 100.0f;
            if (Friction != -1.0f)
                Options.Friction = Friction / 50.0f;
            if (Restitution != -1.0f)
                Options.Restitution = std::min(Restitution / 100.0f, 1.0f);

            const JPH::Vec3 HalfExtent = Bounds.HalfExtent();
            JPH::RefConst<JPH::Shape> Shape = MakeBoxShape(HalfExtent);
            if (TipOverMode >= 1 && TipOverMode <= 3)
            {
                // A wide thin plate through the box keeps it from settling flat on the sides the plate crosses.
                JPH::Vec3 PlateHalfExtent = 1.5f * HalfExtent;
                PlateHalfExtent.SetComponent(TipOverMode - 1, MinimumShapeExtent);
                JPH::Ref<JPH::StaticCompoundShapeSettings> Compound = new JPH::StaticCompoundShapeSettings();
                Compound->AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), Shape.GetPtr());
                Compound->AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), MakeBoxShape(PlateHalfExtent).GetPtr());
                const JPH::ShapeSettings::ShapeResult Result = Compound->Create();
                if (Result.HasError())
                    return Fail(Error,
                                std::string(Command) + " could not build its shape: " + Result.GetError().c_str());
                Shape = Result.Get();
            }
            return AddBody(ObjectId, *Object, Command, OffsetShape(Shape, Bounds.Center()), Options, Error) != nullptr;
        }

        bool CreateCapsule(int ObjectId, bool Dynamic, float ScaleModifier, float Raised, const char* Command,
                           std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !CollisionBounds(*Object, ObjectId, -1, Command, Bounds, Error))
                return false;
            const CapsuleSize Size = CapsuleSizeFor(Bounds.Size(), ScaleModifier);
            // The plugin hung a feet-origin model under the capsule's centre. Standing the capsule on the object's
            // lowest point places such a model the same way, and keeps a centred primitive out of the ground too.
            const JPH::Vec3 Center = Bounds.Center();
            const JPH::Vec3 Offset(Center.GetX(), Bounds.Min.GetY() + Raised + Size.Radius + Size.HalfHeight,
                                   Center.GetZ());
            BodyOptions Options = OptionsAt(*Object, Dynamic);
            Options.LockRotation = true;
            BodyRecord* Record =
                AddBody(ObjectId, *Object, Command,
                        OffsetShape(new JPH::CapsuleShape(Size.HalfHeight, Size.Radius), Offset), Options, Error);
            if (!Record)
                return false;
            Record->SyncRotation = false;
            return true;
        }

        bool CreateStaticMesh(int ObjectId, int Limb, float ScalingPercent, const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            ObjectGeometry Geometry;
            if (!Object || !GatherGeometry(*Object, ObjectId, Limb, Command, Geometry, Error))
                return false;
            if (ScalingPercent <= 0.0f)
                return Fail(Error, std::string(Command) + " requires a positive scaling percentage");

            const float Scale = ScalingPercent / 100.0f;
            JPH::VertexList Vertices;
            Vertices.reserve(Geometry.Points.size());
            for (JPH::Vec3Arg Point : Geometry.Points)
                Vertices.push_back(JPH::Float3(Scale * Point.GetX(), Scale * Point.GetY(), Scale * Point.GetZ()));
            JPH::IndexedTriangleList Triangles;
            Triangles.reserve(Geometry.Indices.size() / 3);
            for (std::size_t Index = 0; Index + 2 < Geometry.Indices.size(); Index += 3)
                Triangles.push_back(JPH::IndexedTriangle(Geometry.Indices[Index], Geometry.Indices[Index + 1],
                                                         Geometry.Indices[Index + 2], 0));

            JPH::Ref<JPH::MeshShapeSettings> Settings =
                new JPH::MeshShapeSettings(std::move(Vertices), std::move(Triangles));
            const JPH::ShapeSettings::ShapeResult Result = Settings->Create();
            if (Result.HasError())
                return Fail(Error, std::string(Command) + " could not build a mesh from object " +
                                       std::to_string(ObjectId) + ": " + Result.GetError().c_str());
            return AddBody(ObjectId, *Object, Command, Result.Get(), OptionsAt(*Object, false), Error) != nullptr;
        }

        // Jolt does not simulate moving triangle meshes; a hull keeps the object's outline without its hollows.
        bool CreateDynamicHull(int ObjectId, const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            ObjectGeometry Geometry;
            if (!Object || !GatherGeometry(*Object, ObjectId, -1, Command, Geometry, Error))
                return false;
            JPH::Ref<JPH::ConvexHullShapeSettings> Settings =
                new JPH::ConvexHullShapeSettings(Geometry.Points.data(), static_cast<int>(Geometry.Points.size()));
            const JPH::ShapeSettings::ShapeResult Result = Settings->Create();
            if (Result.HasError())
                return Fail(Error, std::string(Command) + " could not build a convex hull from object " +
                                       std::to_string(ObjectId) + ": " + Result.GetError().c_str());
            // Jolt accepts a flat hull, but a body without volume has no inertia to simulate.
            if (Result.Get()->GetVolume() < MinimumHullVolume)
                return Fail(Error, std::string(Command) + " requires object " + std::to_string(ObjectId) +
                                       " to have volume; ODE CREATE DYNAMIC BOX takes a flat object");
            return AddBody(ObjectId, *Object, Command, Result.Get(), OptionsAt(*Object, true), Error) != nullptr;
        }

        void AddPendingForce(BodyRecord& Record, JPH::Vec3Arg Force, JPH::Vec3Arg Torque)
        {
            Record.PendingForce += Force;
            Record.PendingTorque += Torque;
            Record.HasPendingForce = true;
            AnyPendingForce = true;
        }

        // A force lasts for one update, so the impulse covers that update's whole duration at any update rate.
        void ApplyPendingForces(float Seconds)
        {
            if (!AnyPendingForce)
                return;
            AnyPendingForce = false;
            JPH::BodyInterface& Interface = Bodies();
            for (auto& [ObjectId, Record] : Records)
            {
                static_cast<void>(ObjectId);
                if (!Record.HasPendingForce)
                    continue;
                Interface.AddImpulse(Record.Id, Seconds * Record.PendingForce);
                Interface.AddAngularImpulse(Record.Id, Seconds * Record.PendingTorque);
                Record.HasPendingForce = false;
                Record.PendingForce = JPH::Vec3::sZero();
                Record.PendingTorque = JPH::Vec3::sZero();
            }
        }

        void ApplyMotionType(const BodyRecord& Record)
        {
            JPH::BodyInterface& Interface = Bodies();
            Interface.SetMotionType(Record.Id,
                                    Record.Kinematic() ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                                    JPH::EActivation::Activate);
            if (Record.Kinematic())
                Interface.SetLinearAndAngularVelocity(Record.Id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        }

        void ApplyPhysicsSettings()
        {
            JPH::PhysicsSettings Settings = World->GetPhysicsSettings();
            Settings.mBaumgarte = PositionCorrection;
            Settings.mNumVelocitySteps =
                static_cast<JPH::uint>(ExactStepMode ? std::max(VelocitySteps, ExactVelocitySteps) : VelocitySteps);
            Settings.mNumPositionSteps = static_cast<JPH::uint>(ExactStepMode ? ExactPositionSteps : PositionSteps);
            World->SetPhysicsSettings(Settings);
        }

        // Without a presented frame there is no meaningful elapsed time: a loop stepping without SYNC gets a regular
        // step per call, which keeps it deterministic.
        [[nodiscard]] float FrameSeconds() const
        {
            if (Kernel.Core().LastRenderTicks() == LastPresentTicks)
                return DefaultStepSeconds;
            const double Elapsed = static_cast<double>(SDL_GetTicksNS() - LastUpdateNanoseconds) / 1'000'000'000.0;
            return std::min(static_cast<float>(Elapsed), MaximumFrameUpdateSeconds);
        }

        bool Advance(float Seconds, std::string* Error)
        {
            LastUpdateNanoseconds = SDL_GetTicksNS();
            LastPresentTicks = Kernel.Core().LastRenderTicks();
            RemoveOrphanedBodies();
            ApplyPendingForces(Seconds);

            AccumulatedSeconds += Seconds;
            while (AccumulatedSeconds >= StepSeconds - StepToleranceSeconds)
            {
                const JPH::EPhysicsUpdateError UpdateError =
                    World->Update(StepSeconds, 1, TempAllocator.get(), Jobs.get());
                if (UpdateError != JPH::EPhysicsUpdateError::None)
                    return Fail(Error, "ODE UPDATE exhausted a Jolt simulation buffer");
                AccumulatedSeconds = std::max(AccumulatedSeconds - StepSeconds, 0.0f);
            }
            SyncObjects(AccumulatedSeconds);
            return true;
        }

        // The time left in the accumulator extrapolates what the objects show, so motion stays smooth when frames
        // and steps do not line up.
        void SyncObjects(float RemainderSeconds)
        {
            const JPH::BodyInterface& Interface = Bodies();
            for (const auto& [ObjectId, Record] : Records)
            {
                if (!Record.Moving)
                    continue;
                JPH::RVec3 Position;
                JPH::Quat Rotation;
                JPH::Vec3 LinearVelocity;
                JPH::Vec3 AngularVelocity;
                Interface.GetPositionAndRotation(Record.Id, Position, Rotation);
                Interface.GetLinearAndAngularVelocity(Record.Id, LinearVelocity, AngularVelocity);
                Position += JPH::RVec3(RemainderSeconds * LinearVelocity);
                const float AngularSpeed = AngularVelocity.Length();
                if (AngularSpeed * RemainderSeconds > 1.0e-6f)
                    Rotation = (JPH::Quat::sRotation(AngularVelocity / AngularSpeed, AngularSpeed * RemainderSeconds) *
                                Rotation)
                                   .Normalized();
                Kernel.Objects().PositionObject(ObjectId, static_cast<float>(Position.GetX()),
                                                static_cast<float>(Position.GetY()),
                                                static_cast<float>(Position.GetZ()));
                if (!Record.SyncRotation)
                    continue;
                const JPH::Vec3 Angles = ToObjectDegrees(Rotation, Record.ZyxRotation);
                Kernel.Objects().RotateObject(ObjectId, Angles.GetX(), Angles.GetY(), Angles.GetZ());
            }
        }

        EngineKernel& Kernel;
        BroadPhaseLayers BroadPhase;
        ObjectVsBroadPhase ObjectVsBroadPhaseFilter;
        ObjectLayerPairs ObjectLayerPairFilter;
        std::unique_ptr<JPH::PhysicsSystem> World;
        std::unique_ptr<JPH::TempAllocatorImplWithMallocFallback> TempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> Jobs;
        std::unordered_map<int, BodyRecord> Records;
        float StepSeconds = DefaultStepSeconds;
        float AccumulatedSeconds = 0.0f;
        float PositionCorrection = 0.0f;
        int VelocitySteps = 0;
        int PositionSteps = 0;
        bool ExactStepMode = false;
        bool AnyPendingForce = false;
        Uint64 LastUpdateNanoseconds = 0;
        Uint64 LastPresentTicks = 0;
        bool Started = false;
    };

    PhysicsSystem::PhysicsSystem(EngineKernel& Kernel) : Impl_(std::make_unique<Impl>(Kernel)) {}

    PhysicsSystem::~PhysicsSystem()
    {
        End();
    }

    void PhysicsSystem::Start()
    {
        End();
        Impl_->Start();
    }

    void PhysicsSystem::End()
    {
        Impl_->End();
    }

    bool PhysicsSystem::IsStarted() const
    {
        return Impl_->Started;
    }

    bool PhysicsSystem::Update(std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE UPDATE", Error))
            return false;
        return Impl_->Advance(Impl_->FrameSeconds(), Error);
    }

    bool PhysicsSystem::Update(float Seconds, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE UPDATE", Error) || !ValidateFinite({Seconds}, "ODE UPDATE", Error))
            return false;
        if (Seconds <= 0.0f)
            return Fail(Error, "ODE UPDATE step must be greater than zero");
        return Impl_->Advance(std::min(Seconds, MaximumManualUpdateSeconds), Error);
    }

    void PhysicsSystem::ObjectDeleted(int ObjectId)
    {
        ObjectsDeleted(ObjectId, ObjectId);
    }

    void PhysicsSystem::ObjectsDeleted(int FirstObjectId, int LastObjectId)
    {
        if (!Impl_->Started)
            return;
        for (auto It = Impl_->Records.begin(); It != Impl_->Records.end();)
        {
            const auto Current = It++;
            if (Current->first >= FirstObjectId && Current->first <= LastObjectId)
                Impl_->DestroyRecord(Current);
        }
    }

    bool PhysicsSystem::CreateDynamicSphere(int ObjectId, std::string* Error)
    {
        return Impl_->CreateSphere(ObjectId, true, "ODE CREATE DYNAMIC SPHERE", Error);
    }

    bool PhysicsSystem::CreateStaticBox(int ObjectId, std::string* Error)
    {
        return Impl_->CreateBox(ObjectId, false, -1, "ODE CREATE STATIC BOX", Error);
    }

    bool PhysicsSystem::CreateStaticBoxLimb(int ObjectId, int Limb, std::string* Error)
    {
        return Impl_->CreateBox(ObjectId, false, Limb, "ODE CREATE STATIC BOX LIMB", Error);
    }

    bool PhysicsSystem::CreateDynamicBox(int ObjectId, std::string* Error)
    {
        return Impl_->CreateBox(ObjectId, true, -1, "ODE CREATE DYNAMIC BOX", Error);
    }

    bool PhysicsSystem::CreateDynamicBox(int ObjectId, int Limb, int TipOverMode, float Weight, float Friction,
                                         float Restitution, std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE DYNAMIC BOX";
        if (!ValidateFinite({Weight, Friction, Restitution}, Command, Error))
            return false;
        if (TipOverMode != 0 && TipOverMode != 1 && TipOverMode != 2 && TipOverMode != 3 && TipOverMode != 11)
            return Fail(Error, "ODE CREATE DYNAMIC BOX requires a tip-over mode of 0, 1, 2, 3 or 11");
        if ((Weight != -1.0f && Weight <= 0.0f) || (Friction != -1.0f && Friction < 0.0f) ||
            (Restitution != -1.0f && Restitution < 0.0f))
            return Fail(Error, "ODE CREATE DYNAMIC BOX requires a positive weight and no negative friction or bounce");
        return Impl_->CreateTunedBox(ObjectId, Limb, TipOverMode, Weight, Friction, Restitution, Command, Error);
    }

    bool PhysicsSystem::CreateDynamicBoxSized(int ObjectId, float XSize, float YSize, float ZSize, std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE DYNAMIC BOX";
        const Object3D* Object = Impl_->RequireObject(ObjectId, Command, Error);
        if (!Object || !ValidateFinite({XSize, YSize, ZSize}, Command, Error))
            return false;
        if (XSize <= 0.0f || YSize <= 0.0f || ZSize <= 0.0f)
            return Fail(Error, "ODE CREATE DYNAMIC BOX requires positive sizes");
        return Impl_->AddBody(ObjectId, *Object, Command, MakeBoxShape(0.5f * JPH::Vec3(XSize, YSize, ZSize)),
                              Impl::OptionsAt(*Object, true), Error) != nullptr;
    }

    bool PhysicsSystem::CreateDynamicBoxManual(int ObjectId, float X, float Y, float Z, float XSize, float YSize,
                                               float ZSize, float XDegrees, float YDegrees, float ZDegrees,
                                               std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE DYNAMIC BOX MANUAL";
        const Object3D* Object = Impl_->RequireObject(ObjectId, Command, Error);
        if (!Object || !ValidateFinite({X, Y, Z, XSize, YSize, ZSize, XDegrees, YDegrees, ZDegrees}, Command, Error))
            return false;
        if (XSize <= 0.0f || YSize <= 0.0f || ZSize <= 0.0f)
            return Fail(Error, "ODE CREATE DYNAMIC BOX MANUAL requires positive sizes");
        Impl::BodyOptions Options = Impl::OptionsAt(*Object, true);
        Options.Position = JPH::RVec3(X, Y, Z);
        Options.Rotation = ToPhysicsRotation(XDegrees, YDegrees, ZDegrees, Object->UseZyxRotation);
        if (!Impl_->AddBody(ObjectId, *Object, Command, MakeBoxShape(0.5f * JPH::Vec3(XSize, YSize, ZSize)), Options,
                            Error))
            return false;
        Impl_->Kernel.Objects().PositionObject(ObjectId, X, Y, Z);
        Impl_->Kernel.Objects().RotateObject(ObjectId, XDegrees, YDegrees, ZDegrees);
        return true;
    }

    bool PhysicsSystem::CreateDynamicCylinder(int ObjectId, std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE DYNAMIC CYLINDER";
        const Object3D* Object = Impl_->RequireObject(ObjectId, Command, Error);
        LocalBounds Bounds;
        if (!Object || !Impl_->CollisionBounds(*Object, ObjectId, -1, Command, Bounds, Error))
            return false;
        const JPH::Vec3 HalfExtent = Bounds.HalfExtent();
        return Impl_->AddBody(
                   ObjectId, *Object, Command,
                   OffsetShape(MakeCylinderShape(HalfExtent.GetY(), std::max(HalfExtent.GetX(), HalfExtent.GetZ())),
                               Bounds.Center()),
                   Impl::OptionsAt(*Object, true), Error) != nullptr;
    }

    bool PhysicsSystem::CreateStaticCylinder(int ObjectId, float X, float Y, float Z, float XSize, float YSize,
                                             float ZSize, float XRadians, float YRadians, float ZRadians,
                                             std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE STATIC CYLINDER";
        const Object3D* Object = Impl_->RequireObject(ObjectId, Command, Error);
        if (!Object || !ValidateFinite({X, Y, Z, XSize, YSize, ZSize, XRadians, YRadians, ZRadians}, Command, Error))
            return false;
        if (XSize <= 0.0f || YSize <= 0.0f || ZSize <= 0.0f)
            return Fail(Error, "ODE CREATE STATIC CYLINDER requires positive sizes");
        Impl::BodyOptions Options = Impl::OptionsAt(*Object, false);
        Options.Position = JPH::RVec3(X, Y, Z);
        // As in the plugin, this one form takes radians.
        Options.Rotation = JPH::Quat::sEulerAngles(JPH::Vec3(XRadians, YRadians, ZRadians));
        return Impl_->AddBody(ObjectId, *Object, Command,
                              MakeCylinderShape(std::max(0.5f * YSize, MinimumShapeExtent),
                                                std::max(0.5f * std::max(XSize, ZSize), MinimumShapeExtent)),
                              Options, Error) != nullptr;
    }

    bool PhysicsSystem::CreateDynamicCapsule(int ObjectId, float ScaleModifier, float Raised, std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE DYNAMIC CAPSULE";
        if (!ValidateFinite({ScaleModifier, Raised}, Command, Error))
            return false;
        if (ScaleModifier < 0.0f)
            return Fail(Error, "ODE CREATE DYNAMIC CAPSULE requires a non-negative scale modifier");
        return Impl_->CreateCapsule(ObjectId, true, ScaleModifier == 0.0f ? 1.0f : ScaleModifier, Raised, Command,
                                    Error);
    }

    bool PhysicsSystem::CreateStaticCapsule(int ObjectId, std::string* Error)
    {
        return Impl_->CreateCapsule(ObjectId, false, 1.0f, 0.0f, "ODE CREATE STATIC CAPSULE", Error);
    }

    bool PhysicsSystem::CreateDynamicTriangleMesh(int ObjectId, std::string* Error)
    {
        return Impl_->CreateDynamicHull(ObjectId, "ODE CREATE DYNAMIC TRIANGLE MESH", Error);
    }

    bool PhysicsSystem::CreateStaticTriangleMesh(int ObjectId, int Limb, int ScalingPercent, std::string* Error)
    {
        return Impl_->CreateStaticMesh(ObjectId, Limb, static_cast<float>(ScalingPercent),
                                       "ODE CREATE STATIC TRIANGLE MESH", Error);
    }

    bool PhysicsSystem::CreateStaticTriangleMeshLimb(int ObjectId, int Limb, std::string* Error)
    {
        return Impl_->CreateStaticMesh(ObjectId, Limb, 100.0f, "ODE CREATE STATIC TRIANGLE MESH LIMB", Error);
    }

    bool PhysicsSystem::CreateStaticTerrainMesh(int ObjectId, std::string* Error)
    {
        return Impl_->CreateStaticMesh(ObjectId, -1, 100.0f, "ODE CREATE STATIC TERRAIN MESH", Error);
    }

    bool PhysicsSystem::CreateStaticSphere(int ObjectId, std::string* Error)
    {
        return Impl_->CreateSphere(ObjectId, false, "ODE CREATE STATIC SPHERE", Error);
    }

    bool PhysicsSystem::DestroyObject(int ObjectId, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE DESTROY OBJECT", Error))
            return false;
        // As in the plugin, an object without a body has nothing to destroy.
        if (const auto It = Impl_->Records.find(ObjectId); It != Impl_->Records.end())
            Impl_->DestroyRecord(It);
        return true;
    }

    bool PhysicsSystem::SetWorldGravity(float X, float Y, float Z, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE SET WORLD GRAVITY", Error) ||
            !ValidateFinite({X, Y, Z}, "ODE SET WORLD GRAVITY", Error))
            return false;
        Impl_->World->SetGravity(JPH::Vec3(X, Y, Z));
        // Sleeping bodies would otherwise ignore the new gravity until something touches them.
        JPH::BodyIDVector AllBodies;
        Impl_->World->GetBodies(AllBodies);
        Impl_->Bodies().ActivateBodies(AllBodies.data(), static_cast<int>(AllBodies.size()));
        return true;
    }

    bool PhysicsSystem::SetWorldStep(float Seconds, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE SET WORLD STEP", Error) ||
            !ValidateFinite({Seconds}, "ODE SET WORLD STEP", Error))
            return false;
        if (Seconds < MinimumStepSeconds || Seconds > MaximumStepSeconds)
            return Fail(Error, "ODE SET WORLD STEP requires a step between 0.001 and 0.1 seconds");
        Impl_->StepSeconds = Seconds;
        return true;
    }

    bool PhysicsSystem::SetWorldErp(float Value, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE SET WORLD ERP", Error) || !ValidateFinite({Value}, "ODE SET WORLD ERP", Error))
            return false;
        if (Value < 0.0f || Value > 1.0f)
            return Fail(Error, "ODE SET WORLD ERP requires a value between 0 and 1");
        Impl_->PositionCorrection = Value;
        Impl_->ApplyPhysicsSettings();
        return true;
    }

    bool PhysicsSystem::SetWorldQuickStepIterations(int Iterations, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE SET WORLD QUICK STEP NUM ITERATIONS", Error))
            return false;
        if (Iterations < 1)
            return Fail(Error, "ODE SET WORLD QUICK STEP NUM ITERATIONS requires at least one iteration");
        Impl_->VelocitySteps = std::clamp(Iterations, MinimumVelocitySteps, MaximumVelocitySteps);
        Impl_->ApplyPhysicsSettings();
        return true;
    }

    bool PhysicsSystem::SetStepMode(int Mode, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE SET STEP MODE", Error))
            return false;
        if (Mode != 0 && Mode != 1)
            return Fail(Error, "ODE SET STEP MODE requires 0 (quick) or 1 (exact)");
        Impl_->ExactStepMode = Mode == 1;
        Impl_->ApplyPhysicsSettings();
        return true;
    }

    bool PhysicsSystem::SetBodyPosition(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET BODY POSITION", Error);
        if (!Record || !ValidateFinite({X, Y, Z}, "ODE SET BODY POSITION", Error))
            return false;
        Impl_->WakeTouchingBodies(*Record);
        Impl_->Bodies().SetPosition(Record->Id, JPH::RVec3(X, Y, Z), JPH::EActivation::Activate);
        Impl_->Kernel.Objects().PositionObject(ObjectId, X, Y, Z);
        return true;
    }

    bool PhysicsSystem::SetBodyRotation(int ObjectId, float XDegrees, float YDegrees, float ZDegrees,
                                        std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET BODY ROTATION", Error);
        if (!Record || !ValidateFinite({XDegrees, YDegrees, ZDegrees}, "ODE SET BODY ROTATION", Error))
            return false;
        Impl_->WakeTouchingBodies(*Record);
        Impl_->Bodies().SetRotation(Record->Id, ToPhysicsRotation(XDegrees, YDegrees, ZDegrees, Record->ZyxRotation),
                                    JPH::EActivation::Activate);
        Impl_->Kernel.Objects().RotateObject(ObjectId, XDegrees, YDegrees, ZDegrees);
        return true;
    }

    bool PhysicsSystem::SetLinearVelocity(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET LINEAR VELOCITY", Error);
        if (!Record || !ValidateFinite({X, Y, Z}, "ODE SET LINEAR VELOCITY", Error))
            return false;
        Impl_->Bodies().ActivateBody(Record->Id);
        Impl_->Bodies().SetLinearVelocity(Record->Id, JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::SetAngularVelocity(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET ANGULAR VELOCITY", Error);
        if (!Record || !ValidateFinite({X, Y, Z}, "ODE SET ANGULAR VELOCITY", Error))
            return false;
        Impl_->Bodies().ActivateBody(Record->Id);
        Impl_->Bodies().SetAngularVelocity(Record->Id, JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::SetBodyFriction(int ObjectId, float Friction, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET BODY FRICTION", Error);
        if (!Record || !ValidateFinite({Friction}, "ODE SET BODY FRICTION", Error))
            return false;
        if (Friction < 0.0f)
            return Fail(Error, "ODE SET BODY FRICTION requires a non-negative value");
        Impl_->Bodies().SetFriction(Record->Id, Friction);
        return true;
    }

    bool PhysicsSystem::SetBodyMass(int ObjectId, float Mass, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET BODY MASS", Error);
        if (!Record || !ValidateFinite({Mass}, "ODE SET BODY MASS", Error))
            return false;
        if (Mass <= 0.0f)
            return Fail(Error, "ODE SET BODY MASS requires a positive mass");
        const JPH::BodyLockWrite Lock(Impl_->World->GetBodyLockInterfaceNoLock(), Record->Id);
        if (!Lock.Succeeded())
            return Fail(Error, "ODE SET BODY MASS could not write the body of object " + std::to_string(ObjectId));
        Lock.GetBody().GetMotionProperties()->ScaleToMass(Mass);
        return true;
    }

    bool PhysicsSystem::SetBodyDamping(int ObjectId, float Linear, float Angular, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET BODY DAMPING", Error);
        if (!Record || !ValidateFinite({Linear, Angular}, "ODE SET BODY DAMPING", Error))
            return false;
        if (Linear < 0.0f || Angular < 0.0f)
            return Fail(Error, "ODE SET BODY DAMPING requires non-negative values");
        const JPH::BodyLockWrite Lock(Impl_->World->GetBodyLockInterfaceNoLock(), Record->Id);
        if (!Lock.Succeeded())
            return Fail(Error, "ODE SET BODY DAMPING could not write the body of object " + std::to_string(ObjectId));
        JPH::MotionProperties* Motion = Lock.GetBody().GetMotionProperties();
        Motion->SetLinearDamping(Linear);
        Motion->SetAngularDamping(Angular);
        return true;
    }

    bool PhysicsSystem::SetBodyKinematic(int ObjectId, int Kinematic, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET BODY KINEMATIC", Error);
        if (!Record)
            return false;
        Record->KinematicRequested = Kinematic != 0;
        Impl_->ApplyMotionType(*Record);
        return true;
    }

    bool PhysicsSystem::SetResponse(int ObjectId, int Mode, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET RESPONSE", Error);
        if (!Record)
            return false;
        if (Mode < 0 || Mode > 2)
            return Fail(Error, "ODE SET RESPONSE requires 0 (default), 1 (ignore universe) or 2 (pseudo-static)");
        // Mode 1 only ignored ODE CREATE STATIC UNIVERSE's mesh, which cannot exist here, so it responds like mode 0.
        // Mode 2 holds still and still pushes others, which is what a kinematic body does.
        Record->PseudoStatic = Mode == 2;
        Impl_->ApplyMotionType(*Record);
        return true;
    }

    bool PhysicsSystem::SetRayCast(int ObjectId, int Mode, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET RAY CAST", Error);
        if (!Record)
            return false;
        Impl_->Bodies().SetMotionQuality(Record->Id,
                                         Mode > 0 ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
        return true;
    }

    bool PhysicsSystem::AddForce(int ObjectId, float X, float Y, float Z, float PointX, float PointY, float PointZ,
                                 std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE ADD FORCE", Error);
        if (!Record || !ValidateFinite({X, Y, Z, PointX, PointY, PointZ}, "ODE ADD FORCE", Error))
            return false;
        const JPH::Vec3 Force(X, Y, Z);
        Impl_->AddPendingForce(*Record, Force, JPH::Vec3(PointX, PointY, PointZ).Cross(Force));
        return true;
    }

    bool PhysicsSystem::AddTorque(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE ADD TORQUE", Error);
        if (!Record || !ValidateFinite({X, Y, Z}, "ODE ADD TORQUE", Error))
            return false;
        Impl_->AddPendingForce(*Record, JPH::Vec3::sZero(), JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::AddImpulse(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE ADD IMPULSE", Error);
        if (!Record || !ValidateFinite({X, Y, Z}, "ODE ADD IMPULSE", Error))
            return false;
        Impl_->Bodies().AddImpulse(Record->Id, JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::SetGravity(int ObjectId, int Enabled, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET GRAVITY", Error);
        if (!Record)
            return false;
        Impl_->Bodies().SetGravityFactor(Record->Id, Enabled == 0 ? 0.0f : 1.0f);
        Impl_->Bodies().ActivateBody(Record->Id);
        return true;
    }

    bool PhysicsSystem::SetEnabled(int ObjectId, int Enabled, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET ENABLED", Error);
        if (!Record)
            return false;
        const bool Enable = Enabled != 0;
        if (Enable == Record->Enabled)
            return true;
        Impl_->WakeTouchingBodies(*Record);
        Impl_->Bodies().SetObjectLayer(Record->Id,
                                       Enable ? (Record->Moving ? MovingLayer : NonMovingLayer) : DisabledLayer);
        if (Record->Moving)
            Impl_->Bodies().ActivateBody(Record->Id);
        Record->Enabled = Enable;
        return true;
    }

    bool PhysicsSystem::SetActive(int ObjectId, int Active, std::string* Error)
    {
        const Impl::BodyRecord* Record = Impl_->RequireMovingBody(ObjectId, "ODE SET ACTIVE", Error);
        if (!Record)
            return false;
        if (Active != 0)
            Impl_->Bodies().ActivateBody(Record->Id);
        else
            Impl_->Bodies().DeactivateBody(Record->Id);
        return true;
    }

    bool PhysicsSystem::BodyHeight(float& Output, int ObjectId, std::string* Error) const
    {
        const Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE GET BODY HEIGHT", Error);
        if (!Record)
            return false;
        const JPH::BodyLockRead Lock(Impl_->World->GetBodyLockInterfaceNoLock(), Record->Id);
        if (!Lock.Succeeded())
            return Fail(Error, "ODE GET BODY HEIGHT could not read the body of object " + std::to_string(ObjectId));
        Output = Lock.GetBody().GetWorldSpaceBounds().GetSize().GetY();
        return true;
    }

    bool PhysicsSystem::LinearVelocity(float& Output, int ObjectId, int Axis, std::string* Error) const
    {
        static constexpr const char* Commands[] = {"ODE GET BODY LINEAR VELOCITY X", "ODE GET BODY LINEAR VELOCITY Y",
                                                   "ODE GET BODY LINEAR VELOCITY Z"};
        const Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, Commands[Axis], Error);
        if (!Record)
            return false;
        Output = Impl_->Bodies().GetLinearVelocity(Record->Id)[Axis];
        return true;
    }

    bool PhysicsSystem::AngularVelocity(float& Output, int ObjectId, int Axis, std::string* Error) const
    {
        static constexpr const char* Commands[] = {"ODE GET BODY ANGULAR VELOCITY X", "ODE GET BODY ANGULAR VELOCITY Y",
                                                   "ODE GET BODY ANGULAR VELOCITY Z"};
        const Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, Commands[Axis], Error);
        if (!Record)
            return false;
        Output = Impl_->Bodies().GetAngularVelocity(Record->Id)[Axis];
        return true;
    }
} // namespace dbc
