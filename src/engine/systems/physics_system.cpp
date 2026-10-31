#include "physics_system.hpp"

#include "../engine_math.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "log.hpp"
#include "object_manager.hpp"
#include "renderer_system.hpp"

// Jolt requires its configuration header before all other Jolt headers.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
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

        // The categories of ODE RAY's mask.
        constexpr int RayCategoryTerrain = 1;
        constexpr int RayCategoryObject = 2;
        constexpr int RayCategoryCapsule = 4;
        constexpr int RayCategoryAny = RayCategoryTerrain | RayCategoryObject | RayCategoryCapsule;

        // Both are fixed when the world starts, so a step never allocates for its contacts. The first holds what one
        // step reports and the second what the program has not read yet, which loses the oldest when full.
        constexpr std::uint32_t MaximumStepContacts = 8192;
        constexpr std::uint32_t MaximumCollisionMessages = 4096;

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

        // The plugin's character moved by its walk speed on each of Bullet's ticks of 1/120 s.
        constexpr float CharacterTicksPerSecond = 120.0f;
        // Its capsule stood 70 tall, 52 when ducked, and climbed steps of 10.35; here those are shares of the object.
        constexpr float CharacterDuckedHeightShare = 52.0f / 70.0f;
        constexpr float CharacterStepHeightShare = 10.35f / 70.0f;
        // Jolt's stair settings suit a radius of 0.3. Testing a step's slope has to reach past the capsule's round end.
        constexpr float CharacterStairForwardTestShare = 0.5f;
        constexpr float CharacterStairMinimumForwardShare = 0.02f / 0.3f;
        constexpr float MinimumCharacterSlopeDegrees = 1.0f;
        constexpr float MaximumCharacterSlopeDegrees = 90.0f;

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

    struct PhysicsSystem::Impl final : JPH::ContactListener
    {
        struct CharacterRecord
        {
            JPH::Ref<JPH::CharacterVirtual> Character;
            JPH::RefConst<JPH::Shape> StandingShape;
            JPH::RefConst<JPH::Shape> DuckedShape;
            float Radius = 0.0f;
            float StepHeight = 0.0f;
            float Gravity = 0.0f;
            float FallSpeed = 0.0f;
            float JumpSpeed = 0.0f;
            float ThrustSpeed = 0.0f;
            JPH::Vec3 WalkVelocity = JPH::Vec3::sZero();
            bool Ducked = false;
            bool Airborne = false;
            float HighestY = 0.0f;
        };

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
            int RayCategory = RayCategoryObject;
            bool BounceEnabled = false;
            float Bounce = 0.0f;
            float BounceVelocity = 0.0f;
            float Restitution = 0.0f;
            JPH::Vec3 PendingForce = JPH::Vec3::sZero();
            JPH::Vec3 PendingTorque = JPH::Vec3::sZero();
            int JointCount = 0;
            // Set when the body is a character's inner body, which the character owns.
            std::unique_ptr<CharacterRecord> Control;

            [[nodiscard]] bool Kinematic() const
            {
                return KinematicRequested || PseudoStatic;
            }
        };

        struct JointRecord
        {
            // A swing door's hinge has no number and holds its body to the world, with object B at 0.
            int Id = 0;
            bool Named = false;
            int ObjectA = 0;
            int ObjectB = 0;
            JPH::Ref<JPH::Constraint> Constraint;
            JPH::HingeConstraint* Hinge = nullptr;
        };

        struct BodyOptions
        {
            bool Dynamic = false;
            bool Sensor = false;
            bool LockRotation = false;
            int RayCategory = RayCategoryObject;
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
            World->SetContactListener(this);
            StepContacts = std::make_unique<PhysicsCollisionMessage[]>(MaximumStepContacts);
            CollisionMessages = std::make_unique<PhysicsCollisionMessage[]>(MaximumCollisionMessages);
            StepContactCount.store(0, std::memory_order_relaxed);
            CollisionMessageHead = 0;
            CollisionMessageCount = 0;
            CurrentMessage = {};
            ContactOverflowLogged = false;
            LastRay = {};
            const JPH::PhysicsSettings& Defaults = World->GetPhysicsSettings();
            PositionCorrection = Defaults.mBaumgarte;
            VelocitySteps = static_cast<int>(Defaults.mNumVelocitySteps);
            PositionSteps = static_cast<int>(Defaults.mNumPositionSteps);
            ExactStepMode = false;
            AnyPendingForce = false;
            CharacterCount = 0;
            CharacterHitFloor = 0;
            CharacterFallDistance = 0.0f;
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
            // A constraint keeps pointers to its bodies, so it goes first.
            for (const JointRecord& Joint : Joints)
                World->RemoveConstraint(Joint.Constraint);
            Joints.clear();
            PendingCompound = nullptr;
            for (auto& [ObjectId, Record] : Records)
            {
                static_cast<void>(ObjectId);
                RemoveBody(Record);
            }
            Records.clear();
            Started = false;
            World.reset();
            StepContacts.reset();
            CollisionMessages.reset();
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
            if (Record && Record->Control)
            {
                Fail(Error, std::string(Command) + " does not apply to the character controller of object " +
                                std::to_string(ObjectId) + "; ODE CONTROL CHARACTER CONTROLLER moves it");
                return nullptr;
            }
            return Record;
        }

        BodyRecord* RequireCharacter(int ObjectId, const char* Command, std::string* Error)
        {
            BodyRecord* Record = RequireBody(ObjectId, Command, Error);
            if (Record && !Record->Control)
            {
                Fail(Error,
                     std::string(Command) + " requires a character controller for object " + std::to_string(ObjectId));
                return nullptr;
            }
            return Record;
        }

        // The plugin kept one character, so its landing forms name none; here they report any character's landing.
        bool RequireAnyCharacter(const char* Command, std::string* Error) const
        {
            if (!RequireStarted(Command, Error))
                return false;
            if (CharacterCount == 0)
                return Fail(Error, std::string(Command) + " requires ODE CREATE CHARACTER CONTROLLER");
            return true;
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

        void RemoveBody(BodyRecord& Record)
        {
            if (Record.Control)
            {
                // The character removes its inner body when it goes.
                Record.Control.reset();
                --CharacterCount;
                return;
            }
            JPH::BodyInterface& Interface = Bodies();
            Interface.RemoveBody(Record.Id);
            Interface.DestroyBody(Record.Id);
        }

        // A jointed neighbour may be asleep, held only by this joint.
        void ReleaseJointBody(int ObjectId)
        {
            const auto It = Records.find(ObjectId);
            if (It == Records.end())
                return;
            --It->second.JointCount;
            if (It->second.Moving)
                Bodies().ActivateBody(It->second.Id);
        }

        void RemoveJointsOf(int ObjectId)
        {
            for (auto It = Joints.begin(); It != Joints.end();)
            {
                if (It->ObjectA != ObjectId && It->ObjectB != ObjectId)
                {
                    ++It;
                    continue;
                }
                World->RemoveConstraint(It->Constraint);
                ReleaseJointBody(It->ObjectA);
                ReleaseJointBody(It->ObjectB);
                It = Joints.erase(It);
            }
        }

        void DestroyRecord(std::unordered_map<int, BodyRecord>::iterator It)
        {
            RemoveJointsOf(It->first);
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

            // Map nodes do not move, so workers can reach the record through the body during a step.
            BodyRecord& Record = Records[ObjectId];
            JPH::BodyCreationSettings Settings(Shape.GetPtr(), Options.Position, Options.Rotation,
                                               Options.Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                               Options.Dynamic ? MovingLayer : NonMovingLayer);
            Settings.mFriction = Options.Friction;
            Settings.mRestitution = Options.Restitution;
            Settings.mLinearDamping = DefaultLinearDamping;
            Settings.mAngularDamping = DefaultAngularDamping;
            Settings.mIsSensor = Options.Sensor;
            Settings.mUserData = reinterpret_cast<JPH::uint64>(&Record);
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
                Records.erase(ObjectId);
                Fail(Error, std::string(Command) + " exhausted the physics body pool");
                return nullptr;
            }
            Record.ObjectId = ObjectId;
            Record.Id = BodyId;
            Record.Moving = Options.Dynamic;
            Record.ZyxRotation = Object.UseZyxRotation;
            Record.RayCategory = Options.RayCategory;
            Record.Restitution = Options.Restitution;
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
            Options.RayCategory = RayCategoryCapsule;
            BodyRecord* Record =
                AddBody(ObjectId, *Object, Command,
                        OffsetShape(new JPH::CapsuleShape(Size.HalfHeight, Size.Radius), Offset), Options, Error);
            if (!Record)
                return false;
            Record->SyncRotation = false;
            return true;
        }

        bool CreateStaticMesh(int ObjectId, int Limb, float ScalingPercent, int RayCategory, const char* Command,
                              std::string* Error)
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
            BodyOptions Options = OptionsAt(*Object, false);
            Options.RayCategory = RayCategory;
            return AddBody(ObjectId, *Object, Command, Result.Get(), Options, Error) != nullptr;
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

        bool RequireJointBodies(int JointId, int ObjectA, int ObjectB, const char* Command, std::string* Error)
        {
            if (!RequireStarted(Command, Error))
                return false;
            for (const JointRecord& Joint : Joints)
            {
                if (Joint.Named && Joint.Id == JointId)
                    return Fail(Error, std::string(Command) + " requires a new joint number, but joint " +
                                           std::to_string(JointId) + " exists");
            }
            const BodyRecord* A = RequireBody(ObjectA, Command, Error);
            const BodyRecord* B = A ? RequireBody(ObjectB, Command, Error) : nullptr;
            if (!A || !B)
                return false;
            if (ObjectA == ObjectB)
                return Fail(Error, std::string(Command) + " requires two different bodies");
            if (A->Control || B->Control)
                return Fail(Error, std::string(Command) + " cannot join a character controller");
            if (!A->Moving && !B->Moving)
                return Fail(Error, std::string(Command) + " requires at least one moving body");
            return true;
        }

        // Object A is what a hinge's angle measures, as in ODE; Jolt measures its second body against its first.
        JointRecord* AddJoint(int JointId, bool Named, int ObjectA, int ObjectB,
                              const JPH::TwoBodyConstraintSettings& Settings, const char* Command, std::string* Error)
        {
            const auto Body = [this](int ObjectId)
            {
                const auto It = Records.find(ObjectId);
                return It == Records.end() ? JPH::BodyID() : It->second.Id;
            };
            JPH::Ref<JPH::Constraint> Constraint = Bodies().CreateConstraint(&Settings, Body(ObjectB), Body(ObjectA));
            if (!Constraint)
            {
                Fail(Error, std::string(Command) + " could not create its constraint");
                return nullptr;
            }
            World->AddConstraint(Constraint);
            Bodies().ActivateConstraint(static_cast<JPH::TwoBodyConstraint*>(Constraint.GetPtr()));
            JointRecord& Joint = Joints.emplace_back();
            Joint.Id = JointId;
            Joint.Named = Named;
            Joint.ObjectA = ObjectA;
            Joint.ObjectB = ObjectB;
            Joint.Constraint = std::move(Constraint);
            for (const int ObjectId : {ObjectA, ObjectB})
            {
                if (const auto It = Records.find(ObjectId); It != Records.end())
                    ++It->second.JointCount;
            }
            return &Joint;
        }

        // The capsule stands on the object's lowest point, in the body's own space, so changing it keeps the feet.
        [[nodiscard]] static JPH::RefConst<JPH::Shape> MakeCharacterShape(const LocalBounds& Bounds, float Height,
                                                                          float Radius)
        {
            const JPH::Vec3 Center = Bounds.Center();
            const float HalfCylinder = std::max(0.5f * Height - Radius, MinimumShapeExtent);
            return OffsetShape(new JPH::CapsuleShape(HalfCylinder, Radius),
                               JPH::Vec3(Center.GetX(), Bounds.Min.GetY() + Radius + HalfCylinder, Center.GetZ()));
        }

        bool CreateCharacter(int ObjectId, float Gravity, float FallSpeed, float MaximumSlopeDegrees,
                             std::string* Error)
        {
            constexpr const char* Command = "ODE CREATE CHARACTER CONTROLLER";
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !ValidateFinite({Gravity, FallSpeed, MaximumSlopeDegrees}, Command, Error) ||
                !CollisionBounds(*Object, ObjectId, -1, Command, Bounds, Error))
                return false;
            if (Gravity < 0.0f || FallSpeed <= 0.0f)
                return Fail(Error,
                            "ODE CREATE CHARACTER CONTROLLER requires no negative gravity and a positive fall speed");
            if (MaximumSlopeDegrees < MinimumCharacterSlopeDegrees ||
                MaximumSlopeDegrees > MaximumCharacterSlopeDegrees)
                return Fail(Error, "ODE CREATE CHARACTER CONTROLLER requires a maximum slope between 1 and 90 degrees");
            if (const auto Existing = Records.find(ObjectId); Existing != Records.end())
                DestroyRecord(Existing);

            const JPH::Vec3 Size = Bounds.Size();
            const float Radius = std::max(0.25f * (Size.GetX() + Size.GetZ()), MinimumShapeExtent);
            const float Height = std::max(Size.GetY(), 2.0f * Radius);
            auto Control = std::make_unique<CharacterRecord>();
            Control->StandingShape = MakeCharacterShape(Bounds, Height, Radius);
            Control->DuckedShape = MakeCharacterShape(Bounds, CharacterDuckedHeightShare * Height, Radius);
            Control->Radius = Radius;
            Control->StepHeight = CharacterStepHeightShare * Height;
            Control->Gravity = Gravity;
            Control->FallSpeed = FallSpeed;
            Control->HighestY = Object->PosY;

            JPH::Ref<JPH::CharacterVirtualSettings> Settings = new JPH::CharacterVirtualSettings();
            Settings->mShape = Control->StandingShape;
            Settings->mInnerBodyShape = Control->StandingShape;
            Settings->mInnerBodyLayer = MovingLayer;
            Settings->mMaxSlopeAngle = MaximumSlopeDegrees * DegreesToRadians;
            // Only what touches the lower end of the capsule can hold it up.
            Settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -(Bounds.Min.GetY() + Radius));

            // Map nodes do not move, so workers can reach the record through the inner body during a step.
            BodyRecord& Record = Records[ObjectId];
            Record.ObjectId = ObjectId;
            Record.Moving = true;
            Record.SyncRotation = false;
            Record.ZyxRotation = Object->UseZyxRotation;
            Record.RayCategory = RayCategoryCapsule;
            Control->Character =
                new JPH::CharacterVirtual(Settings, JPH::RVec3(Object->PosX, Object->PosY, Object->PosZ),
                                          JPH::Quat::sIdentity(), reinterpret_cast<JPH::uint64>(&Record), World.get());
            Record.Id = Control->Character->GetInnerBodyID();
            if (Record.Id.IsInvalid())
            {
                Records.erase(ObjectId);
                return Fail(Error, "ODE CREATE CHARACTER CONTROLLER exhausted the physics body pool");
            }
            JPH::BodyFilter AnyBody;
            JPH::ShapeFilter AnyShape;
            Control->Character->RefreshContacts(World->GetDefaultBroadPhaseLayerFilter(MovingLayer),
                                                World->GetDefaultLayerFilter(MovingLayer), AnyBody, AnyShape,
                                                *TempAllocator);
            Record.Control = std::move(Control);
            ++CharacterCount;
            return true;
        }

        void SetCharacterDucked(CharacterRecord& Control, bool Ducked)
        {
            if (Control.Ducked == Ducked)
                return;
            // Going down always fits. Standing up under something waits until there is room, as in the plugin.
            const float MaximumPenetration = Ducked ? FLT_MAX : 1.5f * World->GetPhysicsSettings().mPenetrationSlop;
            const JPH::RefConst<JPH::Shape>& Shape = Ducked ? Control.DuckedShape : Control.StandingShape;
            JPH::BodyFilter AnyBody;
            JPH::ShapeFilter AnyShape;
            if (!Control.Character->SetShape(
                    Shape.GetPtr(), MaximumPenetration, World->GetDefaultBroadPhaseLayerFilter(MovingLayer),
                    World->GetDefaultLayerFilter(MovingLayer), AnyBody, AnyShape, *TempAllocator))
                return;
            Control.Character->SetInnerBodyShape(Shape.GetPtr());
            Control.Ducked = Ducked;
        }

        void StepCharacter(CharacterRecord& Control)
        {
            using GroundState = JPH::CharacterVirtual::EGroundState;
            JPH::CharacterVirtual& Character = *Control.Character;
            Character.UpdateGroundVelocity();
            const JPH::Vec3 GroundVelocity = Character.GetGroundVelocity();
            const float VerticalVelocity = Character.GetLinearVelocity().GetY();
            // A jump that has just begun still touches the ground, without standing on it.
            const bool Standing =
                Character.GetGroundState() == GroundState::OnGround && VerticalVelocity - GroundVelocity.GetY() < 0.1f;
            JPH::Vec3 Velocity = Standing ? GroundVelocity : JPH::Vec3(0.0f, VerticalVelocity, 0.0f);
            if (Standing)
                Velocity.SetY(Velocity.GetY() + Control.JumpSpeed);
            Control.JumpSpeed = 0.0f;
            Velocity.SetY(std::max(Velocity.GetY() - Control.Gravity * StepSeconds, -Control.FallSpeed));
            if (Control.ThrustSpeed > 0.0f)
                Velocity.SetY(std::max(Velocity.GetY(), Control.ThrustSpeed));
            Character.SetLinearVelocity(Velocity + Control.WalkVelocity);

            JPH::CharacterVirtual::ExtendedUpdateSettings Update;
            Update.mStickToFloorStepDown = JPH::Vec3(0.0f, -Control.StepHeight, 0.0f);
            Update.mWalkStairsStepUp = JPH::Vec3(0.0f, Control.StepHeight, 0.0f);
            Update.mWalkStairsStepForwardTest = CharacterStairForwardTestShare * Control.Radius;
            Update.mWalkStairsMinStepForward = CharacterStairMinimumForwardShare * Control.Radius;
            JPH::BodyFilter AnyBody;
            JPH::ShapeFilter AnyShape;
            Character.ExtendedUpdate(StepSeconds, JPH::Vec3(0.0f, -Control.Gravity, 0.0f), Update,
                                     World->GetDefaultBroadPhaseLayerFilter(MovingLayer),
                                     World->GetDefaultLayerFilter(MovingLayer), AnyBody, AnyShape, *TempAllocator);

            const auto Height = static_cast<float>(Character.GetPosition().GetY());
            if (Character.GetGroundState() != GroundState::OnGround)
            {
                Control.Airborne = true;
                Control.HighestY = std::max(Control.HighestY, Height);
                return;
            }
            if (Control.Airborne)
            {
                CharacterHitFloor = 1;
                CharacterFallDistance = std::max(CharacterFallDistance, Control.HighestY - Height);
            }
            Control.Airborne = false;
            Control.HighestY = Height;
        }

        // Jolt does not simulate a character: it moves on the program's thread, before the step around it.
        void StepCharacters()
        {
            if (CharacterCount == 0)
                return;
            for (auto& [ObjectId, Record] : Records)
            {
                static_cast<void>(ObjectId);
                if (Record.Control && Record.Enabled)
                    StepCharacter(*Record.Control);
            }
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

        void ApplyBounce(const BodyRecord& Record)
        {
            Bodies().SetRestitution(Record.Id, Record.BounceEnabled ? Record.Bounce : Record.Restitution);
        }

        static const BodyRecord& RecordOf(const JPH::Body& Body)
        {
            return *reinterpret_cast<const BodyRecord*>(Body.GetUserData());
        }

        // Runs on Jolt's workers: it reads what cannot change during a step, and takes its slot atomically.
        void RecordContact(const JPH::Body& First, const JPH::Body& Second, const JPH::ContactManifold& Manifold)
        {
            const std::uint32_t Slot = StepContactCount.fetch_add(1, std::memory_order_relaxed);
            if (Slot >= MaximumStepContacts)
                return;

            const BodyRecord& FirstRecord = RecordOf(First);
            const BodyRecord& SecondRecord = RecordOf(Second);
            // The plugin's object A is a body that moves; among two of them, the lower number keeps it repeatable.
            const bool Swap =
                First.IsStatic() != Second.IsStatic() ? First.IsStatic() : SecondRecord.ObjectId < FirstRecord.ObjectId;
            const JPH::Body& A = Swap ? Second : First;
            const JPH::Body& B = Swap ? First : Second;

            const auto Store = [](float* Output, JPH::Vec3Arg Value)
            {
                Output[0] = Value.GetX();
                Output[1] = Value.GetY();
                Output[2] = Value.GetZ();
            };
            PhysicsCollisionMessage& Message = StepContacts[Slot];
            Message.ObjectA = RecordOf(A).ObjectId;
            Message.ObjectB = RecordOf(B).ObjectId;
            Message.ContactA = A.IsStatic() ? 0.0f : 1.0f;
            Message.ContactB = B.IsStatic() ? 0.0f : 1.0f;
            Store(Message.VelocityA, A.GetLinearVelocity());
            Store(Message.AngularVelocityA, A.GetAngularVelocity());
            Store(Message.VelocityB, B.GetLinearVelocity());
            Store(Message.AngularVelocityB, B.GetAngularVelocity());

            // Jolt's normal runs from its first body to its second.
            Store(Message.Normal, Swap ? -Manifold.mWorldSpaceNormal : Manifold.mWorldSpaceNormal);
            // A speculative contact has not touched yet; its negative depth is how far it has to go.
            Message.Depth = std::max(Manifold.mPenetrationDepth, 0.0f);
            const JPH::uint PointCount = Manifold.mRelativeContactPointsOn1.size();
            JPH::Vec3 Sum = JPH::Vec3::sZero();
            for (JPH::uint Index = 0; Index < PointCount; ++Index)
                Sum += JPH::Vec3(Swap ? Manifold.GetWorldSpaceContactPointOn2(Index)
                                      : Manifold.GetWorldSpaceContactPointOn1(Index));
            Store(Message.Point, Sum / static_cast<float>(std::max<JPH::uint>(PointCount, 1)));
        }

        // Jolt's own threshold is global, while the plugin gives each body one.
        static void ApplyBounceThreshold(const JPH::Body& First, const JPH::Body& Second,
                                         const JPH::ContactManifold& Manifold, JPH::ContactSettings& Settings)
        {
            const BodyRecord& FirstRecord = RecordOf(First);
            const BodyRecord& SecondRecord = RecordOf(Second);
            const float Threshold = std::max(FirstRecord.BounceEnabled ? FirstRecord.BounceVelocity : 0.0f,
                                             SecondRecord.BounceEnabled ? SecondRecord.BounceVelocity : 0.0f);
            if (Threshold <= 0.0f || Settings.mCombinedRestitution <= 0.0f)
                return;
            const JPH::RVec3 Point = Manifold.GetWorldSpaceContactPointOn1(0);
            const JPH::Vec3 Relative = First.GetPointVelocity(Point) - Second.GetPointVelocity(Point);
            if (Relative.Dot(Manifold.mWorldSpaceNormal) < Threshold)
                Settings.mCombinedRestitution = 0.0f;
        }

        // Runs on Jolt's workers. Joints do not change during a step, and few bodies get as far as the search.
        JPH::ValidateResult OnContactValidate(const JPH::Body& First, const JPH::Body& Second, JPH::RVec3Arg,
                                              const JPH::CollideShapeResult&) override
        {
            const BodyRecord& A = RecordOf(First);
            const BodyRecord& B = RecordOf(Second);
            if (A.JointCount > 0 && B.JointCount > 0)
            {
                for (const JointRecord& Joint : Joints)
                {
                    if ((Joint.ObjectA == A.ObjectId && Joint.ObjectB == B.ObjectId) ||
                        (Joint.ObjectA == B.ObjectId && Joint.ObjectB == A.ObjectId))
                        return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                }
            }
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        void OnContactAdded(const JPH::Body& First, const JPH::Body& Second, const JPH::ContactManifold& Manifold,
                            JPH::ContactSettings& Settings) override
        {
            ApplyBounceThreshold(First, Second, Manifold, Settings);
            RecordContact(First, Second, Manifold);
        }

        void OnContactPersisted(const JPH::Body& First, const JPH::Body& Second, const JPH::ContactManifold& Manifold,
                                JPH::ContactSettings& Settings) override
        {
            ApplyBounceThreshold(First, Second, Manifold, Settings);
            RecordContact(First, Second, Manifold);
        }

        // Workers report in any order and may report a pair for several of its sub-shapes. Sorting makes the outcome
        // repeatable, and leaves the deepest contact of each pair first.
        void CollectStepContacts()
        {
            const std::uint32_t Reported = StepContactCount.exchange(0, std::memory_order_relaxed);
            const std::uint32_t Count = std::min(Reported, MaximumStepContacts);
            if (Reported > MaximumStepContacts && !ContactOverflowLogged)
            {
                DBC_LOG_WARN("ODE: a step reported {} contacts; collision messages keep the first {}", Reported,
                             MaximumStepContacts);
                ContactOverflowLogged = true;
            }
            const auto Order = [](const PhysicsCollisionMessage& Left, const PhysicsCollisionMessage& Right)
            {
                if (Left.ObjectA != Right.ObjectA)
                    return Left.ObjectA < Right.ObjectA;
                if (Left.ObjectB != Right.ObjectB)
                    return Left.ObjectB < Right.ObjectB;
                if (Left.Depth != Right.Depth)
                    return Left.Depth > Right.Depth;
                return std::lexicographical_compare(Left.Point, Left.Point + 3, Right.Point, Right.Point + 3);
            };
            std::sort(StepContacts.get(), StepContacts.get() + Count, Order);
            for (std::uint32_t Index = 0; Index < Count; ++Index)
            {
                if (Index > 0 && StepContacts[Index].ObjectA == StepContacts[Index - 1].ObjectA &&
                    StepContacts[Index].ObjectB == StepContacts[Index - 1].ObjectB)
                    continue;
                CollisionMessages[CollisionMessageHead] = StepContacts[Index];
                CollisionMessageHead = (CollisionMessageHead + 1) % MaximumCollisionMessages;
                CollisionMessageCount = std::min(CollisionMessageCount + 1, MaximumCollisionMessages);
            }
        }

        // What a ray may hit; workers play no part, so it runs on the program's thread.
        class RayBodyFilter final : public JPH::BodyFilter
        {
          public:
            RayBodyFilter(int Mask, int OnlyObjectId) : Mask_(Mask), OnlyObjectId_(OnlyObjectId) {}

            bool ShouldCollideLocked(const JPH::Body& Body) const override
            {
                const BodyRecord& Record = RecordOf(Body);
                if (!Record.Enabled)
                    return false;
                return OnlyObjectId_ > 0 ? Record.ObjectId == OnlyObjectId_ : (Mask_ & Record.RayCategory) != 0;
            }

          private:
            int Mask_;
            int OnlyObjectId_;
        };

        BodyRecord* CastRay(const char* Command, float X, float Y, float Z, float ToX, float ToY, float ToZ, int Mask,
                            int OnlyObjectId, std::string* Error, bool* Failed)
        {
            *Failed = !RequireStarted(Command, Error) || !ValidateFinite({X, Y, Z, ToX, ToY, ToZ}, Command, Error);
            if (*Failed)
                return nullptr;
            LastRay.ObjectId = 0;
            const JPH::RVec3 Origin(X, Y, Z);
            const JPH::Vec3 Direction(ToX - X, ToY - Y, ToZ - Z);
            if (Direction.IsNearZero())
                return nullptr;

            // As with the plugin's Bullet rays, a ray starting inside a convex shape does not hit it, and a triangle
            // answers from both sides.
            JPH::RayCastSettings Settings;
            Settings.mTreatConvexAsSolid = false;
            Settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
            const JPH::RRayCast Ray(Origin, Direction);
            JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> Collector;
            World->GetNarrowPhaseQueryNoLock().CastRay(Ray, Settings, Collector, {}, {},
                                                       RayBodyFilter(Mask, OnlyObjectId));
            if (!Collector.HadHit())
                return nullptr;

            const JPH::BodyLockRead Lock(World->GetBodyLockInterfaceNoLock(), Collector.mHit.mBodyID);
            if (!Lock.Succeeded())
                return nullptr;
            const JPH::Body& Body = Lock.GetBody();
            const JPH::RVec3 Point = Ray.GetPointOnRay(Collector.mHit.mFraction);
            JPH::Vec3 Normal = Body.GetWorldSpaceSurfaceNormal(Collector.mHit.mSubShapeID2, Point);
            if (Normal.Dot(Direction) > 0.0f)
                Normal = -Normal;
            auto* Record = reinterpret_cast<BodyRecord*>(Body.GetUserData());
            LastRay.ObjectId = Record->ObjectId;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                LastRay.Point[Axis] = static_cast<float>(Point[Axis]);
                LastRay.Normal[Axis] = Normal[Axis];
            }
            return Record;
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
                StepCharacters();
                const JPH::EPhysicsUpdateError UpdateError =
                    World->Update(StepSeconds, 1, TempAllocator.get(), Jobs.get());
                if (UpdateError != JPH::EPhysicsUpdateError::None)
                    return Fail(Error, "ODE UPDATE exhausted a Jolt simulation buffer");
                CollectStepContacts();
                AccumulatedSeconds = std::max(AccumulatedSeconds - StepSeconds, 0.0f);
            }
            SyncObjects(AccumulatedSeconds);
            return true;
        }

        // As in the plugin, the object's X rotation tells the program whether the character stands on the ground.
        void SyncCharacter(int ObjectId, const JPH::CharacterVirtual& Character, float RemainderSeconds)
        {
            const Object3D* Object = Kernel.Objects().GetObject(ObjectId);
            if (!Object)
                return;
            const bool OnGround = Character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
            JPH::Vec3 Velocity = Character.GetLinearVelocity();
            if (OnGround)
                Velocity.SetY(Character.GetGroundVelocity().GetY());
            const JPH::RVec3 Position = Character.GetPosition() + JPH::RVec3(RemainderSeconds * Velocity);
            Kernel.Objects().PositionObject(ObjectId, static_cast<float>(Position.GetX()),
                                            static_cast<float>(Position.GetY()), static_cast<float>(Position.GetZ()));
            Kernel.Objects().RotateObject(ObjectId, OnGround ? 1.0f : 0.0f, Object->RotY, Object->RotZ);
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
                if (Record.Control)
                {
                    SyncCharacter(ObjectId, *Record.Control->Character, RemainderSeconds);
                    continue;
                }
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
        std::vector<JointRecord> Joints;
        JPH::Ref<JPH::StaticCompoundShapeSettings> PendingCompound;
        int PendingCompoundObject = 0;
        int CharacterCount = 0;
        int CharacterHitFloor = 0;
        float CharacterFallDistance = 0.0f;
        float StepSeconds = DefaultStepSeconds;
        float AccumulatedSeconds = 0.0f;
        float PositionCorrection = 0.0f;
        int VelocitySteps = 0;
        int PositionSteps = 0;
        bool ExactStepMode = false;
        bool AnyPendingForce = false;
        std::unique_ptr<PhysicsCollisionMessage[]> StepContacts;
        std::atomic<std::uint32_t> StepContactCount{0};
        std::unique_ptr<PhysicsCollisionMessage[]> CollisionMessages;
        std::uint32_t CollisionMessageHead = 0;
        std::uint32_t CollisionMessageCount = 0;
        PhysicsCollisionMessage CurrentMessage;
        bool ContactOverflowLogged = false;

        struct
        {
            int ObjectId = 0;
            float Point[3]{};
            float Normal[3]{};
        } LastRay;

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
        return Impl_->CreateStaticMesh(ObjectId, Limb, static_cast<float>(ScalingPercent), RayCategoryObject,
                                       "ODE CREATE STATIC TRIANGLE MESH", Error);
    }

    bool PhysicsSystem::CreateStaticTriangleMeshLimb(int ObjectId, int Limb, std::string* Error)
    {
        return Impl_->CreateStaticMesh(ObjectId, Limb, 100.0f, RayCategoryObject,
                                       "ODE CREATE STATIC TRIANGLE MESH LIMB", Error);
    }

    bool PhysicsSystem::CreateStaticTerrainMesh(int ObjectId, std::string* Error)
    {
        return Impl_->CreateStaticMesh(ObjectId, -1, 100.0f, RayCategoryTerrain, "ODE CREATE STATIC TERRAIN MESH",
                                       Error);
    }

    bool PhysicsSystem::CreateStaticSphere(int ObjectId, std::string* Error)
    {
        return Impl_->CreateSphere(ObjectId, false, "ODE CREATE STATIC SPHERE", Error);
    }

    bool PhysicsSystem::StartStaticObject(int ObjectId, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE START STATIC OBJECT", Error))
            return false;
        Impl_->PendingCompound = new JPH::StaticCompoundShapeSettings();
        Impl_->PendingCompoundObject = ObjectId;
        return true;
    }

    bool PhysicsSystem::AddStaticObjectBox(int ObjectId, int BoxObjectId, std::string* Error)
    {
        constexpr const char* Command = "ODE ADD STATIC OBJECT BOX";
        if (!Impl_->RequireStarted(Command, Error))
            return false;
        if (!Impl_->PendingCompound || Impl_->PendingCompoundObject != ObjectId)
            return Fail(Error, "ODE ADD STATIC OBJECT BOX requires ODE START STATIC OBJECT for object " +
                                   std::to_string(ObjectId));
        const Object3D* Box = Impl_->RequireObject(BoxObjectId, Command, Error);
        LocalBounds Bounds;
        if (!Box || !Impl_->CollisionBounds(*Box, BoxObjectId, -1, Command, Bounds, Error))
            return false;
        // As in the plugin, the box's own position and rotation count from the finished object's.
        const JPH::Quat Rotation = ToPhysicsRotation(*Box);
        Impl_->PendingCompound->AddShape(JPH::Vec3(Box->PosX, Box->PosY, Box->PosZ) + Rotation * Bounds.Center(),
                                         Rotation, MakeBoxShape(Bounds.HalfExtent()).GetPtr());
        return true;
    }

    bool PhysicsSystem::EndStaticObject(int ObjectId, int Material, std::string* Error)
    {
        constexpr const char* Command = "ODE END STATIC OBJECT";
        const Object3D* Object = Impl_->RequireObject(ObjectId, Command, Error);
        if (!Object)
            return false;
        if (!Impl_->PendingCompound || Impl_->PendingCompoundObject != ObjectId)
            return Fail(Error, "ODE END STATIC OBJECT requires ODE START STATIC OBJECT for object " +
                                   std::to_string(ObjectId));
        const JPH::Ref<JPH::StaticCompoundShapeSettings> Compound = std::move(Impl_->PendingCompound);
        Impl_->PendingCompound = nullptr;
        if (Compound->mSubShapes.empty())
            return Fail(Error, "ODE END STATIC OBJECT requires ODE ADD STATIC OBJECT BOX first");
        const JPH::ShapeSettings::ShapeResult Result = Compound->Create();
        if (Result.HasError())
            return Fail(Error, std::string(Command) + " could not build its shape: " + Result.GetError().c_str());
        Impl::BodyOptions Options = Impl::OptionsAt(*Object, false);
        Options.RayCategory = Material == 1 ? RayCategoryTerrain : RayCategoryObject;
        return Impl_->AddBody(ObjectId, *Object, Command, Result.Get(), Options, Error) != nullptr;
    }

    bool PhysicsSystem::CreateBallJoint(int JointId, int ObjectA, int ObjectB, float X, float Y, float Z,
                                        std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE BALL JOINT";
        if (!Impl_->RequireJointBodies(JointId, ObjectA, ObjectB, Command, Error) ||
            !ValidateFinite({X, Y, Z}, Command, Error))
            return false;
        JPH::PointConstraintSettings Settings;
        Settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        Settings.mPoint1 = Settings.mPoint2 = JPH::RVec3(X, Y, Z);
        return Impl_->AddJoint(JointId, true, ObjectA, ObjectB, Settings, Command, Error) != nullptr;
    }

    bool PhysicsSystem::CreateHingeJoint(int JointId, int ObjectA, int ObjectB, float AxisX, float AxisY, float AxisZ,
                                         float X, float Y, float Z, std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE HINGE JOINT";
        if (!Impl_->RequireJointBodies(JointId, ObjectA, ObjectB, Command, Error) ||
            !ValidateFinite({AxisX, AxisY, AxisZ, X, Y, Z}, Command, Error))
            return false;
        const JPH::Vec3 Axis(AxisX, AxisY, AxisZ);
        if (Axis.IsNearZero())
            return Fail(Error, "ODE CREATE HINGE JOINT requires an axis with a length");
        JPH::HingeConstraintSettings Settings;
        Settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        Settings.mPoint1 = Settings.mPoint2 = JPH::RVec3(X, Y, Z);
        Settings.mHingeAxis1 = Settings.mHingeAxis2 = Axis.Normalized();
        Settings.mNormalAxis1 = Settings.mNormalAxis2 = Settings.mHingeAxis1.GetNormalizedPerpendicular();
        Impl::JointRecord* Joint = Impl_->AddJoint(JointId, true, ObjectA, ObjectB, Settings, Command, Error);
        if (!Joint)
            return false;
        Joint->Hinge = static_cast<JPH::HingeConstraint*>(Joint->Constraint.GetPtr());
        return true;
    }

    bool PhysicsSystem::SetHingeJointLimit(int JointId, float Low, float High, std::string* Error)
    {
        constexpr const char* Command = "ODE SET HINGE JOINT LIMIT";
        if (!Impl_->RequireStarted(Command, Error) || !ValidateFinite({Low, High}, Command, Error))
            return false;
        const auto Joint =
            std::find_if(Impl_->Joints.begin(), Impl_->Joints.end(),
                         [JointId](const Impl::JointRecord& Value) { return Value.Named && Value.Id == JointId; });
        if (Joint == Impl_->Joints.end())
            return Fail(Error, "ODE SET HINGE JOINT LIMIT requires joint " + std::to_string(JointId));
        if (!Joint->Hinge)
            return Fail(Error, "ODE SET HINGE JOINT LIMIT requires a hinge, but joint " + std::to_string(JointId) +
                                   " is a ball joint");
        constexpr float Pi = std::numbers::pi_v<float>;
        if (Low < -Pi || Low > 0.0f || High < 0.0f || High > Pi)
            return Fail(Error,
                        "ODE SET HINGE JOINT LIMIT requires a low limit from -pi to 0 and a high limit from 0 to "
                        "pi, in radians from where the hinge was made");
        Joint->Hinge->SetLimits(Low, High);
        Impl_->Bodies().ActivateConstraint(Joint->Hinge);
        return true;
    }

    bool PhysicsSystem::CreateSwingDoorHinge(int& Output, int ObjectId, float X, float Y, float Z, float Angle,
                                             std::string* Error)
    {
        constexpr const char* Command = "ODE CREATE SWINGDOOR HINGE";
        Output = 0;
        if (!Impl_->RequireStarted(Command, Error) || !ValidateFinite({X, Y, Z, Angle}, Command, Error))
            return false;
        // As in the plugin, a number without a body is an answer rather than an error, and the angle does nothing.
        const auto It = Impl_->Records.find(ObjectId);
        if (It == Impl_->Records.end())
            return true;
        if (!Impl_->RequireMovingBody(ObjectId, Command, Error))
            return false;
        JPH::RVec3 Position;
        JPH::Quat Rotation;
        Impl_->Bodies().GetPositionAndRotation(It->second.Id, Position, Rotation);
        JPH::HingeConstraintSettings Settings;
        Settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        Settings.mPoint1 = Settings.mPoint2 = Position + Rotation * JPH::Vec3(X, Y, Z);
        Settings.mHingeAxis1 = Settings.mHingeAxis2 = Rotation * JPH::Vec3::sAxisY();
        Settings.mNormalAxis1 = Settings.mNormalAxis2 = Rotation * JPH::Vec3::sAxisX();
        if (!Impl_->AddJoint(0, false, ObjectId, 0, Settings, Command, Error))
            return false;
        Output = 1;
        return true;
    }

    bool PhysicsSystem::CreateCharacterController(int ObjectId, float Gravity, float FallSpeed,
                                                  float MaximumSlopeDegrees, std::string* Error)
    {
        return Impl_->CreateCharacter(ObjectId, Gravity, FallSpeed, MaximumSlopeDegrees, Error);
    }

    bool PhysicsSystem::ControlCharacterController(int ObjectId, float YDegrees, float XDegrees, float Speed,
                                                   float Jump, float Ducking, float PushDegrees, float PushForce,
                                                   float Thrust, std::string* Error)
    {
        constexpr const char* Command = "ODE CONTROL CHARACTER CONTROLLER";
        Impl::BodyRecord* Record = Impl_->RequireCharacter(ObjectId, Command, Error);
        if (!Record ||
            !ValidateFinite({YDegrees, XDegrees, Speed, Jump, Ducking, PushDegrees, PushForce, Thrust}, Command, Error))
            return false;
        Impl::CharacterRecord& Control = *Record->Control;
        const float Yaw = (PushForce > 0.0f ? PushDegrees : YDegrees) * DegreesToRadians;
        // A push replaces the walk. The plugin's walk also went down by the sine of the pitch, to keep to the floor,
        // which sticking to the floor does here.
        const float Distance = PushForce > 0.0f ? PushForce : std::cos(XDegrees * DegreesToRadians) * Speed;
        Control.WalkVelocity = CharacterTicksPerSecond * Distance * JPH::Vec3(std::sin(Yaw), 0.0f, std::cos(Yaw));
        if (Jump > 0.0f && Control.Character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround)
            Control.JumpSpeed = Jump;
        Control.ThrustSpeed = Thrust;
        Impl_->SetCharacterDucked(Control, Ducking == 1.0f);
        return true;
    }

    bool PhysicsSystem::CharacterDucking(float& Output, int ObjectId, std::string* Error)
    {
        const Impl::BodyRecord* Record =
            Impl_->RequireCharacter(ObjectId, "ODE GET CHARACTER CONTROLLER DUCKING", Error);
        if (!Record)
            return false;
        Output = Record->Control->Ducked ? 1.0f : 0.0f;
        return true;
    }

    bool PhysicsSystem::CharacterHitFloor(int& Output, std::string* Error)
    {
        if (!Impl_->RequireAnyCharacter("ODE GET CHARACTER HIT FLOOR", Error))
            return false;
        Output = Impl_->CharacterHitFloor;
        Impl_->CharacterHitFloor = 0;
        return true;
    }

    bool PhysicsSystem::CharacterFallDistance(float& Output, std::string* Error)
    {
        if (!Impl_->RequireAnyCharacter("ODE GET CHARACTER FALL DISTANCE", Error))
            return false;
        // The plugin halves what it measured, and programs are tuned to that.
        Output = 0.5f * Impl_->CharacterFallDistance;
        Impl_->CharacterFallDistance = 0.0f;
        return true;
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
        if (Record->Control)
        {
            Record->Control->Character->SetPosition(JPH::RVec3(X, Y, Z));
            Record->Control->HighestY = Y;
        }
        else
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
        // A character's capsule stays upright whatever its object shows.
        if (!Record->Control)
            Impl_->Bodies().SetRotation(Record->Id,
                                        ToPhysicsRotation(XDegrees, YDegrees, ZDegrees, Record->ZyxRotation),
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

    bool PhysicsSystem::SetContactFriction(int ObjectId, float Friction, std::string* Error)
    {
        // The plugin's export of this name sets the friction coefficient, not a friction direction.
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET CONTACT FDIR1", Error);
        if (!Record || !ValidateFinite({Friction}, "ODE SET CONTACT FDIR1", Error))
            return false;
        if (Friction < 0.0f)
            return Fail(Error, "ODE SET CONTACT FDIR1 requires a non-negative value");
        Impl_->Bodies().SetFriction(Record->Id, Friction);
        return true;
    }

    bool PhysicsSystem::SetContactBounce(int ObjectId, float Bounce, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET CONTACT BOUNCE", Error);
        if (!Record || !ValidateFinite({Bounce}, "ODE SET CONTACT BOUNCE", Error))
            return false;
        if (Bounce < 0.0f || Bounce > 1.0f)
            return Fail(Error, "ODE SET CONTACT BOUNCE requires a value between 0 and 1");
        Record->Bounce = Bounce;
        Impl_->ApplyBounce(*Record);
        return true;
    }

    bool PhysicsSystem::SetContactBounceVelocity(int ObjectId, float Velocity, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET CONTACT BOUNCE VELOCITY", Error);
        if (!Record || !ValidateFinite({Velocity}, "ODE SET CONTACT BOUNCE VELOCITY", Error))
            return false;
        if (Velocity < 0.0f)
            return Fail(Error, "ODE SET CONTACT BOUNCE VELOCITY requires a non-negative value");
        Record->BounceVelocity = Velocity;
        return true;
    }

    bool PhysicsSystem::SetContactVelocity(int ObjectId, float Velocity, std::string* Error)
    {
        // The plugin exports this name with no definition behind it; the closest meaning it has is the bounce velocity.
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SET CONTACT VELOCITY", Error);
        if (!Record || !ValidateFinite({Velocity}, "ODE SET CONTACT VELOCITY", Error))
            return false;
        if (Velocity < 0.0f)
            return Fail(Error, "ODE SET CONTACT VELOCITY requires a non-negative value");
        Record->BounceVelocity = Velocity;
        return true;
    }

    bool PhysicsSystem::SetContact(int ObjectId, int Mode, int Value, std::string* Error)
    {
        switch (Mode)
        {
            case 0:
                return SetContactFriction(ObjectId, static_cast<float>(Value), Error);
            case 2:
                return SetContactBounce(ObjectId, static_cast<float>(Value), Error);
            case 3:
                return SetContactBounceVelocity(ObjectId, static_cast<float>(Value), Error);
            default:
                return Fail(Error,
                            "ODE SET CONTACT mode " + std::to_string(Mode) +
                                " has no Jolt counterpart; modes 0 (friction), 2 (bounce) and 3 (bounce velocity) do");
        }
    }

    bool PhysicsSystem::SetSurfaceBounce(int ObjectId, int State, std::string* Error)
    {
        Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE SETSURFACE MODE CONTACT BOUNCE", Error);
        if (!Record)
            return false;
        Record->BounceEnabled = State != 0;
        Impl_->ApplyBounce(*Record);
        return true;
    }

    bool PhysicsSystem::SetSurfaceMode(int ObjectId, int Mode, int State, std::string* Error)
    {
        if (Mode != 2)
            return Fail(Error, "ODE SETSURFACE MODE mode " + std::to_string(Mode) +
                                   " has no Jolt counterpart; mode 2 (bounce) does");
        return SetSurfaceBounce(ObjectId, State, Error);
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

    bool PhysicsSystem::CollisionMessageExists(int& Output, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE COLLISION MESSAGE EXISTS", Error))
            return false;
        Impl_->CurrentMessage = {};
        Output = Impl_->CollisionMessageCount > 0 ? 1 : 0;
        return true;
    }

    bool PhysicsSystem::NextCollisionMessage(std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE COLLISION GET MESSAGE", Error))
            return false;
        Impl_->CurrentMessage = {};
        if (Impl_->CollisionMessageCount == 0)
            return true;
        Impl_->CollisionMessageHead =
            (Impl_->CollisionMessageHead + MaximumCollisionMessages - 1) % MaximumCollisionMessages;
        --Impl_->CollisionMessageCount;
        Impl_->CurrentMessage = Impl_->CollisionMessages[Impl_->CollisionMessageHead];
        return true;
    }

    const PhysicsCollisionMessage* PhysicsSystem::CollisionMessage(std::string* Error) const
    {
        if (!Impl_->Started)
        {
            Fail(Error, "Reading an ODE collision message requires ODE START");
            return nullptr;
        }
        return &Impl_->CurrentMessage;
    }

    bool PhysicsSystem::RayCast(int& Output, int ObjectId, float X, float Y, float Z, float ToX, float ToY, float ToZ,
                                std::string* Error)
    {
        bool Failed = false;
        // As in the plugin, a number without a body is a ray that hits nothing.
        const bool Hit =
            Impl_->CastRay("ODE RAY CAST", X, Y, Z, ToX, ToY, ToZ, RayCategoryAny, ObjectId, Error, &Failed);
        Output = Hit ? 1 : 0;
        return !Failed;
    }

    bool PhysicsSystem::Ray(int& Output, float X, float Y, float Z, float ToX, float ToY, float ToZ, int Mask,
                            std::string* Error)
    {
        bool Failed = false;
        const bool Hit = Impl_->CastRay("ODE RAY", X, Y, Z, ToX, ToY, ToZ, Mask, 0, Error, &Failed);
        Output = Hit ? 1 : 0;
        return !Failed;
    }

    bool PhysicsSystem::RayTerrain(int& Output, float X, float Y, float Z, float ToX, float ToY, float ToZ,
                                   std::string* Error)
    {
        bool Failed = false;
        const bool Hit =
            Impl_->CastRay("ODE RAY TERRAIN", X, Y, Z, ToX, ToY, ToZ, RayCategoryTerrain, 0, Error, &Failed);
        Output = Hit ? 1 : 0;
        return !Failed;
    }

    bool PhysicsSystem::RayForce(int& Output, float X, float Y, float Z, float ToX, float ToY, float ToZ, float Force,
                                 std::string* Error)
    {
        constexpr const char* Command = "ODE RAY FORCE";
        if (!ValidateFinite({Force}, Command, Error))
            return false;
        bool Failed = false;
        Impl::BodyRecord* Record = Impl_->CastRay(Command, X, Y, Z, ToX, ToY, ToZ, RayCategoryAny, 0, Error, &Failed);
        Output = Record ? 1 : 0;
        if (Failed)
            return false;
        if (Record && Record->Moving)
        {
            const JPH::Vec3 Push = Force * JPH::Vec3(ToX - X, ToY - Y, ToZ - Z).Normalized();
            const JPH::RVec3 Center = Impl_->Bodies().GetCenterOfMassPosition(Record->Id);
            const JPH::Vec3 Lever = JPH::Vec3(
                JPH::RVec3(Impl_->LastRay.Point[0], Impl_->LastRay.Point[1], Impl_->LastRay.Point[2]) - Center);
            Impl_->AddPendingForce(*Record, Push, Lever.Cross(Push));
        }
        return true;
    }

    bool PhysicsSystem::RayObjectHit(int& Output, std::string* Error) const
    {
        if (!Impl_->RequireStarted("ODE GET RAY OBJECT HIT", Error))
            return false;
        Output = Impl_->LastRay.ObjectId;
        return true;
    }

    bool PhysicsSystem::RayCollision(float& Output, int Axis, std::string* Error) const
    {
        static constexpr const char* Commands[] = {"ODE GET RAY COLLISION X", "ODE GET RAY COLLISION Y",
                                                   "ODE GET RAY COLLISION Z"};
        if (!Impl_->RequireStarted(Commands[Axis], Error))
            return false;
        Output = Impl_->LastRay.Point[Axis];
        return true;
    }

    bool PhysicsSystem::RayNormal(float& Output, int Axis, std::string* Error) const
    {
        static constexpr const char* Commands[] = {"ODE GET RAY NORMAL X", "ODE GET RAY NORMAL Y",
                                                   "ODE GET RAY NORMAL Z"};
        if (!Impl_->RequireStarted(Commands[Axis], Error))
            return false;
        Output = Impl_->LastRay.Normal[Axis];
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
        Output = Record->Control ? Record->Control->Character->GetLinearVelocity()[Axis]
                                 : Impl_->Bodies().GetLinearVelocity(Record->Id)[Axis];
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
