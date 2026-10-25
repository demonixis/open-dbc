#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace dbc::studio
{
    namespace core
    {
        // Native file dialogs through SDL_dialog (Cocoa on macOS, comdlg32 on
        // Windows, the portal or zenity on Linux).
        //
        // SDL invokes the completion callback on its own thread, so results are
        // parked here and picked up by Poll() from the UI loop.
        class FileDialog
        {
          public:
            using Handler = std::function<void(const std::string& Path)>;

            // Only one dialog can be pending at a time; requests made while one is
            // open are ignored.
            void OpenFile(SDL_Window* Parent, const std::string& DefaultLocation, const std::string& FilterName,
                          const std::string& FilterPatterns, Handler HandlerValue);
            void SaveFile(SDL_Window* Parent, const std::string& DefaultLocation, const std::string& FilterName,
                          const std::string& FilterPatterns, Handler HandlerValue);
            void OpenFolder(SDL_Window* Parent, const std::string& DefaultLocation, Handler HandlerValue);

            bool IsOpen() const
            {
                return Open_;
            }

            // Runs the handler for a dialog that closed since the last call. An
            // empty path reports cancellation or a platform dialog failure.
            void Poll();

          private:
            static void SDLCALL OnResult(void* Userdata, const char* const* Filelist, int Filter);
            bool Begin(Handler HandlerValue);

            std::mutex Mutex_;
            std::optional<std::string> Result_;
            bool Done_ = false;
            bool Open_ = false;
            Handler Handler_;

            // Filter storage must outlive the SDL_Show*Dialog call.
            std::string FilterName_;
            std::string FilterPatterns_;
            SDL_DialogFileFilter Filter_{};
        };
    } // namespace core

} // namespace dbc::studio
