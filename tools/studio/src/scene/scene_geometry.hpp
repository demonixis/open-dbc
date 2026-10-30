#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
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

        // A texture stored inside a model file, decoded to RGBA8.
        struct ModelImage
        {
            int Width = 0;
            int Height = 0;
            std::vector<std::uint8_t> Rgba;
        };

        // A run of a model's vertices sharing one imported material, as LOAD OBJECT
        // keeps it per mesh.
        struct MeshPart
        {
            std::size_t First = 0;
            std::size_t Count = 0;
            glm::vec4 Color{1.0f}; // the diffuse colour, shown where no texture applies
            std::string Texture;   // a file named by the material, as written there
            int Image = -1;        // or an index into CpuMesh::Images
        };

        // A triangle list in the node's local space.
        struct CpuMesh
        {
            std::vector<MeshVertex> Vertices;
            // Models only; a mesh without parts draws as one untextured run.
            std::vector<MeshPart> Parts;
            std::vector<ModelImage> Images;
            glm::vec3 Min{0.0f};
            glm::vec3 Max{0.0f};
        };

        // Light 0's direction when the level does not rotate it.
        inline constexpr glm::vec3 KDefaultLightDirection{0.0f, -0.70710678f, 0.70710678f};
        inline constexpr float KTerrainHeightScale = 100.0f;

        // Primitives and matrices; empty for models and terrains, which need files.
        CpuMesh BuildMesh(const SceneNode& Node, dbc::language::LanguageProfile Profile);
        // LOAD OBJECT through Assimp, with the engine's left-handed conversion and
        // each mesh's first material texture in its search order.
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

        // Puts each axis `Value` moved away from `Start` on the nearest multiple of
        // `Step`; the other axes keep their value. A step of 0 or less snaps nothing.
        glm::vec3 SnapChangedAxes(const glm::vec3& Value, const glm::vec3& Start, float Step);
        // Puts every axis on the nearest multiple of `Step`. A step of 0 or less snaps nothing.
        glm::vec3 SnapToGrid(const glm::vec3& Value, float Step);
        // `Move` adjusted so that `Anchor` lands on the grid on the axes it changes: a
        // multi-selection follows one node, which its centre rarely matches.
        glm::vec3 SnapAnchoredMove(const glm::vec3& Anchor, const glm::vec3& Move, float Step);

        inline constexpr float KMinGridStep = 0.01f;
        inline constexpr float KMaxGridStep = 10000.0f;
        // The next step up or down the series 1, 2.5, 5, 10, 25..., from any step.
        float NextGridStep(float Step, bool Up);

        enum class GridLineKind
        {
            Minor,
            Major,
            AxisX,
            AxisZ,
        };

        struct GridLine
        {
            glm::vec3 A{0.0f};
            glm::vec3 B{0.0f};
            GridLineKind Kind = GridLineKind::Minor;
        };

        // The ground grid around `Focus`, in cells of `Step` with a major line every 10.
        // Empty for a step of 0 or less.
        std::vector<GridLine> BuildGridLines(float Step, const glm::vec3& Focus);
        // Scales all of `Start` by the ratio of the axis `Edited` changed most, keeping
        // its proportions. An axis that starts at 0 has no ratio: `Edited` is kept.
        glm::vec3 ProportionalScale(const glm::vec3& Start, const glm::vec3& Edited);

        // A gizmo edit of several nodes, measured from where the drag began:
        // positions turn and scale about `Pivot` along `Axes`, then move.
        struct GroupChange
        {
            glm::vec3 Pivot{0.0f};
            glm::vec3 Move{0.0f};
            glm::mat3 Turn{1.0f};
            glm::mat3 Axes{1.0f};
            glm::vec3 Factor{1.0f};
        };

        // Sets `Node` from its state `Start` at the beginning of the drag. Only
        // objects and lights turn, only objects scale; the rest just move.
        void ApplyGroupChange(const GroupChange& Change, const SceneNode& Start, SceneNode& Node);

        // SET SHADOW SHADING ON's default range: how far a shadow volume reaches.
        inline constexpr float KShadowRange = 2000.0f;
        // Where SET SHADOW POSITION puts the shadow's light: the fixed point for -1,
        // else light N's position. Light 0, the default, and a missing light sit at
        // the origin, as in the engine.
        glm::vec3 ShadowSource(const SceneDocument& Document);
        // The engine's shadow volume: each triangle facing `Light`, pushed 0.1 away
        // from it and extruded `Range` further, with both caps and the three sides.
        void AppendShadowVolume(const CpuMesh& Mesh, const glm::mat4& World, const glm::vec3& Light, float Range,
                                std::vector<glm::vec3>& Out);

        struct Ray
        {
            glm::vec3 Origin{0.0f};
            glm::vec3 Direction{0.0f, 0.0f, 1.0f};
        };

        // Distance along the ray to the nearest triangle, both sides counted.
        std::optional<float> Intersect(const Ray& Value, const CpuMesh& Mesh, const glm::mat4& World);

        // `Count` points spread evenly over a disc.
        std::vector<glm::vec2> ScatterInDisc(const glm::vec2& Center, float Radius, int Count, std::mt19937& Random);
        // What a stamp of the paint brush places at most, whatever its radius and density.
        inline constexpr int KMaxScatter = 2000;
        // Points that bring a disc up to `Density` objects per 100 x 100 units, apart from one
        // another and from `Existing`, so that painting over an area does not pile objects up.
        std::vector<glm::vec2> ScatterToDensity(const glm::vec2& Center, float Radius, float Density,
                                                const std::vector<glm::vec2>& Existing, std::mt19937& Random);
        // How far above a surface an object's origin sits for its bounds to rest on it.
        float RestingHeight(const glm::vec3& Min, const glm::vec3& Max, const glm::vec3& Rotation,
                            const glm::vec3& Scale);

        // Where the viewport shows the world, for measuring on screen.
        struct ScreenView
        {
            glm::mat4 ViewProjection{1.0f};
            glm::vec2 Min{0.0f};
            glm::vec2 Size{1.0f};
        };

        struct ScreenVertex
        {
            glm::vec3 World{0.0f};
            float Pixels = 0.0f;
            float Depth = 0.0f;
        };

        // Replaces `Best` when `Point` is visible and nearer `Mouse` on screen. Points within
        // a pixel of each other, like a box's corners seen head-on, go to the nearer to the eye.
        void KeepNearestPoint(const glm::vec3& Point, const ScreenView& View, const glm::vec2& Mouse,
                              std::optional<ScreenVertex>& Best);
        void KeepNearestVertex(const CpuMesh& Mesh, const glm::mat4& World, const ScreenView& View,
                               const glm::vec2& Mouse, std::optional<ScreenVertex>& Best);
    } // namespace scene

} // namespace dbc::studio
