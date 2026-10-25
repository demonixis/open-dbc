#include "vr_system.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace dbc
{
    void VrTracking::Update(std::size_t Index, const glm::mat4& Pose, bool IsTracked)
    {
        if (Index >= Poses.size())
            return;
        for (int Column = 0; Column < 4; ++Column)
            for (int Row = 0; Row < 4; ++Row)
                IsTracked &= std::isfinite(Pose[Column][Row]);
        Tracked[Index] = IsTracked;
        if (IsTracked)
        {
            Poses[Index] = Pose;
            Valid[Index] = true;
        }
    }

    bool VrTracking::RecenterHead()
    {
        if (!Tracked[0] || !Valid[0])
            return false;
        const glm::vec3 Forward = glm::vec3(Poses[0][2]);
        if (std::hypot(Forward.x, Forward.z) < 1e-6f)
            return false;
        const float Yaw = std::atan2(Forward.x, Forward.z);
        glm::vec3 Position = glm::vec3(Poses[0][3]);
        if (Space != VrTrackingSpace::Local)
            Position.y = 0;
        Recenter = glm::rotate(glm::mat4(1.0f), -Yaw, glm::vec3(0, 1, 0)) * glm::translate(glm::mat4(1.0f), -Position);
        return true;
    }

    glm::mat4 VrTracking::Origin() const
    {
        return glm::translate(glm::mat4(1.0f), glm::vec3(0, Height, 0)) * Recenter;
    }

    VrPose VrTracking::Compose(std::size_t Index, const glm::mat4& Player) const
    {
        if (Index >= Poses.size() || !Valid[Index])
            return {};
        const glm::mat4 World = Player * Origin() * Poses[Index];
        // Invert the same default DBC camera order: world = Ry(yaw) Rx(pitch) Rz(roll).
        const float Pitch = std::asin(std::clamp(-World[2][1], -1.0f, 1.0f));
        const bool Pole = std::abs(std::cos(Pitch)) < 1e-5f;
        const float Yaw = Pole ? std::atan2(-World[0][2], World[0][0]) : std::atan2(World[2][0], World[2][2]);
        const float Roll = Pole ? 0.0f : std::atan2(World[0][1], World[1][1]);
        return {glm::vec3(World[3]), glm::degrees(glm::vec3(Pitch, Yaw, Roll)), Tracked[Index]};
    }

    glm::mat4 VrTracking::ConvertPose(const glm::vec3& Position, const glm::vec4& Quaternion)
    {
        const glm::quat Rotation(Quaternion.w, Quaternion.x, Quaternion.y, Quaternion.z);
        glm::mat4 Basis(1.0f);
        Basis[2][2] = -1.0f;
        return Basis * (glm::translate(glm::mat4(1.0f), Position) * glm::mat4_cast(Rotation)) * Basis;
    }

    glm::mat4 VrTracking::Projection(float Left, float Right, float Up, float Down, float Near, float Far)
    {
        const float L = std::tan(Left), R = std::tan(Right), U = std::tan(Up), D = std::tan(Down);
        glm::mat4 Result(0.0f);
        Result[0][0] = 2.0f / (R - L);
        Result[1][1] = 2.0f / (U - D);
        Result[2][0] = -(R + L) / (R - L);
        Result[2][1] = -(U + D) / (U - D);
        Result[2][2] = Far / (Far - Near);
        Result[2][3] = 1.0f;
        Result[3][2] = -Far * Near / (Far - Near);
        return Result;
    }

    glm::vec4 VrTracking::Letterbox(float X, float Y, float Width, float Height, float ImageWidth, float ImageHeight)
    {
        if (Width <= 0 || Height <= 0 || ImageWidth <= 0 || ImageHeight <= 0)
            return {};
        const float Scale = std::min(Width / ImageWidth, Height / ImageHeight);
        const float W = ImageWidth * Scale, H = ImageHeight * Scale;
        return {X + (Width - W) * 0.5f, Y + (Height - H) * 0.5f, W, H};
    }
} // namespace dbc
