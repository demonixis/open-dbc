#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "language/language_catalogue.hpp"
#include "language/language_profile.hpp"
#include "scene/scene_capture.hpp"
#include "scene/scene_document.hpp"

// A program's transforms edited in the Scene view become changes to its own statements. Every edit is
// checked against a fresh parse of the text, so a source that moved since the capture is refused, not corrupted.
namespace dbc::studio
{
    namespace scene
    {
        const glm::vec3& TransformValue(const SceneNode& Node, TransformPart Part);
        glm::vec3& TransformValue(SceneNode& Node, TransformPart Part);

        // One change to a source file, located by the statement it starts from.
        struct SourceEdit
        {
            std::uint32_t Line = 0;
            std::uint32_t Column = 0;
            dbc::language::SymbolId Symbol = dbc::language::SymbolId::Count;
            // Rewrites this argument of the statement to Values[0]...
            int Argument = -1;
            // ...or adds this command right after it, its number repeating argument IdArgument.
            dbc::language::SymbolId Added = dbc::language::SymbolId::Count;
            int IdArgument = 0;
            std::vector<float> Values;
        };

        struct AxisEdit
        {
            NodeKey Node;
            TransformPart Part = TransformPart::Position;
            int Axis = 0;
            float Value = 0.0f;
        };

        struct PatchPlan
        {
            std::map<std::string, std::vector<SourceEdit>> Files; // by absolute source path
            std::vector<AxisEdit> Axes;
        };

        // What `After` changes from `Before` on the axes `Origins` leaves free. Locked axes never reach the plan.
        PatchPlan PlanPatches(const SceneDocument& Before, const SceneDocument& After,
                              const std::map<NodeKey, NodeOrigin>& Origins);

        struct PatchResult
        {
            std::string Text;
            std::string Error; // empty on success, which also means the text still parses
            // "12: X# * 2": expressions a number replaced, which the user may want back.
            std::vector<std::string> Replaced;
        };

        PatchResult PatchSource(const std::string& Text, const std::string& LogicalPath,
                                dbc::language::LanguageProfile Profile, std::span<const SourceEdit> Edits);

        // The edits a fresh capture of the patched program does not show, as messages for the user.
        std::vector<std::string> UnappliedEdits(std::span<const AxisEdit> Edits, const CaptureReadResult& Read);
    } // namespace scene
} // namespace dbc::studio
