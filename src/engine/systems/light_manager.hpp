#pragma once

#include <unordered_map>

#include "../engine_types.hpp"

namespace dbc
{
    class EngineKernel;

    class LightManager
    {
      public:
        explicit LightManager(EngineKernel& Kernel);
        ~LightManager();

        // Ambient
        void SetAmbientLight(int Value);
        void ColorAmbientLight(int RgbPacked);
        float AmbientLight() const;
        int AmbientColor() const;

        // Light CRUD
        bool MakeLight(int LightId, std::string* Error = nullptr);
        bool DeleteLight(int LightId, std::string* Error = nullptr);
        bool ShowLight(int LightId, std::string* Error = nullptr);
        bool HideLight(int LightId, std::string* Error = nullptr);
        int LightExist(int LightId) const;
        int LightVisible(int LightId) const;
        int LightType(int LightId) const;
        int MaximumLights() const
        {
            return 19;
        }

        // Light properties
        bool ColorLight(int LightId, int RgbPacked, std::string* Error = nullptr);
        bool PositionLight(int LightId, float X, float Y, float Z, std::string* Error = nullptr);
        bool RotateLight(int LightId, float XDegrees, float YDegrees, float ZDegrees, std::string* Error = nullptr);
        bool PointLight(int LightId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetDirectionalLight(int LightId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetPointLight(int LightId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetSpotLight(int LightId, float InnerAngle, float OuterAngle, std::string* Error = nullptr);
        bool SetLightRange(int LightId, float Range, std::string* Error = nullptr);

        // Light getters
        float LightPositionX(int LightId) const;
        float LightPositionY(int LightId) const;
        float LightPositionZ(int LightId) const;
        float LightDirectionX(int LightId) const;
        float LightDirectionY(int LightId) const;
        float LightDirectionZ(int LightId) const;
        float LightRotationX(int LightId) const;
        float LightRotationY(int LightId) const;
        float LightRotationZ(int LightId) const;
        float LightRange(int LightId) const;

        // Cross-subsystem helpers
        bool SetLightToObjectPosition(int LightId, int ObjectId, std::string* Error = nullptr);
        bool SetLightToObjectOrientation(int LightId, int ObjectId, std::string* Error = nullptr);

        // Renderer access
        Light3D& EnsureLight(int LightId);

        template <typename Fn> void ForEachLight(Fn&& FnValue) const
        {
            for (const auto& [Id, Light] : Lights_)
                FnValue(Id, Light);
        }

      private:
        static void NormalizeDirection(Light3D& Light);
        bool RequireLight(Light3D*& Result, int LightId, const char* Command, std::string* Error = nullptr);

        EngineKernel& Kernel_;
        std::unordered_map<int, Light3D> Lights_;
        float AmbientLight_ = 0.25f;
        int AmbientColor_ = 0xFFFFFF;
    };

} // namespace dbc
