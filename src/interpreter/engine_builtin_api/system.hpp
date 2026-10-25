#pragma once

#include "../../engine/systems/core_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/input_system.hpp"
#include "../../engine/systems/text_system.hpp"
#include "../engine_builtin_context.hpp"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

namespace dbc
{
    inline int ChecklistQuantity(const interpreter::detail::BuiltinContext& Context)
    {
        return static_cast<int>(Context.Checklist.size());
    }

    inline std::string ChecklistString(const interpreter::detail::BuiltinContext& Context, int ItemNumber)
    {
        if (ItemNumber < 1 || ItemNumber > static_cast<int>(Context.Checklist.size()))
            return {};
        return Context.Checklist[static_cast<std::size_t>(ItemNumber - 1)].Name;
    }

    inline int ChecklistValueA(const interpreter::detail::BuiltinContext& Context, int ItemNumber)
    {
        if (ItemNumber < 1 || ItemNumber > static_cast<int>(Context.Checklist.size()))
            return 0;
        return Context.Checklist[static_cast<std::size_t>(ItemNumber - 1)].A;
    }

    inline int ChecklistValueB(const interpreter::detail::BuiltinContext& Context, int ItemNumber)
    {
        if (ItemNumber < 1 || ItemNumber > static_cast<int>(Context.Checklist.size()))
            return 0;
        return Context.Checklist[static_cast<std::size_t>(ItemNumber - 1)].B;
    }

    inline int ChecklistValueC(const interpreter::detail::BuiltinContext& Context, int ItemNumber)
    {
        if (ItemNumber < 1 || ItemNumber > static_cast<int>(Context.Checklist.size()))
            return 0;
        return Context.Checklist[static_cast<std::size_t>(ItemNumber - 1)].C;
    }

    inline int ChecklistValueD(const interpreter::detail::BuiltinContext& Context, int ItemNumber)
    {
        if (ItemNumber < 1 || ItemNumber > static_cast<int>(Context.Checklist.size()))
            return 0;
        return Context.Checklist[static_cast<std::size_t>(ItemNumber - 1)].D;
    }

    inline void DisableEscapekey(EngineKernel& K)
    {
        K.Core().DisableEscapekey();
    }

    inline void EmptyChecklist(interpreter::detail::BuiltinContext& Context)
    {
        Context.Checklist.clear();
    }

    inline void EnableEscapekey(EngineKernel& K)
    {
        K.Core().EnableEscapekey();
    }

    inline bool HideWindow(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().HideWindow(Error);
    }

    inline bool MaximizeWindow(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().MaximizeWindow(Error);
    }

    inline bool MinimizeWindow(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().MinimizeWindow(Error);
    }

    inline void PerformChecklistForGraphicsCards(interpreter::detail::BuiltinContext& Context)
    {
        Context.Checklist.clear();
        interpreter::detail::ChecklistItem Item;
        SDL_GPUDevice* Device = Context.Kernel->Gpu().GpuDevice();
        const SDL_PropertiesID Properties = Device ? SDL_GetGPUDeviceProperties(Device) : 0;
        const char* Name =
            Properties ? SDL_GetStringProperty(Properties, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr) : nullptr;
        const char* Driver = Device ? SDL_GetGPUDeviceDriver(Device) : nullptr;
        Item.Name = Name && *Name ? Name : (Driver && *Driver ? Driver : "none");
        Context.Checklist.push_back(std::move(Item));
    }

    inline void PerformChecklistForControlDevices(interpreter::detail::BuiltinContext& Context)
    {
        Context.Checklist.clear();
        for (ControlDeviceInfo Device : Context.Kernel->Input().ControlDevices())
        {
            interpreter::detail::ChecklistItem Item;
            Item.Name = std::move(Device.Name);
            Item.A = Device.HasForceFeedback ? 1 : 0;
            Item.B = Device.DuplicateIndex;
            Context.Checklist.push_back(std::move(Item));
        }
    }

    inline void PerformChecklistForFonts(interpreter::detail::BuiltinContext& Context)
    {
        Context.Checklist.clear();
        for (std::string Name : Context.Kernel->Text().AvailableFontNames())
        {
            interpreter::detail::ChecklistItem Item;
            Item.Name = std::move(Name);
            Context.Checklist.push_back(std::move(Item));
        }
    }

    inline void PerformChecklistForDisplayModes(interpreter::detail::BuiltinContext& Context)
    {
        Context.Checklist.clear();
        SDL_Window* Window = Context.Kernel->Core().Window();
        const SDL_DisplayID Display = Window ? SDL_GetDisplayForWindow(Window) : SDL_GetPrimaryDisplay();
        int Count = 0;
        SDL_DisplayMode** Modes = Display ? SDL_GetFullscreenDisplayModes(Display, &Count) : nullptr;
        std::vector<std::tuple<int, int, int>> Values;
        for (int Index = 0; Modes && Index < Count; ++Index)
        {
            const SDL_DisplayMode* Mode = Modes[Index];
            const int Depth = SDL_BITSPERPIXEL(Mode->format);
            if (Mode->w > 0 && Mode->h > 0 && Depth >= 16)
                Values.emplace_back(Mode->w, Mode->h, Depth);
        }
        SDL_free(Modes);
        if (Values.empty())
        {
            const SDL_DisplayMode* Desktop = Display ? SDL_GetDesktopDisplayMode(Display) : nullptr;
            if (Desktop && Desktop->w > 0 && Desktop->h > 0)
            {
                const int Depth = std::max(16, SDL_BITSPERPIXEL(Desktop->format));
                Values.emplace_back(Desktop->w, Desktop->h, Depth);
            }
        }
        std::sort(Values.begin(), Values.end());
        Values.erase(std::unique(Values.begin(), Values.end()), Values.end());
        for (const auto& [Width, Height, Depth] : Values)
        {
            interpreter::detail::ChecklistItem Item;
            Item.Name = std::to_string(Width) + "x" + std::to_string(Height) + "x" + std::to_string(Depth);
            Item.A = Width;
            Item.B = Height;
            Item.C = Depth;
            Context.Checklist.push_back(std::move(Item));
        }
    }

    inline bool RestoreWindow(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().RestoreWindow(Error);
    }

    inline bool SetWindowLayout(EngineKernel& K, int FullWindow, int TitleBar = 1, int Icon = 1,
                                std::string* Error = nullptr)
    {
        return K.Core().SetWindowLayout(FullWindow, TitleBar, Icon, Error);
    }

    inline bool SetWindowOff(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().SetWindowOff(Error);
    }

    inline bool SetWindowOn(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().SetWindowOn(Error);
    }

    inline bool SetWindowPosition(EngineKernel& K, int X, int Y, std::string* Error = nullptr)
    {
        return K.Core().SetWindowPosition(X, Y, Error);
    }

    inline bool SetWindowSize(EngineKernel& K, int Width, int Height, std::string* Error = nullptr)
    {
        return K.Core().SetWindowSize(Width, Height, Error);
    }

    inline bool SetWindowTitle(EngineKernel& K, const std::string& Title, std::string* Error = nullptr)
    {
        return K.Core().SetWindowTitle(Title, Error);
    }

    inline bool ShowWindow(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Core().ShowWindow(Error);
    }

} // namespace dbc
