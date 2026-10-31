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
        bool CreateStaticSphere(int ObjectId, std::string* Error = nullptr);
        bool DestroyObject(int ObjectId, std::string* Error = nullptr);

        bool SetWorldGravity(float X, float Y, float Z, std::string* Error = nullptr);
        bool SetBodyPosition(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetBodyRotation(int ObjectId, float XDegrees, float YDegrees, float ZDegrees,
                             std::string* Error = nullptr);
        bool SetLinearVelocity(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetAngularVelocity(int ObjectId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetBodyFriction(int ObjectId, float Friction, std::string* Error = nullptr);
        bool SetGravity(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetEnabled(int ObjectId, int Enabled, std::string* Error = nullptr);
        bool SetActive(int ObjectId, int Active, std::string* Error = nullptr);

        [[nodiscard]] bool BodyHeight(float& Output, int ObjectId, std::string* Error = nullptr) const;
        [[nodiscard]] bool LinearVelocityX(float& Output, int ObjectId, std::string* Error = nullptr) const;
        [[nodiscard]] bool LinearVelocityY(float& Output, int ObjectId, std::string* Error = nullptr) const;
        [[nodiscard]] bool LinearVelocityZ(float& Output, int ObjectId, std::string* Error = nullptr) const;

      private:
        struct Impl;

        std::unique_ptr<Impl> Impl_;
    };
} // namespace dbc
