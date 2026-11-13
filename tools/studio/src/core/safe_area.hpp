#pragma once

#include <SDL3/SDL_video.h>

namespace dbc::studio
{
    namespace core
    {
        // The part of the window the Studio lays itself out in: clear of notches and of the system's
        // bars. False when the system gives none. On Android SDL also leaves out the bands of the
        // system's edge gestures, which would put a margin on every side: there the area comes from the
        // activity, and holds only what covers the window.
#if defined(DBC_STUDIO_ANDROID)
        bool WindowSafeArea(SDL_Window* Window, SDL_Rect& Area);
#else
        inline bool WindowSafeArea(SDL_Window* Window, SDL_Rect& Area)
        {
            return SDL_GetWindowSafeArea(Window, &Area);
        }
#endif
    } // namespace core

} // namespace dbc::studio
