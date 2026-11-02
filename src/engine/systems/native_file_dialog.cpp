#include "native_file_dialog.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>

namespace dbc
{
    namespace
    {
        struct DialogState
        {
            std::atomic<bool> Done = false;
            bool Failed = false;
            std::string Path;
            std::string Error;
        };

        void SDLCALL OnDialogResult(void* Userdata, const char* const* Files, int /*Filter*/)
        {
            auto* State = static_cast<DialogState*>(Userdata);
            if (!Files)
            {
                State->Failed = true;
                State->Error = SDL_GetError();
            }
            else if (Files[0])
            {
                State->Path = Files[0];
            }
            State->Done.store(true, std::memory_order_release);
        }

        std::string Trim(const std::string& Text)
        {
            const std::size_t Start = Text.find_first_not_of(" \t");
            if (Start == std::string::npos)
                return {};
            return Text.substr(Start, Text.find_last_not_of(" \t") - Start + 1);
        }

        // "*.txt;*.dat" becomes "txt;dat"; "*.*" and "*" accept any file.
        std::string SdlPattern(const std::string& Patterns)
        {
            std::string Result;
            std::size_t Start = 0;
            while (Start <= Patterns.size())
            {
                const std::size_t End = std::min(Patterns.find(';', Start), Patterns.size());
                std::string Item = Trim(Patterns.substr(Start, End - Start));
                Start = End + 1;
                if (Item == "*" || Item == "*.*")
                    return "*";
                if (Item.rfind("*.", 0) == 0)
                    Item.erase(0, 2);
                else if (Item.rfind('.', 0) == 0)
                    Item.erase(0, 1);
                if (Item.empty())
                    continue;
                if (!Result.empty())
                    Result.push_back(';');
                Result += Item;
            }
            return Result;
        }
    } // namespace

    std::vector<FileDialogFilter> ParseFileDialogFilters(const std::string& Filter)
    {
        std::vector<std::string> Parts;
        std::size_t Start = 0;
        while (Start <= Filter.size())
        {
            const std::size_t End = std::min(Filter.find('|', Start), Filter.size());
            Parts.push_back(Filter.substr(Start, End - Start));
            Start = End + 1;
        }
        std::vector<FileDialogFilter> Filters;
        if (Parts.size() == 1)
        {
            const std::string Pattern = SdlPattern(Parts[0]);
            if (!Pattern.empty())
                Filters.push_back({Trim(Parts[0]), Pattern});
            return Filters;
        }
        for (std::size_t Index = 0; Index + 1 < Parts.size(); Index += 2)
        {
            const std::string Pattern = SdlPattern(Parts[Index + 1]);
            if (!Pattern.empty())
                Filters.push_back({Trim(Parts[Index]), Pattern});
        }
        return Filters;
    }

    bool ShowFileDialog(SDL_Window* Parent, bool Save, const std::string& Directory, const std::string& Filter,
                        const std::string& Title, std::string& Output, std::string* Error)
    {
        Output.clear();
        const std::vector<FileDialogFilter> Filters = ParseFileDialogFilters(Filter);
        std::vector<SDL_DialogFileFilter> SdlFilters;
        for (const FileDialogFilter& Item : Filters)
            SdlFilters.push_back({Item.Name.c_str(), Item.Pattern.c_str()});

        const SDL_PropertiesID Properties = SDL_CreateProperties();
        if (!Properties)
        {
            if (Error)
                *Error = std::string("The file dialog could not be prepared: ") + SDL_GetError();
            return false;
        }
        if (!SdlFilters.empty())
        {
            SDL_SetPointerProperty(Properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, SdlFilters.data());
            SDL_SetNumberProperty(Properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                                  static_cast<Sint64>(SdlFilters.size()));
        }
        if (Parent)
            SDL_SetPointerProperty(Properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, Parent);
        if (!Directory.empty())
            SDL_SetStringProperty(Properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, Directory.c_str());
        if (!Title.empty())
            SDL_SetStringProperty(Properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, Title.c_str());

        DialogState State;
        SDL_ShowFileDialogWithProperties(Save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE, OnDialogResult,
                                         &State, Properties);
        // The dialog answers through the event loop on some platforms and from its own thread on others; either
        // way the program waits, as it did for the Windows dialog.
        while (!State.Done.load(std::memory_order_acquire))
        {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        SDL_DestroyProperties(Properties);

        if (State.Failed)
        {
            if (Error)
                *Error = "The file dialog could not be shown: " + State.Error;
            return false;
        }
        Output = State.Path;
        return true;
    }

} // namespace dbc
