#include "runtime_startup.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"

namespace dbc::interpreter
{
    RuntimeStartupResult InitializeProjectRuntime(EngineKernel& Kernel, const ProjectStartup& Startup)
    {
        std::string Error;
        if (!Kernel.Core().SetDisplayMode(Startup.Width, Startup.Height, Startup.Depth, 1, 0, 0, &Error))
            return {"DBC5000: Runtime startup failed: " + Error};
        if (!Kernel.Core().Init())
            return {"DBC5000: Failed to initialize the OpenDBC runtime"};
        if (!Kernel.Core().SetWindowTitle(Startup.Title, &Error))
            return {"DBC5000: Runtime startup failed: " + Error};
        if (Startup.WindowMode == ProjectWindowMode::Hidden && !Kernel.Core().HideWindow(&Error))
            return {"DBC5000: Runtime startup failed: " + Error};
        if ((Startup.WindowMode == ProjectWindowMode::Fullscreen ||
             Startup.WindowMode == ProjectWindowMode::FullDesktop) &&
            !Kernel.Core().SetWindowOff(&Error))
            return {"DBC5000: Runtime startup failed: " + Error};
        return {};
    }
} // namespace dbc::interpreter
