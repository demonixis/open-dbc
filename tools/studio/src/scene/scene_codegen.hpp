#pragma once

#include <filesystem>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "language/language_catalogue.hpp"
#include "scene/scene_document.hpp"

// Generated BASIC for the Scene view. The text is the only stored form: the
// Studio writes canonical statements and reads them back with the language
// frontend, refusing anything it did not write rather than dropping it.
namespace dbc::studio
{
    namespace scene
    {
        inline constexpr const char* KLevelFolder = "levels";
        inline constexpr const char* KWorldFileName = "world_settings.dba";

        struct ReadError
        {
            std::uint32_t Line = 0;
            std::string Message;
        };

        struct LevelReadResult
        {
            SceneDocument Document;
            std::vector<ReadError> Errors;

            bool Ok() const
            {
                return Errors.empty();
            }
        };

        // A number as BASIC source: fixed notation, no exponent the language could not read.
        std::string NumberLiteral(float Value);
        // The nodes. Image numbers are assigned here, in node order, one per distinct path.
        std::string WriteLevel(const SceneDocument& Document);
        // `Name` is the level name (the file stem); it must match the function. The environment is
        // accepted too, as older levels hold it: the next save moves it to the world settings.
        LevelReadResult ReadLevel(const std::string& Text, const std::string& Name);
        // The display and the environment.
        std::string WriteWorld(const SceneDocument& Document);
        // Applies a world settings file over `Document`, whose name it reads with.
        std::vector<ReadError> ReadWorld(const std::string& Text, SceneDocument& Document);

        // A command a program really ran, with its evaluated arguments.
        struct ReplayArgument
        {
            bool IsText = false;
            double Number = 0.0;
            std::string Text;
        };

        struct ReplayCall
        {
            dbc::language::SymbolId Symbol{};
            std::vector<ReplayArgument> Arguments;
        };

        struct ReplayResult
        {
            SceneDocument Document;
            // The commands a document cannot hold.
            std::set<std::string> Skipped;
        };

        // The level a program's own calls amount to. Unlike ReadLevel it refuses nothing: a program may reuse
        // a number, and what no statement of a level can express is only named.
        ReplayResult ReplayCalls(std::span<const ReplayCall> Calls, const std::string& Name);

        // "levels/<name>/<name>.dba", relative to the project root.
        std::filesystem::path LevelRelativePath(const std::string& Name);
        // "levels/<name>/world_settings.dba", relative to the project root.
        std::filesystem::path WorldRelativePath(const std::string& Name);
    } // namespace scene

} // namespace dbc::studio
