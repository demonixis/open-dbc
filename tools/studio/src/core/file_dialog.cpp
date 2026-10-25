#include "file_dialog.hpp"

namespace dbc::studio
{
    namespace core
    {
        void SDLCALL FileDialog::OnResult(void* Userdata, const char* const* Filelist, int /*filter*/)
        {
            auto* Self = static_cast<FileDialog*>(Userdata);

            std::lock_guard<std::mutex> Lock(Self->Mutex_);
            Self->Done_ = true;

            // filelist == nullptr means the dialog failed; an empty list means the
            // user cancelled. Both leave m_result unset.
            if (Filelist != nullptr && Filelist[0] != nullptr)
            {
                Self->Result_ = std::string(Filelist[0]);
            }
        }

        bool FileDialog::Begin(Handler HandlerValue)
        {
            if (Open_)
            {
                return false;
            }

            std::lock_guard<std::mutex> Lock(Mutex_);
            Open_ = true;
            Done_ = false;
            Result_.reset();
            Handler_ = std::move(HandlerValue);
            return true;
        }

        void FileDialog::OpenFile(SDL_Window* Parent, const std::string& DefaultLocation, const std::string& FilterName,
                                  const std::string& FilterPatterns, Handler HandlerValue)
        {
            if (!Begin(std::move(HandlerValue)))
            {
                return;
            }

            FilterName_ = FilterName;
            FilterPatterns_ = FilterPatterns;
            Filter_ = SDL_DialogFileFilter{FilterName_.c_str(), FilterPatterns_.c_str()};

            const bool HasFilter = !FilterPatterns_.empty();
            SDL_ShowOpenFileDialog(OnResult, this, Parent, HasFilter ? &Filter_ : nullptr, HasFilter ? 1 : 0,
                                   DefaultLocation.empty() ? nullptr : DefaultLocation.c_str(), false);
        }

        void FileDialog::SaveFile(SDL_Window* Parent, const std::string& DefaultLocation, const std::string& FilterName,
                                  const std::string& FilterPatterns, Handler HandlerValue)
        {
            if (!Begin(std::move(HandlerValue)))
            {
                return;
            }

            FilterName_ = FilterName;
            FilterPatterns_ = FilterPatterns;
            Filter_ = SDL_DialogFileFilter{FilterName_.c_str(), FilterPatterns_.c_str()};

            const bool HasFilter = !FilterPatterns_.empty();
            SDL_ShowSaveFileDialog(OnResult, this, Parent, HasFilter ? &Filter_ : nullptr, HasFilter ? 1 : 0,
                                   DefaultLocation.empty() ? nullptr : DefaultLocation.c_str());
        }

        void FileDialog::OpenFolder(SDL_Window* Parent, const std::string& DefaultLocation, Handler HandlerValue)
        {
            if (!Begin(std::move(HandlerValue)))
            {
                return;
            }

            SDL_ShowOpenFolderDialog(OnResult, this, Parent,
                                     DefaultLocation.empty() ? nullptr : DefaultLocation.c_str(), false);
        }

        void FileDialog::Poll()
        {
            Handler HandlerValue;
            std::optional<std::string> Result;

            {
                std::lock_guard<std::mutex> Lock(Mutex_);
                if (!Done_)
                {
                    return;
                }
                Done_ = false;
                Open_ = false;
                HandlerValue = std::move(Handler_);
                Result = std::move(Result_);
                Handler_ = nullptr;
                Result_.reset();
            }

            if (HandlerValue)
            {
                HandlerValue(Result.value_or(std::string()));
            }
        }
    } // namespace core

} // namespace dbc::studio
