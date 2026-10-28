#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "scene/scene_document.hpp"

// Generated BASIC for the Scene view. The text is the only stored form: the
// Studio writes canonical statements and reads them back with the language
// frontend, refusing anything it did not write rather than dropping it.
namespace dbc::studio
{
    namespace scene
    {
        inline constexpr const char* KGameFunctionName = "SetupGame";
        inline constexpr const char* KGameFileName = "opendbc_game.dba";
        inline constexpr const char* KLevelFolder = "levels";

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

        struct GameReadResult
        {
            GameSettings Settings;
            std::vector<ReadError> Errors;

            bool Ok() const
            {
                return Errors.empty();
            }
        };

        // Image numbers are assigned here, in node order, one per distinct path.
        std::string WriteLevel(const SceneDocument& Document);
        // `Name` is the level name (the file stem); it must match the function.
        LevelReadResult ReadLevel(const std::string& Text, const std::string& Name,
                                  dbc::language::LanguageProfile Profile);

        std::string WriteGame(const GameSettings& Settings, dbc::language::LanguageProfile Profile);
        GameReadResult ReadGame(const std::string& Text, dbc::language::LanguageProfile Profile);

        // "levels/<name>.dba", relative to the project root.
        std::filesystem::path LevelRelativePath(const std::string& Name);
    } // namespace scene

} // namespace dbc::studio
