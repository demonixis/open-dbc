#include "physics_system.hpp"

#include "engine_kernel.hpp"
#include "object_manager.hpp"

// Jolt requires its configuration header before all other Jolt headers.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <unordered_map>

namespace dbc
{
    namespace
    {
        constexpr JPH::ObjectLayer NonMovingLayer = 0;
        constexpr JPH::ObjectLayer MovingLayer = 1;
        constexpr JPH::ObjectLayer DisabledLayer = 2;
        constexpr float MinimumShapeExtent = 0.01f;

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

        JPH::Quat ToPhysicsRotation(float XDegrees, float YDegrees, float ZDegrees)
        {
            constexpr float ToRadians = std::numbers::pi_v<float> / 180.0f;
            return JPH::Quat::sEulerAngles(JPH::Vec3(XDegrees * ToRadians, YDegrees * ToRadians, ZDegrees * ToRadians));
        }

        float ToDegrees(float Radians)
        {
            return Radians * 180.0f / std::numbers::pi_v<float>;
        }

        bool ValidateFinite(float Value, const char* Command, std::string* Error = nullptr)
        {
            if (!std::isfinite(Value))
            {
                if (Error)
                    *Error = std::string(Command) + " requires finite values";
                return false;
            }
            return true;
        }
    } // namespace

    struct PhysicsSystem::Impl
    {
        struct BodyRecord
        {
            JPH::BodyID Id;
            bool Dynamic = false;
            bool Enabled = true;
        };

        explicit Impl(EngineKernel& Kernel) : Kernel(Kernel) {}

        void Start()
        {
            static_cast<void>(Runtime());
            TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
            Jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);
            World = std::make_unique<JPH::PhysicsSystem>();
            World->Init(65536, 0, 65536, 10240, BroadPhase, ObjectVsBroadPhaseFilter, ObjectLayerPairFilter);
            World->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
            Started = true;
        }

        bool RequireStarted(const char* Command, std::string* Error = nullptr) const
        {
            if (!Started)
            {
                if (Error)
                    *Error = std::string(Command) + " requires ODE START";
                return false;
            }
            return true;
        }

        BodyRecord* RequireBody(int ObjectId, const char* Command, std::string* Error = nullptr)
        {
            if (!RequireStarted(Command, Error))
                return nullptr;
            const auto It = Bodies.find(ObjectId);
            if (It == Bodies.end())
            {
                if (Error)
                    *Error = std::string(Command) + " requires an ODE body for object " + std::to_string(ObjectId);
                return nullptr;
            }
            return &It->second;
        }

        const BodyRecord* RequireBody(int ObjectId, const char* Command, std::string* Error = nullptr) const
        {
            return const_cast<Impl*>(this)->RequireBody(ObjectId, Command, Error);
        }

        void Destroy(BodyRecord& Record)
        {
            JPH::BodyInterface& Interface = World->GetBodyInterface();
            if (Interface.IsAdded(Record.Id))
                Interface.RemoveBody(Record.Id);
            Interface.DestroyBody(Record.Id);
        }

        void DestroyAll()
        {
            for (auto& [ObjectId, Record] : Bodies)
            {
                static_cast<void>(ObjectId);
                Destroy(Record);
            }
            Bodies.clear();
        }

        bool AddBody(int ObjectId, JPH::RefConst<JPH::Shape> ShapeValue, bool Dynamic, std::string* Error = nullptr)
        {
            if (!RequireStarted(Dynamic ? "ODE CREATE DYNAMIC SPHERE" : "ODE CREATE STATIC SHAPE", Error))
                return false;
            const Object3D* Object = Kernel.Objects().GetObject(ObjectId);
            if (!Object)
            {
                if (Error)
                    *Error = "ODE body creation requires object " + std::to_string(ObjectId);
                return false;
            }

            if (const auto Existing = Bodies.find(ObjectId); Existing != Bodies.end())
            {
                Destroy(Existing->second);
                Bodies.erase(Existing);
            }

            JPH::BodyCreationSettings Settings(
                ShapeValue.GetPtr(), JPH::RVec3(Object->PosX, Object->PosY, Object->PosZ),
                ToPhysicsRotation(Object->RotX, Object->RotY, Object->RotZ),
                Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, Dynamic ? MovingLayer : NonMovingLayer);
            Settings.mUserData = static_cast<JPH::uint64>(static_cast<std::uint32_t>(ObjectId));
            const JPH::BodyID BodyId = World->GetBodyInterface().CreateAndAddBody(
                Settings, Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (BodyId.IsInvalid())
            {
                if (Error)
                    *Error = "ODE body creation exhausted the physics body pool";
                return false;
            }
            Bodies.emplace(ObjectId, BodyRecord{BodyId, Dynamic, true});
            return true;
        }

        void SyncObjects()
        {
            const JPH::BodyInterface& Interface = World->GetBodyInterface();
            for (const auto& [ObjectId, Record] : Bodies)
            {
                if (!Record.Dynamic || !Kernel.Objects().GetObject(ObjectId))
                    continue;
                JPH::RVec3 Position;
                JPH::Quat Rotation;
                Interface.GetPositionAndRotation(Record.Id, Position, Rotation);
                const JPH::Vec3 Angles = Rotation.GetEulerAngles();
                Kernel.Objects().PositionObject(ObjectId, static_cast<float>(Position.GetX()),
                                                static_cast<float>(Position.GetY()),
                                                static_cast<float>(Position.GetZ()));
                Kernel.Objects().RotateObject(ObjectId, ToDegrees(Angles.GetX()), ToDegrees(Angles.GetY()),
                                              ToDegrees(Angles.GetZ()));
            }
        }

        EngineKernel& Kernel;
        BroadPhaseLayers BroadPhase;
        ObjectVsBroadPhase ObjectVsBroadPhaseFilter;
        ObjectLayerPairs ObjectLayerPairFilter;
        std::unique_ptr<JPH::PhysicsSystem> World;
        std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> Jobs;
        std::unordered_map<int, BodyRecord> Bodies;
        bool Started = false;
    };

    PhysicsSystem::PhysicsSystem(EngineKernel& Kernel) : Kernel_(Kernel), Impl_(std::make_unique<Impl>(Kernel)) {}

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
        if (!Impl_->World)
            return;
        Impl_->DestroyAll();
        Impl_->Started = false;
        Impl_->World.reset();
        Impl_->Jobs.reset();
        Impl_->TempAllocator.reset();
    }

    bool PhysicsSystem::IsStarted() const
    {
        return Impl_->Started;
    }

    bool PhysicsSystem::Update(float Seconds, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE UPDATE", Error))
            return false;
        if (!ValidateFinite(Seconds, "ODE UPDATE", Error))
            return false;
        if (Seconds <= 0.0f)
        {
            if (Error)
                *Error = "ODE UPDATE step must be greater than zero";
            return false;
        }
        const int CollisionSteps =
            std::clamp(static_cast<int>(std::ceil(std::clamp(Seconds, 1.0f / 60.0f, 16.0f / 60.0f) * 60.0f)), 1, 16);
        const JPH::EPhysicsUpdateError UpdateError =
            Impl_->World->Update(Seconds, CollisionSteps, Impl_->TempAllocator.get(), Impl_->Jobs.get());
        if (UpdateError != JPH::EPhysicsUpdateError::None)
        {
            if (Error)
                *Error = "ODE UPDATE exhausted a Jolt simulation buffer";
            return false;
        }
        Impl_->SyncObjects();
        return true;
    }

    bool PhysicsSystem::CreateDynamicSphere(int ObjectId, std::string* Error)
    {
        const float Radius = std::max(Kernel_.Objects().ObjectSize(ObjectId, 1), MinimumShapeExtent);
        if (!Impl_->AddBody(ObjectId, new JPH::SphereShape(Radius), true, Error))
            return false;
        return true;
    }

    bool PhysicsSystem::CreateStaticBox(int ObjectId, std::string* Error)
    {
        const JPH::Vec3 HalfExtent(std::max(Kernel_.Objects().ObjectSizeX(ObjectId, 1) * 0.5f, MinimumShapeExtent),
                                   std::max(Kernel_.Objects().ObjectSizeY(ObjectId, 1) * 0.5f, MinimumShapeExtent),
                                   std::max(Kernel_.Objects().ObjectSizeZ(ObjectId, 1) * 0.5f, MinimumShapeExtent));
        if (!Impl_->AddBody(ObjectId, new JPH::BoxShape(HalfExtent), false, Error))
            return false;
        return true;
    }

    bool PhysicsSystem::CreateStaticSphere(int ObjectId, std::string* Error)
    {
        const float Radius = std::max(Kernel_.Objects().ObjectSize(ObjectId, 1), MinimumShapeExtent);
        if (!Impl_->AddBody(ObjectId, new JPH::SphereShape(Radius), false, Error))
            return false;
        return true;
    }

    bool PhysicsSystem::DestroyObject(int ObjectId, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE DESTROY OBJECT", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        Impl_->Destroy(Record);
        Impl_->Bodies.erase(ObjectId);
        return true;
    }

    bool PhysicsSystem::SetWorldGravity(float X, float Y, float Z, std::string* Error)
    {
        if (!Impl_->RequireStarted("ODE SET WORLD GRAVITY", Error))
            return false;
        if (!ValidateFinite(X, "ODE SET WORLD GRAVITY", Error))
            return false;
        if (!ValidateFinite(Y, "ODE SET WORLD GRAVITY", Error))
            return false;
        if (!ValidateFinite(Z, "ODE SET WORLD GRAVITY", Error))
            return false;
        Impl_->World->SetGravity(JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::SetBodyPosition(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET BODY POSITION", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        if (!ValidateFinite(X, "ODE SET BODY POSITION", Error))
            return false;
        if (!ValidateFinite(Y, "ODE SET BODY POSITION", Error))
            return false;
        if (!ValidateFinite(Z, "ODE SET BODY POSITION", Error))
            return false;
        JPH::BodyInterface& Interface = Impl_->World->GetBodyInterface();
        Interface.SetPosition(Record.Id, JPH::RVec3(X, Y, Z), JPH::EActivation::Activate);
        Kernel_.Objects().PositionObject(ObjectId, X, Y, Z);
        return true;
    }

    bool PhysicsSystem::SetBodyRotation(int ObjectId, float XDegrees, float YDegrees, float ZDegrees,
                                        std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET BODY ROTATION", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        if (!ValidateFinite(XDegrees, "ODE SET BODY ROTATION", Error))
            return false;
        if (!ValidateFinite(YDegrees, "ODE SET BODY ROTATION", Error))
            return false;
        if (!ValidateFinite(ZDegrees, "ODE SET BODY ROTATION", Error))
            return false;
        Impl_->World->GetBodyInterface().SetRotation(Record.Id, ToPhysicsRotation(XDegrees, YDegrees, ZDegrees),
                                                     JPH::EActivation::Activate);
        Kernel_.Objects().RotateObject(ObjectId, XDegrees, YDegrees, ZDegrees);
        return true;
    }

    bool PhysicsSystem::SetLinearVelocity(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET LINEAR VELOCITY", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        if (!ValidateFinite(X, "ODE SET LINEAR VELOCITY", Error))
            return false;
        if (!ValidateFinite(Y, "ODE SET LINEAR VELOCITY", Error))
            return false;
        if (!ValidateFinite(Z, "ODE SET LINEAR VELOCITY", Error))
            return false;
        Impl_->World->GetBodyInterface().SetLinearVelocity(Record.Id, JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::SetAngularVelocity(int ObjectId, float X, float Y, float Z, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET ANGULAR VELOCITY", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        if (!ValidateFinite(X, "ODE SET ANGULAR VELOCITY", Error))
            return false;
        if (!ValidateFinite(Y, "ODE SET ANGULAR VELOCITY", Error))
            return false;
        if (!ValidateFinite(Z, "ODE SET ANGULAR VELOCITY", Error))
            return false;
        Impl_->World->GetBodyInterface().SetAngularVelocity(Record.Id, JPH::Vec3(X, Y, Z));
        return true;
    }

    bool PhysicsSystem::SetBodyFriction(int ObjectId, float Friction, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET BODY FRICTION", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        if (!ValidateFinite(Friction, "ODE SET BODY FRICTION", Error))
            return false;
        if (Friction < 0.0f)
        {
            if (Error)
                *Error = "ODE SET BODY FRICTION requires a non-negative value";
            return false;
        }
        Impl_->World->GetBodyInterface().SetFriction(Record.Id, Friction);
        return true;
    }

    bool PhysicsSystem::SetGravity(int ObjectId, int Enabled, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET GRAVITY", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        Impl_->World->GetBodyInterface().SetGravityFactor(Record.Id, Enabled == 0 ? 0.0f : 1.0f);
        return true;
    }

    bool PhysicsSystem::SetEnabled(int ObjectId, int Enabled, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET ENABLED", Error);
        if (!RecordPointer)
            return false;
        Impl::BodyRecord& Record = *RecordPointer;
        JPH::BodyInterface& Interface = Impl_->World->GetBodyInterface();
        const bool Enable = Enabled != 0;
        if (Enable == Record.Enabled)
            return true;
        Interface.SetObjectLayer(Record.Id, Enable ? (Record.Dynamic ? MovingLayer : NonMovingLayer) : DisabledLayer);
        Record.Enabled = Enable;
        return true;
    }

    bool PhysicsSystem::SetActive(int ObjectId, int Active, std::string* Error)
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE SET ACTIVE", Error);
        if (!RecordPointer)
            return false;
        const Impl::BodyRecord& Record = *RecordPointer;
        JPH::BodyInterface& Interface = Impl_->World->GetBodyInterface();
        if (!Interface.IsAdded(Record.Id))
        {
            if (Error)
                *Error = "ODE SET ACTIVE requires an enabled body";
            return false;
        }
        if (Active != 0)
            Interface.ActivateBody(Record.Id);
        else
            Interface.DeactivateBody(Record.Id);
        return true;
    }

    bool PhysicsSystem::BodyHeight(float& Output, int ObjectId, std::string* Error) const
    {
        if (!Impl_->RequireBody(ObjectId, "ODE GET BODY HEIGHT", Error))
            return false;
        {
            Output = Kernel_.Objects().ObjectSizeY(ObjectId, 1);
            return true;
        }
    }

    bool PhysicsSystem::LinearVelocityX(float& Output, int ObjectId, std::string* Error) const
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE GET BODY LINEAR VELOCITY X", Error);
        if (!RecordPointer)
            return false;
        const Impl::BodyRecord& Record = *RecordPointer;
        {
            Output = Impl_->World->GetBodyInterface().GetLinearVelocity(Record.Id).GetX();
            return true;
        }
    }

    bool PhysicsSystem::LinearVelocityY(float& Output, int ObjectId, std::string* Error) const
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE GET BODY LINEAR VELOCITY Y", Error);
        if (!RecordPointer)
            return false;
        const Impl::BodyRecord& Record = *RecordPointer;
        {
            Output = Impl_->World->GetBodyInterface().GetLinearVelocity(Record.Id).GetY();
            return true;
        }
    }

    bool PhysicsSystem::LinearVelocityZ(float& Output, int ObjectId, std::string* Error) const
    {
        auto* RecordPointer = Impl_->RequireBody(ObjectId, "ODE GET BODY LINEAR VELOCITY Z", Error);
        if (!RecordPointer)
            return false;
        const Impl::BodyRecord& Record = *RecordPointer;
        {
            Output = Impl_->World->GetBodyInterface().GetLinearVelocity(Record.Id).GetZ();
            return true;
        }
    }
} // namespace dbc
