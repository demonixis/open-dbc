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
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
        constexpr float MaximumManualUpdateSeconds = 1.0f;
        constexpr float MaximumFrameUpdateSeconds = 0.1f;
        // Absorbs float rounding, so N updates of one step run exactly N steps.
        constexpr float StepToleranceSeconds = 1.0e-5f;

        constexpr float MinimumShapeExtent = 0.01f;
        constexpr float DefaultMass = 1.0f;
        constexpr float DefaultFriction = 0.9f;
        constexpr float DefaultLinearDamping = 0.001f;
        constexpr float DefaultAngularDamping = 0.005f;

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

            [[nodiscard]] JPH::Vec3 HalfExtent() const
            {
                return JPH::Vec3::sMax(0.5f * (Max - Min), JPH::Vec3::sReplicate(MinimumShapeExtent));
            }
        };

        // Jolt requires every half extent of a box to reach its convex radius.
        JPH::RefConst<JPH::Shape> MakeBoxShape(JPH::Vec3Arg HalfExtent)
        {
            return new JPH::BoxShape(HalfExtent, std::min(JPH::cDefaultConvexRadius, HalfExtent.ReduceMin()));
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
            JPH::BodyID Id;
            bool Moving = false;
            bool Enabled = true;
            bool ZyxRotation = false;
        };

        struct BodyOptions
        {
            bool Dynamic = false;
            float Mass = DefaultMass;
            float Friction = DefaultFriction;
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

        bool ObjectBounds(const Object3D& Object, int ObjectId, const char* Command, LocalBounds& Output,
                          std::string* Error)
        {
            bool Found = false;
            std::vector<Vec3> Vertices;
            const Vec3 Scale{Object.ScaleX, Object.ScaleY, Object.ScaleZ};
            JPH::Vec3 Min = JPH::Vec3::sReplicate(FLT_MAX);
            JPH::Vec3 Max = JPH::Vec3::sReplicate(-FLT_MAX);
            for (int Pass = 0; Pass < 2; ++Pass)
            {
                // The first pass finds the box whose centre the second pass measures the radius from.
                const JPH::Vec3 Center = 0.5f * (Min + Max);
                for (const Mesh& MeshValue : Object.Meshes)
                {
                    Kernel.Renderer().ObjectMeshVertices(Object, MeshValue, Vertices);
                    for (const Vec3& Local : Vertices)
                    {
                        Vec3 Point{Local.X * Scale.X, Local.Y * Scale.Y, Local.Z * Scale.Z};
                        if (Object.ApplyPivot)
                            Point = Mat3Apply(Point, Object.PivotMatrix);
                        const JPH::Vec3 Position(Point.X, Point.Y, Point.Z);
                        if (Pass == 0)
                        {
                            Min = JPH::Vec3::sMin(Min, Position);
                            Max = JPH::Vec3::sMax(Max, Position);
                            Found = true;
                        }
                        else
                        {
                            Output.Radius = std::max(Output.Radius, (Position - Center).Length());
                        }
                    }
                }
                if (!Found)
                    return Fail(Error, std::string(Command) + " requires object " + std::to_string(ObjectId) +
                                           " to have mesh geometry");
            }
            Output.Min = Min;
            Output.Max = Max;
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

        bool AddBody(int ObjectId, const Object3D& Object, const char* Command, JPH::RefConst<JPH::Shape> Shape,
                     const BodyOptions& Options, std::string* Error)
        {
            if (const auto Existing = Records.find(ObjectId); Existing != Records.end())
                DestroyRecord(Existing);

            JPH::BodyCreationSettings Settings(Shape.GetPtr(), JPH::RVec3(Object.PosX, Object.PosY, Object.PosZ),
                                               ToPhysicsRotation(Object),
                                               Options.Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                               Options.Dynamic ? MovingLayer : NonMovingLayer);
            Settings.mFriction = Options.Friction;
            Settings.mLinearDamping = DefaultLinearDamping;
            Settings.mAngularDamping = DefaultAngularDamping;
            if (Options.Dynamic)
            {
                // Mass does not follow the volume, so a force means the same on bodies of any size.
                Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                Settings.mMassPropertiesOverride.mMass = Options.Mass;
                Settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
            }
            const JPH::BodyID BodyId = Bodies().CreateAndAddBody(
                Settings, Options.Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (BodyId.IsInvalid())
                return Fail(Error, std::string(Command) + " exhausted the physics body pool");
            Records.emplace(ObjectId, BodyRecord{BodyId, Options.Dynamic, true, Object.UseZyxRotation});
            return true;
        }

        bool CreateSphere(int ObjectId, bool Dynamic, const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !ObjectBounds(*Object, ObjectId, Command, Bounds, Error))
                return false;
            BodyOptions Options;
            Options.Dynamic = Dynamic;
            return AddBody(
                ObjectId, *Object, Command,
                OffsetShape(new JPH::SphereShape(std::max(Bounds.Radius, MinimumShapeExtent)), Bounds.Center()),
                Options, Error);
        }

        bool CreateBox(int ObjectId, bool Dynamic, const char* Command, std::string* Error)
        {
            const Object3D* Object = RequireObject(ObjectId, Command, Error);
            LocalBounds Bounds;
            if (!Object || !ObjectBounds(*Object, ObjectId, Command, Bounds, Error))
                return false;
            BodyOptions Options;
            Options.Dynamic = Dynamic;
            return AddBody(ObjectId, *Object, Command, OffsetShape(MakeBoxShape(Bounds.HalfExtent()), Bounds.Center()),
                           Options, Error);
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
                const JPH::Vec3 Angles = ToObjectDegrees(Rotation, Record.ZyxRotation);
                Kernel.Objects().PositionObject(ObjectId, static_cast<float>(Position.GetX()),
                                                static_cast<float>(Position.GetY()),
                                                static_cast<float>(Position.GetZ()));
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
        return Impl_->CreateBox(ObjectId, false, "ODE CREATE STATIC BOX", Error);
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

    bool PhysicsSystem::LinearVelocityX(float& Output, int ObjectId, std::string* Error) const
    {
        const Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE GET BODY LINEAR VELOCITY X", Error);
        if (!Record)
            return false;
        Output = Impl_->Bodies().GetLinearVelocity(Record->Id).GetX();
        return true;
    }

    bool PhysicsSystem::LinearVelocityY(float& Output, int ObjectId, std::string* Error) const
    {
        const Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE GET BODY LINEAR VELOCITY Y", Error);
        if (!Record)
            return false;
        Output = Impl_->Bodies().GetLinearVelocity(Record->Id).GetY();
        return true;
    }

    bool PhysicsSystem::LinearVelocityZ(float& Output, int ObjectId, std::string* Error) const
    {
        const Impl::BodyRecord* Record = Impl_->RequireBody(ObjectId, "ODE GET BODY LINEAR VELOCITY Z", Error);
        if (!Record)
            return false;
        Output = Impl_->Bodies().GetLinearVelocity(Record->Id).GetZ();
        return true;
    }
} // namespace dbc
