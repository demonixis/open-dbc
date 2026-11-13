#pragma once

namespace dbc::studio
{
    namespace core
    {
        // How much of the bottom of the window the on-screen keyboard covers, in window points: 0 while
        // it is hidden, floating, or where there is none. SDL does not report it.
#if defined(DBC_STUDIO_IOS) || defined(DBC_STUDIO_ANDROID)
        float ScreenKeyboardHeight();
#else
        inline float ScreenKeyboardHeight()
        {
            return 0.0f;
        }
#endif
    } // namespace core

} // namespace dbc::studio
