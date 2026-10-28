#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "scene/scene_document.hpp"

// CPU geometry for the Scene view, built the way the engine builds it so the
// preview keeps the program's sizes, origins and UVs.
namespace dbc::studio
{
    namespace scene
    {
        struct MeshVertex
        {
            glm::vec3 Position{0.0f};
            glm::vec3 Normal{0.0f, 1.0f, 0.0f};
            glm::vec2 Uv{0.0f};
        };

        // A triangle list in the node's local space.
        struct CpuMesh
        {
            std::vector<MeshVertex> Vertices;
            glm::vec3 Min{0.0f};
            glm::vec3 Max{0.0f};
        };

        // Light 0's direction when the level does not rotate it.
        inline constexpr glm::vec3 KDefaultLightDirection{0.0f, -0.70710678f, 0.70710678f};
        inline constexpr float KTerrainHeightScale = 100.0f;

        // Primitives and matrices; empty for models and terrains, which need files.
        CpuMesh BuildMesh(const SceneNode& Node, dbc::language::LanguageProfile Profile);
        // LOAD OBJECT through Assimp, with the engine's left-handed conversion.
        bool LoadModelMesh(const std::filesystem::path& Path, CpuMesh& Out, std::string& Error);
        // MAKE TERRAIN: one vertex per pixel, growing toward -Z from the position.
        bool LoadTerrainMesh(const std::filesystem::path& Heightmap, CpuMesh& Out, std::string& Error);
        void ComputeBounds(CpuMesh& Mesh);

        // The animation LOAD OBJECT keeps: a model file's first, at 25 frames a second.
        struct ModelAnimation
        {
            std::string Name;
            int LastFrame = 0;      // TOTAL OBJECT FRAMES
            unsigned int Count = 0; // animations in the file, 0 for a still model
        };
        bool ReadModelAnimation(const std::filesystem::path& Path, ModelAnimation& Out, std::string& Error);

        // Rotation in degrees, applied X, then Y, then Z, as ROTATE OBJECT does.
        glm::mat3 RotationMatrix(const glm::vec3& Degrees);
        // The X/Y/Z angles that RotationMatrix maps back to `Rotation`.
        glm::vec3 EulerDegrees(const glm::mat3& Rotation);
        // Where ROTATE LIGHT points a light.
        glm::vec3 LightDirection(const glm::vec3& Degrees);
        // Local to world. Matrices, terrains and lights only move.
        glm::mat4 NodeWorld(const SceneNode& Node);

        // Scales all of `Start` by the ratio of the axis `Edited` changed most, keeping
        // its proportions. An axis that starts at 0 has no ratio: `Edited` is kept.
        glm::vec3 ProportionalScale(const glm::vec3& Start, const glm::vec3& Edited);

        struct Ray
        {
            glm::vec3 Origin{0.0f};
            glm::vec3 Direction{0.0f, 0.0f, 1.0f};
        };
        // Distance along the ray to the nearest triangle, both sides counted.
        std::optional<float> Intersect(const Ray& Value, const CpuMesh& Mesh, const glm::mat4& World);
    } // namespace scene

} // namespace dbc::studio
