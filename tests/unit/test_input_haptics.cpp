#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/input_haptic_mapping.hpp"
#include "engine/systems/input_system.hpp"

#include <doctest/doctest.h>

#include <SDL3/SDL_joystick.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace
{
    struct VirtualRumbleState
    {
        std::uint16_t Low = 0;
        std::uint16_t High = 0;
        int Calls = 0;
    };

    bool SDLCALL CaptureRumble(void* Userdata, Uint16 Low, Uint16 High)
    {
        auto& State = *static_cast<VirtualRumbleState*>(Userdata);
        State.Low = Low;
        State.High = High;
        ++State.Calls;
        return true;
    }
} // namespace

namespace
{
    dbc::detail::ForceAngleHapticParameters ReadForceAngleHapticParameters(int Magnitude, double Angle, int Duration)
    {
        dbc::detail::ForceAngleHapticParameters Result;
        std::string Error;
        REQUIRE(dbc::detail::MakeForceAngleHapticParameters(Magnitude, Angle, Duration, Result, &Error));
        return Result;
    }
    dbc::detail::ForceWaterHapticParameters ReadForceWaterHapticParameters(int Magnitude, int Duration)
    {
        dbc::detail::ForceWaterHapticParameters Result;
        std::string Error;
        REQUIRE(dbc::detail::MakeForceWaterHapticParameters(Magnitude, Duration, Result, &Error));
        return Result;
    }
    dbc::detail::ForcePeriodicHapticParameters ReadForcePeriodicHapticParameters(const char* Command, int Magnitude,
                                                                                 int Duration)
    {
        dbc::detail::ForcePeriodicHapticParameters Result;
        std::string Error;
        REQUIRE(dbc::detail::MakeForcePeriodicHapticParameters(Command, Magnitude, Duration, Result, &Error));
        return Result;
    }
} // namespace

TEST_CASE("DBC haptic effects retain the historical parameter mapping")
{
    using dbc::detail::MakeForceAngleHapticParameters;
    using dbc::detail::MakeForcePeriodicHapticParameters;
    using dbc::detail::MakeForceWaterHapticParameters;

    const auto FractionalAngle = ReadForceAngleHapticParameters(100, 12.345, 5);
    CHECK(FractionalAngle.Direction == 1235);
    CHECK(FractionalAngle.Level == std::numeric_limits<std::int16_t>::max());
    CHECK(FractionalAngle.Duration == 50);

    CHECK(ReadForceAngleHapticParameters(50, 360.0, 999).Direction == 0);
    CHECK(ReadForceAngleHapticParameters(50, 0.0, 999).Duration == 50);
    CHECK(ReadForceAngleHapticParameters(50, 0.0, 1000).Duration == 1050);
    CHECK(ReadForceAngleHapticParameters(50, 0.0, 0).Duration == std::numeric_limits<std::uint32_t>::max());

    const auto Water = ReadForceWaterHapticParameters(100, 250);
    CHECK(Water.Coefficient == std::numeric_limits<std::int16_t>::max() / 2);
    CHECK(Water.Saturation == std::numeric_limits<std::uint16_t>::max());
    CHECK(Water.Duration == 300);
    CHECK(ReadForceWaterHapticParameters(50, 0).Duration == 50);

    {
        dbc::detail::ForceAngleHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForceAngleHapticParameters(-1, 0.0, 1, Result, &Error));
        CHECK(Error == "FORCE ANGLE magnitude must be between zero and 100");
    }
    {
        dbc::detail::ForceAngleHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForceAngleHapticParameters(1, 360.01, 1, Result, &Error));
        CHECK(Error == "FORCE ANGLE angle must be between zero and 360 degrees");
    }
    {
        dbc::detail::ForceAngleHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForceAngleHapticParameters(1, 0.0, -1, Result, &Error));
        CHECK(Error == "FORCE ANGLE duration must be non-negative");
    }
    {
        dbc::detail::ForceWaterHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForceWaterHapticParameters(101, 1, Result, &Error));
        CHECK(Error == "FORCE WATER EFFECT magnitude must be between zero and 100");
    }
    {
        dbc::detail::ForceWaterHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForceWaterHapticParameters(1, -1, Result, &Error));
        CHECK(Error == "FORCE WATER EFFECT duration must be non-negative");
    }

    const auto Chainsaw = ReadForcePeriodicHapticParameters("FORCE CHAINSAW", 75, 250);
    CHECK(Chainsaw.Magnitude == 75 * std::numeric_limits<std::int16_t>::max() / 100);
    CHECK(Chainsaw.Duration == 250);
    CHECK(ReadForcePeriodicHapticParameters("FORCE IMPACT", 1, 0).Duration ==
          std::numeric_limits<std::uint32_t>::max());
    {
        dbc::detail::ForcePeriodicHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForcePeriodicHapticParameters("FORCE IMPACT", 101, 1, Result, &Error));
        CHECK(Error == "FORCE IMPACT magnitude must be between zero and 100");
    }
    {
        dbc::detail::ForcePeriodicHapticParameters Result;
        std::string Error;
        CHECK_FALSE(MakeForcePeriodicHapticParameters("FORCE CHAINSAW", 1, -1, Result, &Error));
        CHECK(Error == "FORCE CHAINSAW duration must be non-negative");
    }
}

TEST_CASE("Control device force feedback includes SDL rumble and haptic devices")
{
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_JoystickID Identifier = 0;
    VirtualRumbleState Rumble;
    {
        dbc::EngineKernel Kernel;
        REQUIRE(Kernel.Initialize("input-haptics", 16, 16));
        REQUIRE(Kernel.Core().EnsureInitialized());

        SDL_VirtualJoystickDesc Description{};
        SDL_INIT_INTERFACE(&Description);
        Description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        Description.naxes = 2;
        Description.nbuttons = 2;
        Description.name = "OpenDBC Virtual Force Controller";
        Description.userdata = &Rumble;
        Description.Rumble = CaptureRumble;
        Identifier = SDL_AttachVirtualJoystick(&Description);
        REQUIRE(Identifier != 0);

        std::string InputError;
        REQUIRE(Kernel.Input().SetControlDevice("OpenDBC Virtual Force Controller", 0, &InputError));
        const auto Devices = Kernel.Input().ControlDevices();
        const auto Iterator = std::find_if(Devices.begin(), Devices.end(), [](const dbc::ControlDeviceInfo& Device)
                                           { return Device.Name == "OpenDBC Virtual Force Controller"; });
        REQUIRE(Iterator != Devices.end());
        CHECK(Iterator->HasForceFeedback);
        CHECK(Kernel.Input().JoystickInformation(1) == 1);

        Kernel.Input().ForceShoot(100, 100);
        CHECK(Rumble.Calls == 1);
        CHECK(Rumble.Low == std::numeric_limits<std::uint16_t>::max());
        CHECK(Rumble.High == std::numeric_limits<std::uint16_t>::max());
        Kernel.Input().ForceChainsaw(50, 200, &InputError);
        CHECK(Rumble.Calls == 2);
        CHECK(Rumble.Low == std::numeric_limits<std::uint16_t>::max() / 2);
        Kernel.Input().ForceImpact(25, 300, &InputError);
        CHECK(Rumble.Calls == 3);
        Kernel.Input().ForceUp(100, &InputError);
        Kernel.Input().ForceDown(100, &InputError);
        Kernel.Input().ForceLeft(100, &InputError);
        Kernel.Input().ForceRight(100, &InputError);
        Kernel.Input().ForceAutoCenter(true);
        Kernel.Input().ForceAutoCenter(false);
        CHECK(Kernel.Input().ForceAngle(100, 45.5, 100, &InputError));
        CHECK(Kernel.Input().ForceWaterEffect(100, 100, &InputError));
        CHECK_NOTHROW(Kernel.Input().ForceNoEffect());
    }
    CHECK(SDL_DetachVirtualJoystick(Identifier));
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

TEST_CASE("Invalid input requests return diagnostics before creating runtime resources")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("invalid-input", 16, 16));
    std::string Error;
    CHECK_FALSE(Kernel.Input().PositionMouse(std::numeric_limits<int>::min(), 0, &Error));
    CHECK(Error.find("integer range") != std::string::npos);
    CHECK(Kernel.Core().Window() == nullptr);
    CHECK_FALSE(Kernel.Input().SetKeyStateToggle(256, 1, &Error));
    CHECK(Error.find("zero and 255") != std::string::npos);
    CHECK_FALSE(Kernel.Input().ForceAngle(101, 0, 1, &Error));
    CHECK(Error.find("magnitude") != std::string::npos);
    CHECK(Kernel.Core().Window() == nullptr);
    std::string Text = "unchanged";
    CHECK_FALSE(Kernel.Input().Entry(Text, 2, &Error));
    CHECK(Text == "unchanged");
}
