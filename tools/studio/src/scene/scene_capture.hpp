#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "language/language_catalogue.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"

// Any program as a Scene view document. `opendbc run --headless --capture-scene` records the scene
// commands a program runs and the engine's final state; replaying the commands gives each node its kind
// and the statement behind it, and the state corrects what a replay cannot know (POINT OBJECT, MOVE OBJECT...).
namespace dbc::studio
{
    namespace scene
    {
        struct SourceRef
        {
            std::string File; // absolute, generic UTF-8; empty when unknown
            std::uint32_t Line = 0;
            std::uint32_t Column = 0;

            bool operator==(const SourceRef&) const = default;
        };

        struct CallSite
        {
            std::uint32_t File = 0;
            std::uint32_t Line = 0;
            std::uint32_t Column = 0;
            int Frame = 0; // frames presented before the call
        };

        struct CapturedObject
        {
            int Id = 0;
            glm::vec3 Position{0.0f};
            glm::vec3 Rotation{0.0f};
            glm::vec3 Scale{100.0f}; // percent
            glm::vec3 Min{0.0f};
            glm::vec3 Max{0.0f};
            bool Visible = true;
            bool Screen = false;
            int Image = 0;
        };

        struct CapturedLight
        {
            int Id = 0;
            int Type = 1; // the engine's: 1 point, 2 spot, 3 directional
            bool Visible = true;
            glm::vec3 Position{0.0f};
            glm::vec3 Rotation{0.0f};
            glm::vec3 Direction{0.0f, -1.0f, 1.0f};
            float Range = 0.0f;
            Rgb Color = 0xFFFFFF;
        };

        struct CapturedMatrix
        {
            int Id = 0;
            glm::vec2 Size{0.0f};
            glm::ivec2 Segments{0};
            glm::vec3 Position{0.0f};
            int Image = 0;
            glm::ivec2 Tiles{1};
            std::vector<float> Heights;
            std::vector<int> Tile;
        };

        struct SceneCapture
        {
            std::string Status;
            std::size_t Frames = 0;
            bool Truncated = false;
            std::vector<std::string> Files; // absolute, generic UTF-8
            // In program order; Sites[i] is where Commands[i] ran. Asset paths are already resolved.
            std::vector<ReplayCall> Commands;
            std::vector<CallSite> Sites;
            std::vector<CapturedObject> Objects;
            std::vector<CapturedLight> Lights;
            std::vector<CapturedMatrix> Matrices;
            std::vector<CapturedMatrix> Terrains;
            std::map<int, std::string> Images; // asset path; empty for an image the program generated
            bool HasCamera = false;
            glm::vec3 CameraPosition{0.0f};
            glm::vec3 CameraRotation{0.0f};
            float CameraFov = 0.0f;
        };

        // `SourceRoot` resolves the relative source paths of a program image. Assets under `AssetRoot` become
        // relative to it, as in a level; others stay absolute. A capture of another language than the Scene
        // view's is an error.
        bool ParseSceneCapture(const std::string& Text, const std::filesystem::path& SourceRoot,
                               const std::filesystem::path& AssetRoot, SceneCapture& Out, std::string& Error);

        enum class TransformPart : std::uint8_t
        {
            Position,
            Rotation,
            Scale,
        };

        inline constexpr std::size_t KTransformPartCount = 3;

        enum class AxisAccess : std::uint8_t
        {
            Replace, // one statement, run once, sets this axis: its argument can be rewritten
            Insert,  // nothing sets this axis: a statement can follow the node's creation
            Locked,  // no single place in the source decides this axis
        };

        struct AxisOrigin
        {
            AxisAccess Access = AxisAccess::Locked;
            SourceRef Writer; // the statement setting this axis, if any
            dbc::language::SymbolId WriterSymbol = dbc::language::SymbolId::Count;
            int Argument = -1;  // this axis's argument in `Writer`
            std::string Reason; // why it is locked, for the Inspector
        };

        // Where a node comes from in the program's source.
        struct NodeOrigin
        {
            SourceRef Creator;
            dbc::language::SymbolId CreatorSymbol = dbc::language::SymbolId::Count;
            int CreatorIdArgument = 0;
            std::array<std::array<AxisOrigin, 3>, KTransformPartCount> Axes;
        };

        using NodeKey = std::pair<IdSpace, int>;

        // Whether the engine holds a value a statement wrote, within what its own conversions lose.
        bool SameTransformValue(TransformPart Part, float Left, float Right);

        struct CaptureReadOptions
        {
            std::string Name;
            // Calls before this frame are the program's setup; later ones are its game loop.
            int SetupFrames = 1;
            // When set, only the nodes this source file creates: an included file seen through its program.
            std::string OnlyFile;
        };

        struct CaptureReadResult
        {
            SceneDocument Document;
            std::map<NodeKey, NodeOrigin> Origins;
            // What the document leaves out, for the user.
            std::vector<std::string> Notes;
        };

        CaptureReadResult ReadCapture(const SceneCapture& Capture, const CaptureReadOptions& Options);
    } // namespace scene
} // namespace dbc::studio
