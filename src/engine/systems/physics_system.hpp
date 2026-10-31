#pragma once

#include <memory>
#include <string>

namespace dbc
{
    class EngineKernel;

    class PhysicsSystem
    {
      public:
        explicit PhysicsSystem(EngineKernel& Kernel);
        ~PhysicsSystem();

        PhysicsSystem(const PhysicsSystem&) = delete;
        PhysicsSystem& operator=(const PhysicsSystem&) = delete;

        void Start();
        void End();
        [[nodiscard]] bool IsStarted() const;

        // Without a duration: the real time since the last update if a frame was presented since, otherwise 1/60 s.
        bool Update(std::string* Error = nullptr);
        bool Update(float Seconds, std::string* Error = nullptr);

        // Object deletion does not go through this system, so the DELETE OBJECT forms report it here.
        void ObjectDeleted(int ObjectId);
        void ObjectsDeleted(int FirstObjectId, int LastObjectId);

        bool CreateDynamicSphere(int ObjectId, std::string* Error = nullptr);
        bool CreateStaticBox(int ObjectId, std::string* Error = nullptr);
        bool CreateStaticBoxLimb(int ObjectId, int Limb, std::string* Error = nullptr);
        bool CreateStaticSphere(int ObjectId, std::string* Error = nullptr);
        bool CreateDynamicBox(int ObjectId, std::string* Error = nullptr);
        // As in the plugin, weight is a percentage of the default mass, friction counts 50 per unit and restitution
        // 100 per unit, with -1 for the default of each.
        bool CreateDynamicBox(int ObjectId, int Limb, int TipOverMode, float Weight, float Friction, float Restitution,
                              std::string* Error = nullptr);
        bool CreateDynamicBoxSized(int ObjectId, float XSize, float YSize, float ZSize, std::string* Error = nullptr);
        bool CreateDynamicBoxManual(int ObjectId, float X, float Y, float Z, float XSize, float YSize, float ZSize,
                                    float XDegrees, float YDegrees, float ZDegrees, std::string* Error = nullptr);
        bool CreateDynamicCylinder(int ObjectId, std::string* Error = nullptr);
        bool CreateStaticCylinder(int ObjectId, float X, float Y, float Z, float XSize, float YSize, float ZSize,
                                  float XRadians, float YRadians, float ZRadians, std::string* Error = nullptr);
        bool CreateDynamicCapsule(int ObjectId, float ScaleModifier, float Raised, std::string* Error = nullptr);
        bool CreateStaticCapsule(int ObjectId, std::string* Error = nullptr);
        bool CreateDynamicTriangleMesh(int ObjectId, std::string* Error = nullptr);
        bool CreateStaticTriangleMesh(int ObjectId, int Limb, int ScalingPercent, std::string* Error = nullptr);
        bool CreateStaticTriangleMeshLimb(int ObjectId, int Limb, std::string* Error = nullptr);
        bool CreateStaticTerrainMesh(int ObjectId, std::string* Error = nullptr);
        bool DestroyObject(int ObjectId, std::string* Error = nullptr);

        bool SetWorldGravity(float X, float Y, float Z, std::string* Error = nullptr);
        bool SetWorldStep(float Seconds, std::string* Error = nullptr);
        bool SetWorldErp(float Value, std::string* Error = nullptr);
        bool SetWorldQuickStepIterations(int Iterations, std::string* Error = nullptr);
        bool SetStepMode(int Mode, std::string* Error = nullptr);
        bool SetBodyPosition(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetBodyRotation(int ObjectId, float XDegrees, float YDegrees, float ZDegrees,
                             std::string* Error = nullptr);
        bool SetLinearVelocity(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetAngularVelocity(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetBodyFriction(int ObjectId, float Friction, std::string* Error = nullptr);
        bool SetBodyMass(int ObjectId, float Mass, std::string* Error = nullptr);
        bool SetBodyDamping(int ObjectId, float Linear, float Angular, std::string* Error = nullptr);
        bool SetBodyKinematic(int ObjectId, int Kinematic, std::string* Error = nullptr);
        bool SetResponse(int ObjectId, int Mode, std::string* Error = nullptr);
        bool SetRayCast(int ObjectId, int Mode, std::string* Error = nullptr);
        bool SetGravity(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetEnabled(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetActive(int ObjectId, int Active, std::string* Error = nullptr);

        // A force lasts for the next update. Its point is relative to the body's centre, in world axes.
        bool AddForce(int ObjectId, float X, float Y, float Z, float PointX, float PointY, float PointZ,
                      std::string* Error = nullptr);
        bool AddTorque(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool AddImpulse(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);

        [[nodiscard]] bool BodyHeight(float& Output, int ObjectId, std::string* Error = nullptr) const;
        [[nodiscard]] bool LinearVelocity(float& Output, int ObjectId, int Axis, std::string* Error = nullptr) const;
        [[nodiscard]] bool AngularVelocity(float& Output, int ObjectId, int Axis, std::string* Error = nullptr) const;

      private:
        struct Impl;

        std::unique_ptr<Impl> Impl_;
    };
} // namespace dbc
