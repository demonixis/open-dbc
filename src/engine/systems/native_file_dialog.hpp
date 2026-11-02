#pragma once

#include <string>
#include <vector>

struct SDL_Window;

namespace dbc
{
    // A filter as SDL takes it: a description and extensions separated by ';', or "*" for any file.
    struct FileDialogFilter
    {
        std::string Name;
        std::string Pattern;
    };

    // Reads Windows-style "Description|*.ext;*.ext2|..." filters. A string without '|' is a pattern list on its own.
    std::vector<FileDialogFilter> ParseFileDialogFilters(const std::string& Filter);

    // Shows the platform's open or save dialog and waits for it. A cancelled dialog leaves Output empty; a dialog
    // the platform cannot show is an error.
    bool ShowFileDialog(SDL_Window* Parent, bool Save, const std::string& Directory, const std::string& Filter,
                        const std::string& Title, std::string& Output, std::string* Error);

} // namespace dbc
