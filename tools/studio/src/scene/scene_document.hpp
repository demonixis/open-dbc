#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "language/language_profile.hpp"

// The level edited by the Scene view. Its only persisted form is the generated
// .dba file (scene_codegen.hpp); everything here maps to a BASIC statement.
namespace dbc::studio
{
    namespace scene
    {
        enum class NodeKind : std::uint8_t
        {
            Cube,
            Box,
            Sphere,
            Cylinder,
            Cone,
            Plain,
            Triangle,
            Model,
            Matrix,
            Terrain,
            Light,
        };

        enum class LightKind : std::uint8_t
        {
            Point,
            Spot,
            Directional,
        };

        // Number spaces the BASIC program keeps apart.
        enum class IdSpace : std::uint8_t
        {
            Object,
            Image,
            Matrix,
            Terrain,
            Light,
        };

        inline constexpr std::size_t KIdSpaceCount = 5;

        IdSpace IdSpaceOf(NodeKind Kind);
        const char* NodeKindLabel(NodeKind Kind);
        // False for kinds whose commands the profile cannot run (terrains in Classic).
        bool KindAvailable(NodeKind Kind, dbc::language::LanguageProfile Profile);

        // Packed 0x00RRGGBB, written as RGB(r, g, b).
        using Rgb = std::uint32_t;

        // SET OBJECT flags. `Transparency` is the raw argument: its meaning differs
        // between profiles (see OpaqueTransparency).
        struct ObjectFlags
        {
            bool Wireframe = false;
            int Transparency = 0;
            bool Cull = true;
            bool Filter = true;
            bool Lighting = true;
            bool Fog = true;
            bool Ambient = true;

            bool operator==(const ObjectFlags&) const = default;
        };

        // The SET OBJECT transparency value that leaves black opaque: Classic reads
        // 0 as "transparent black", Pro reads it as "no transparency".
        int OpaqueTransparency(dbc::language::LanguageProfile Profile);
        ObjectFlags DefaultObjectFlags(dbc::language::LanguageProfile Profile);

        struct Material
        {
            std::string Texture; // asset-relative path, empty for none
            std::optional<Rgb> Color;
            bool Ghost = false;
            std::optional<int> GhostMode;
            ObjectFlags Flags;

            // Pro only.
            std::optional<Rgb> Diffuse;
            std::optional<Rgb> Ambience;
            std::optional<Rgb> Emissive;
            std::optional<Rgb> Specular;
            std::optional<float> SpecularPower;
            bool CastShadow = false;

            bool operator==(const Material&) const = default;
        };

        // One field set covers every kind; each kind reads the fields it uses.
        struct SceneNode
        {
            NodeKind Kind = NodeKind::Cube;
            int Id = 0;
            std::string Name;

            glm::vec3 Position{0.0f};
            glm::vec3 Rotation{0.0f}; // degrees, applied X then Y then Z
            glm::vec3 Scale{100.0f};  // percent

            float Size = 10.0f;                 // cube edge, sphere diameter, cylinder and cone size
            glm::vec3 Extent{10.0f};            // box width/height/depth, plain width/height
            std::optional<glm::ivec2> Detail;   // sphere rings and segments (Pro form)
            std::optional<int> PlainOrientation; // Pro form
            std::array<glm::vec3, 3> Corners{glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(10.0f, -10.0f, 0.0f),
                                             glm::vec3(-10.0f, -10.0f, 0.0f)};

            std::string Asset; // model file or terrain heightmap, asset-relative

            // Matrix: size and tiles, plus the PREPARE MATRIX TEXTURE grid.
            float Width = 100.0f;
            float Depth = 100.0f;
            int XSegments = 10;
            int ZSegments = 10;
            int TilesAcross = 1;
            int TilesDown = 1;

            // Light
            LightKind Light = LightKind::Point;
            std::optional<float> Range;
            float SpotInner = 20.0f;
            float SpotOuter = 40.0f;
            std::optional<Rgb> LightColor;

            Material Mat;

            bool operator==(const SceneNode&) const = default;
        };

        struct Environment
        {
            std::optional<int> AmbientPercent;
            std::optional<Rgb> AmbientColor;
            std::optional<Rgb> Backdrop;

            bool Fog = false;
            std::optional<Rgb> FogColor;
            std::optional<float> FogDistance;

            // Light 0, which every program starts with.
            bool DefaultLightHidden = false;
            std::optional<glm::vec3> DefaultLightRotation;
            std::optional<Rgb> DefaultLightColor;

            // Pro: SET SHADOW POSITION mode (-1 for the fixed position, else a light).
            std::optional<int> ShadowMode;
            glm::vec3 ShadowPosition{0.0f, 100.0f, 0.0f};

            // Off by default: the program keeps its own camera behaviour.
            bool UseCamera = false;
            glm::vec3 CameraPosition{0.0f, 50.0f, -100.0f};
            glm::vec3 CameraRotation{0.0f};
            std::optional<float> CameraFov;
            std::optional<glm::vec2> CameraRange;

            bool operator==(const Environment&) const = default;
        };

        // Program-wide display settings, kept in their own generated file because
        // every level shares them.
        struct GameSettings
        {
            std::optional<glm::ivec3> DisplayMode; // width, height, depth
            std::optional<int> Vsync;              // Pro four-argument SET DISPLAY MODE
            std::optional<bool> Windowed;          // SET WINDOW ON / OFF
            std::optional<glm::ivec2> WindowSize;
            std::optional<std::string> WindowTitle;
            bool SyncOn = false;
            std::optional<int> SyncRate;
            bool HideMouse = false;

            bool operator==(const GameSettings&) const = default;
        };

        struct SceneDocument
        {
            std::string Name; // level name, also the function suffix
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
            std::array<int, KIdSpaceCount> IdBase{1000, 1000, 1, 1, 1};
            Environment Env;
            std::vector<SceneNode> Nodes;

            int& Base(IdSpace Space)
            {
                return IdBase[static_cast<std::size_t>(Space)];
            }
            int Base(IdSpace Space) const
            {
                return IdBase[static_cast<std::size_t>(Space)];
            }

            // Smallest number at or above the space's base that no node uses.
            int NextId(IdSpace Space) const;
            SceneNode* Find(IdSpace Space, int Id);
            const SceneNode* Find(IdSpace Space, int Id) const;
            // A node of `Kind` with the next free number and engine-like defaults.
            SceneNode& Add(NodeKind Kind);

            bool operator==(const SceneDocument&) const = default;
        };

        // "Level 1" -> "Level_1": a valid BASIC identifier suffix, never empty.
        std::string SanitizeLevelName(const std::string& Name);
        std::string LevelFunctionName(const SceneDocument& Document);

        // Snapshot undo: levels are small, so whole copies keep it simple and exact.
        class SceneHistory
        {
          public:
            void Reset(const SceneDocument& Document);
            // Records `Document` as the new current state if it changed.
            void Commit(const SceneDocument& Document);
            bool CanUndo() const;
            bool CanRedo() const;
            const SceneDocument& Undo();
            const SceneDocument& Redo();

          private:
            std::vector<SceneDocument> States_;
            std::size_t Current_ = 0;
        };
    } // namespace scene

} // namespace dbc::studio
