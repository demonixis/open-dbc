#pragma once

#include <string>

namespace dbc::studio
{
    namespace core
    {
        // Makes the files shipped with the Studio readable as files under paths::ResourceDir(). An APK
        // holds them as assets, which only SDL can read: they are copied to the app's storage once per
        // build, listed by the `resources.list` asset and dated by `resources.stamp`. Elsewhere they
        // are files already. False with `Error` set when a copy fails.
        bool PrepareBundledResources(std::string& Error);
    } // namespace core

} // namespace dbc::studio
