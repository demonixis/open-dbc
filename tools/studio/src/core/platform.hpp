#pragma once

namespace dbc::studio
{
    namespace core
    {
        // Platform capabilities of the single OpenDBC product. Sandboxed targets
        // use app-owned storage and in-app browsing.
        namespace platform
        {
            constexpr bool IsSandboxed()
            {
#if defined(DBC_STUDIO_SANDBOXED)
                return true;
#else
                return false;
#endif
            }

            // Covers iOS, iPadOS, visionOS ("Designed for iPad") and Mac Catalyst:
            // they are one target and behave the same as far as the studio cares.
            constexpr bool IsIos()
            {
#if defined(DBC_STUDIO_IOS)
                return true;
#else
                return false;
#endif
            }

            constexpr bool IsAndroid()
            {
#if defined(DBC_STUDIO_ANDROID)
                return true;
#else
                return false;
#endif
            }

            // SDL_dialog has cocoa, windows, unix and android backends, but nothing
            // for UIKit. On iOS and Catalyst the file tree is browsed in-app.
            constexpr bool HasNativeFileDialog()
            {
                return !IsSandboxed();
            }

            // A sandboxed build has no notion of "a folder anywhere on disk": the
            // workspace is a fixed set of storage roots the app owns.
            constexpr bool HasFreeformFileSystem()
            {
                return !IsSandboxed();
            }

            constexpr bool CanExportArtifacts()
            {
                return !IsSandboxed();
            }

            // Touch-sized hit targets and a bigger default font.
            constexpr float UiScaleHint()
            {
                return IsSandboxed() ? 1.6f : 1.0f;
            }
        } // namespace platform
    } // namespace core

} // namespace dbc::studio
