#pragma once

struct SDL_Window;

namespace dbc::studio
{
    namespace core
    {
        // A program running in the Studio's window covers the Studio and its Stop button, and the Studio
        // cannot draw over it: the window belongs to the program's GPU device. This is a button of the
        // system's own, in the window's top right corner, that asks for the program to stop. Show it once
        // the program has the window: taking it puts the program's view in front of everything there.
#if defined(DBC_STUDIO_IOS) || defined(DBC_STUDIO_ANDROID)
        void ShowStopButton(SDL_Window* Window);
        void HideStopButton();
        // Whether the button was pressed since the last call.
        bool TakeStopRequest();
#else
        inline void ShowStopButton(SDL_Window*) {}

        inline void HideStopButton() {}

        inline bool TakeStopRequest()
        {
            return false;
        }
#endif
    } // namespace core

} // namespace dbc::studio
