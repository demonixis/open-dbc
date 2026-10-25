#include "light_manager.hpp"
#include "../engine_math.hpp"
#include "engine_kernel.hpp"
#include "object_manager.hpp"

#include <string>

namespace dbc
{
    LightManager::LightManager(EngineKernel& Kernel) : Kernel_(Kernel)
    {
        // Create default directional light (id 0).
        EnsureLight(0);
    }

    LightManager::~LightManager() = default;

    // === Ambient ===

    void LightManager::SetAmbientLight(int Value)
    {
        const int Clamped = std::clamp(Value, 0, 100);
        AmbientLight_ = static_cast<float>(Clamped) / 100.0f;
    }

    void LightManager::ColorAmbientLight(int RgbPacked)
    {
        AmbientColor_ = RgbPacked;
    }

    float LightManager::AmbientLight() const
    {
        return AmbientLight_;
    }

    int LightManager::AmbientColor() const
    {
        return AmbientColor_;
    }

    // === Light CRUD ===

    bool LightManager::MakeLight(int LightId, std::string* Error)
    {
        if (LightId <= 0 || LightId > 65535)
        {
            if (Error)
                *Error = "MAKE LIGHT number must be between 1 and 65535";
            return false;
        }
        Light3D Light{};
        Light.Type = 1;
        Light.DirX = 0.0f;
        Light.DirY = -1.0f;
        Light.DirZ = 0.0f;
        if (!Lights_.emplace(LightId, Light).second)
        {
            if (Error)
                *Error = "MAKE LIGHT cannot replace light " + std::to_string(LightId);
            return false;
        }
        return true;
    }

    bool LightManager::DeleteLight(int LightId, std::string* Error)
    {
        if (LightId == 0)
        {
            if (Error)
                *Error = "DELETE LIGHT cannot delete light zero";
            return false;
        }
        if (Lights_.erase(LightId) == 0)
        {
            if (Error)
                *Error = "DELETE LIGHT requires existing light " + std::to_string(LightId);
            return false;
        }
        return true;
    }

    bool LightManager::ShowLight(int LightId, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "SHOW LIGHT", Error))
            return false;
        Light->Visible = true;
        return true;
    }

    bool LightManager::HideLight(int LightId, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "HIDE LIGHT", Error))
            return false;
        Light->Visible = false;
        return true;
    }

    int LightManager::LightExist(int LightId) const
    {
        return Lights_.find(LightId) != Lights_.end() ? 1 : 0;
    }

    int LightManager::LightVisible(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        if (It == Lights_.end())
        {
            return 0;
        }
        return It->second.Visible ? 1 : 0;
    }

    int LightManager::LightType(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        if (It == Lights_.end())
        {
            return 0;
        }
        return It->second.Type;
    }

    // === Light properties ===

    bool LightManager::ColorLight(int LightId, int RgbPacked, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "COLOR LIGHT", Error))
            return false;
        Light->Color = RgbPacked;
        return true;
    }

    bool LightManager::PositionLight(int LightId, float X, float Y, float Z, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "POSITION LIGHT", Error))
            return false;
        Light->PosX = X;
        Light->PosY = Y;
        Light->PosZ = Z;
        return true;
    }

    bool LightManager::RotateLight(int LightId, float XDegrees, float YDegrees, float ZDegrees, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "ROTATE LIGHT", Error))
            return false;
        Light->RotX = XDegrees;
        Light->RotY = YDegrees;
        Light->RotZ = ZDegrees;

        const float Yaw = DegToRad(YDegrees);
        const float Pitch = DegToRad(XDegrees);
        const float Cp = std::cos(Pitch);
        Light->DirX = std::sin(Yaw) * Cp;
        Light->DirY = -std::sin(Pitch);
        Light->DirZ = std::cos(Yaw) * Cp;
        NormalizeDirection(*Light);
        return true;
    }

    bool LightManager::PointLight(int LightId, float X, float Y, float Z, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "POINT LIGHT", Error))
            return false;
        Light->DirX = X - Light->PosX;
        Light->DirY = Y - Light->PosY;
        Light->DirZ = Z - Light->PosZ;
        NormalizeDirection(*Light);
        return true;
    }

    bool LightManager::SetDirectionalLight(int LightId, float X, float Y, float Z, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "SET DIRECTIONAL LIGHT", Error))
            return false;
        Light->Type = 3;
        Light->DirX = X;
        Light->DirY = Y;
        Light->DirZ = Z;
        Light->Range = 5000.0f;
        NormalizeDirection(*Light);
        return true;
    }

    bool LightManager::SetPointLight(int LightId, float X, float Y, float Z, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "SET POINT LIGHT", Error))
            return false;
        Light->Type = 1;
        Light->PosX = X;
        Light->PosY = Y;
        Light->PosZ = Z;
        return true;
    }

    bool LightManager::SetSpotLight(int LightId, float InnerAngle, float OuterAngle, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "SET SPOT LIGHT", Error))
            return false;
        Light->Type = 2;
        Light->PosX = 0.0f;
        Light->PosY = 0.0f;
        Light->PosZ = 0.0f;
        Light->DirX = 0.0f;
        Light->DirY = -1.0f;
        Light->DirZ = 0.0f;
        Light->SpotInner = InnerAngle;
        Light->SpotOuter = OuterAngle;
        return true;
    }

    bool LightManager::SetLightRange(int LightId, float Range, std::string* Error)
    {
        Light3D* Light = nullptr;
        if (!RequireLight(Light, LightId, "SET LIGHT RANGE", Error))
            return false;
        Light->Range = std::max(0.0f, Range);
        return true;
    }

    // === Light getters ===

    float LightManager::LightPositionX(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.PosX;
    }

    float LightManager::LightPositionY(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.PosY;
    }

    float LightManager::LightPositionZ(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.PosZ;
    }

    float LightManager::LightDirectionX(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.DirX;
    }

    float LightManager::LightDirectionY(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.DirY;
    }

    float LightManager::LightDirectionZ(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.DirZ;
    }

    float LightManager::LightRotationX(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.RotX;
    }

    float LightManager::LightRotationY(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.RotY;
    }

    float LightManager::LightRotationZ(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.RotZ;
    }

    float LightManager::LightRange(int LightId) const
    {
        const auto It = Lights_.find(LightId);
        return (It == Lights_.end()) ? 0.0f : It->second.Range;
    }

    // === Cross-subsystem helpers ===

    bool LightManager::SetLightToObjectPosition(int LightId, int ObjectId, std::string* Error)
    {
        const Object3D* Obj = Kernel_.Objects().GetObject(ObjectId);
        if (!Obj)
        {
            if (Error)
                *Error = "SET LIGHT TO OBJECT POSITION requires existing object " + std::to_string(ObjectId);
            return false;
        }
        if (!PositionLight(LightId, Obj->PosX, Obj->PosY, Obj->PosZ, Error))
            return false;
        return true;
    }

    bool LightManager::SetLightToObjectOrientation(int LightId, int ObjectId, std::string* Error)
    {
        const Object3D* Obj = Kernel_.Objects().GetObject(ObjectId);
        if (!Obj)
        {
            if (Error)
                *Error = "SET LIGHT TO OBJECT ORIENTATION requires existing object " + std::to_string(ObjectId);
            return false;
        }
        if (!RotateLight(LightId, Obj->RotX, Obj->RotY, Obj->RotZ, Error))
            return false;
        return true;
    }

    // === Renderer access ===

    Light3D& LightManager::EnsureLight(int LightId)
    {
        auto [It, Inserted] = Lights_.try_emplace(LightId, Light3D{});
        if (Inserted && LightId == 0)
        {
            It->second.Type = 3;
        }
        return It->second;
    }

    bool LightManager::RequireLight(Light3D*& Result, int LightId, const char* Command, std::string* Error)
    {
        const auto It = Lights_.find(LightId);
        if (It == Lights_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing light " + std::to_string(LightId);
            return false;
        }
        Result = &It->second;
        return true;
    }

    // === Static helpers ===

    void LightManager::NormalizeDirection(Light3D& Light)
    {
        const float Len = std::sqrt(Light.DirX * Light.DirX + Light.DirY * Light.DirY + Light.DirZ * Light.DirZ);
        if (Len < 0.0001f)
        {
            Light.DirX = 0.0f;
            Light.DirY = -1.0f;
            Light.DirZ = 1.0f;
            return;
        }
        Light.DirX /= Len;
        Light.DirY /= Len;
        Light.DirZ /= Len;
    }

} // namespace dbc
