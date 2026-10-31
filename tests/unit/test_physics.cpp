#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/physics_system.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace dbc::interpreter::detail;
using dbc::language::SymbolId;

namespace
{
    // Drives the physics forms the way a program does, through their catalogue symbols.
    class OdeScene
    {
      public:
        OdeScene()
        {
            REQUIRE(Kernel.Initialize("test", 1, 1));
            Context.Kernel = &Kernel;
            Context.Profile = dbc::language::LanguageProfile::DbPro;
            REQUIRE(Run(SymbolId::Command_ExtOdeStart));
        }

        bool Run(SymbolId Symbol, std::initializer_list<double> Arguments = {})
        {
            Error.clear();
            return InvokeCommand(Context, Symbol, Values(Arguments), Error);
        }

        double Read(SymbolId Symbol, std::initializer_list<double> Arguments = {})
        {
            Error.clear();
            BuiltinValue Result;
            REQUIRE_MESSAGE(InvokeFunction(Context, Symbol, Values(Arguments), Result, Error), Error);
            return Result.AsNumber();
        }

        void Step(int Count = 1)
        {
            for (int Index = 0; Index < Count; ++Index)
                REQUIRE_MESSAGE(Run(SymbolId::Command_ExtOdeUpdate), Error);
        }

        // Advances by whole updates of a given length, as a program running at that frame rate does.
        void StepAtRate(double UpdatesPerSecond, double Seconds)
        {
            const int Count = static_cast<int>(std::lround(UpdatesPerSecond * Seconds));
            for (int Index = 0; Index < Count; ++Index)
                REQUIRE_MESSAGE(Run(SymbolId::Command_ExtOdeUpdate, {1.0 / UpdatesPerSecond}), Error);
        }

        void Floor(int ObjectId = 1)
        {
            Kernel.Objects().MakeObjectBox(ObjectId, 40.0f, 2.0f, 40.0f);
            Kernel.Objects().PositionObject(ObjectId, 0.0f, -1.0f, 0.0f);
            REQUIRE_MESSAGE(Run(SymbolId::Command_ExtOdeCreateStaticBox, {static_cast<double>(ObjectId)}), Error);
        }

        void DynamicSphere(int ObjectId, float Y, float Diameter = 2.0f)
        {
            Kernel.Objects().MakeObjectSphere(ObjectId, Diameter, 8, 8);
            Kernel.Objects().PositionObject(ObjectId, 0.0f, Y, 0.0f);
            REQUIRE_MESSAGE(Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {static_cast<double>(ObjectId)}), Error);
        }

        float X(int ObjectId)
        {
            return Kernel.Objects().ObjectPositionX(ObjectId);
        }

        float Y(int ObjectId)
        {
            return Kernel.Objects().ObjectPositionY(ObjectId);
        }

        float Z(int ObjectId)
        {
            return Kernel.Objects().ObjectPositionZ(ObjectId);
        }

        dbc::EngineKernel Kernel;
        BuiltinContext Context;
        std::string Error;

      private:
        static std::vector<BuiltinValue> Values(std::initializer_list<double> Arguments)
        {
            std::vector<BuiltinValue> Result;
            for (const double Value : Arguments)
                Result.push_back(BuiltinValue::FromNumber(Value));
            return Result;
        }
    };
} // namespace

TEST_CASE("ODE stepping is a fixed timestep whatever the update rate")
{
    SUBCASE("The same time simulates the same motion at any rate")
    {
        const auto FallFor = [](double UpdatesPerSecond)
        {
            OdeScene Scene;
            Scene.DynamicSphere(2, 100.0f);
            Scene.StepAtRate(UpdatesPerSecond, 1.0);
            return Scene.Y(2);
        };
        const float At60 = FallFor(60.0);
        CHECK(At60 == doctest::Approx(100.0f - 0.5f * 9.81f).epsilon(0.02));
        CHECK(FallFor(30.0) == doctest::Approx(At60).epsilon(1.0e-4));
        CHECK(FallFor(120.0) == doctest::Approx(At60).epsilon(1.0e-4));
        CHECK(FallFor(10.0) == doctest::Approx(At60).epsilon(1.0e-4));
    }

    SUBCASE("Updates shorter than a step extrapolate what the object shows")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 10.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 6, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 240.0}));
        CHECK(Scene.X(2) == doctest::Approx(6.0f / 240.0f).epsilon(0.01));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 240.0}));
        CHECK(Scene.X(2) == doctest::Approx(12.0f / 240.0f).epsilon(0.01));
    }

    SUBCASE("A single update cannot run away with the simulation")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 1000.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {50.0}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) ==
              doctest::Approx(-9.81f).epsilon(0.02));
    }

    SUBCASE("Without an argument an update takes the real frame time, or one step without a frame")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 1000.0f);
        Scene.Step();
        const double OneStep = Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2});
        CHECK(OneStep == doctest::Approx(-9.81 / 60.0).epsilon(0.02));
        Scene.Step();
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) ==
              doctest::Approx(2.0 * OneStep).epsilon(0.02));

        // A presented frame long after the last update is clamped to the slowest supported frame rate.
        SDL_Delay(250);
        Scene.Kernel.Core().LastRenderTicks() += 1;
        Scene.Step();
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) ==
              doctest::Approx(8.0 * OneStep).epsilon(0.03));
    }

    SUBCASE("Bad durations are rejected")
    {
        OdeScene Scene;
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {0.0}));
        CHECK(Scene.Error.find("greater than zero") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {-1.0}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {std::numeric_limits<double>::quiet_NaN()}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0e300}));
    }
}

TEST_CASE("ODE bodies and objects agree on position and rotation")
{
    SUBCASE("Rotation reaches the object in degrees and static bodies leave theirs alone")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 3.0f, 4.0f, 5.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {2}));
        Scene.DynamicSphere(3, 50.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {3, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetAngularVelocity, {3, 0, std::numbers::pi / 2.0, 0}));
        Scene.Step(30);
        CHECK(Scene.Kernel.Objects().ObjectAngleY(3) == doctest::Approx(45.0f).epsilon(0.02));
        CHECK(Scene.X(2) == doctest::Approx(3.0f));
        CHECK(Scene.Y(2) == doctest::Approx(4.0f));
        CHECK(Scene.Z(2) == doctest::Approx(5.0f));
    }

    SUBCASE("Both object rotation orders survive a round trip through the body")
    {
        for (const bool Zyx : {false, true})
        {
            CAPTURE(Zyx);
            OdeScene Scene;
            Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
            if (Zyx)
                REQUIRE(Scene.Kernel.Objects().SetObjectRotationZyx(2));
            Scene.Kernel.Objects().RotateObject(2, 20.0f, 35.0f, 50.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {2}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            Scene.Step();
            CHECK(Scene.Kernel.Objects().ObjectAngleX(2) == doctest::Approx(20.0f));
            CHECK(Scene.Kernel.Objects().ObjectAngleY(2) == doctest::Approx(35.0f));
            CHECK(Scene.Kernel.Objects().ObjectAngleZ(2) == doctest::Approx(50.0f));
        }
    }

    SUBCASE("The body turns in the order its object turns")
    {
        // A rod along X, turned about X and then Z: Rz * Rx stands it along Y, Rx * Rz lays it along Z.
        const auto RodHeight = [](bool Zyx)
        {
            OdeScene Scene;
            Scene.Kernel.Objects().MakeObjectBox(2, 8.0f, 1.0f, 1.0f);
            if (Zyx)
                REQUIRE(Scene.Kernel.Objects().SetObjectRotationZyx(2));
            Scene.Kernel.Objects().RotateObject(2, 90.0f, 0.0f, 90.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {2}));
            return Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2});
        };
        CHECK(RodHeight(false) == doctest::Approx(8.0).epsilon(0.01));
        CHECK(RodHeight(true) == doctest::Approx(1.0).epsilon(0.01));
    }

    SUBCASE("Scale is part of the shape")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(1, 10.0f, 1.0f, 10.0f);
        Scene.Kernel.Objects().ScaleObject(1, 400.0f, 400.0f, 400.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {1}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {1}) == doctest::Approx(4.0));
        // 15 lies inside the scaled box, but off the unscaled one.
        Scene.DynamicSphere(2, 6.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {2, 15, 6, 0}));
        Scene.Step(180);
        CHECK(Scene.Y(2) == doctest::Approx(3.0f).epsilon(0.02));
    }

    SUBCASE("A mesh off its object's origin collides where it is drawn")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(1, 40.0f, 2.0f, 40.0f);
        std::string LimbError;
        REQUIRE_MESSAGE(Scene.Kernel.Objects().OffsetLimb(1, 0, 0.0f, 10.0f, 0.0f, 0, &LimbError), LimbError);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {1}));
        Scene.DynamicSphere(2, 20.0f);
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(12.0f).epsilon(0.02));
    }

    SUBCASE("Height follows the body's rotation")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(2, 2.0f, 6.0f, 2.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {2}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2}) == doctest::Approx(6.0));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyRotation, {2, 0, 0, 90}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2}) == doctest::Approx(2.0).epsilon(0.01));
    }

    SUBCASE("A flat object still makes a valid collision box")
    {
        OdeScene Scene;
        std::string PlainError;
        REQUIRE_MESSAGE(Scene.Kernel.Objects().MakeObjectPlain(1, 40.0f, 40.0f, 0, &PlainError), PlainError);
        Scene.Kernel.Objects().RotateObject(1, 90.0f, 0.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {1}));
        Scene.DynamicSphere(2, 5.0f);
        Scene.Step(180);
        CHECK(Scene.Y(2) == doctest::Approx(1.0f).epsilon(0.03));
    }
}

TEST_CASE("ODE bodies follow the life of their objects and of the world")
{
    SUBCASE("Deleting an object removes its body, even with the same number made again")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DynamicSphere(2, 10.0f);
        Scene.Step();
        REQUIRE(Scene.Run(SymbolId::Command_DeleteObject, {2}));
        Scene.Kernel.Objects().MakeObjectSphere(2, 2.0f, 8, 8);
        Scene.Kernel.Objects().PositionObject(2, 3.0f, 30.0f, 0.0f);
        Scene.Step(10);
        // The new object has no body yet: nothing moves it and nothing answers for it.
        CHECK(Scene.X(2) == doctest::Approx(3.0f));
        CHECK(Scene.Y(2) == doctest::Approx(30.0f));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0, 1, 0}));
        CHECK(Scene.Error.find("requires an ODE body") != std::string::npos);
    }

    SUBCASE("A range deletion removes every body in it")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 10.0f);
        Scene.DynamicSphere(3, 20.0f);
        Scene.DynamicSphere(4, 30.0f);
        REQUIRE(Scene.Run(SymbolId::Command_DeleteObjects, {2, 3}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {3, 0}));
        CHECK(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {4, 0}));
    }

    SUBCASE("An object deleted another way leaves no ghost in the world")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DynamicSphere(2, 2.0f);
        Scene.Step(180);
        const float Resting = Scene.Y(2);
        CHECK(Resting == doctest::Approx(1.0f).epsilon(0.03));

        Scene.Kernel.Objects().DeleteObject(1);
        Scene.Step(60);
        CHECK(Scene.Y(2) < Resting - 1.0f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetBodyFriction, {1, 0.5}));
    }

    SUBCASE("Removing the support wakes what slept on it")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DynamicSphere(2, 1.5f);
        Scene.Step(600);
        const float Resting = Scene.Y(2);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeDestroyObject, {1}));
        Scene.Step(60);
        CHECK(Scene.Y(2) < Resting - 1.0f);
    }

    SUBCASE("Creating a body again replaces the old one")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 50.0f);
        Scene.Step(30);
        const float Y = Scene.Y(2);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {2}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) == doctest::Approx(0.0));
        Scene.Step();
        CHECK(Scene.Y(2) == doctest::Approx(Y - 9.81f / 3600.0f).epsilon(0.001));
    }

    SUBCASE("Starting again gives an empty world")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DynamicSphere(2, 2.0f);
        Scene.Step(10);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStart));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetBodyFriction, {1, 0.5}));
        const float Y = Scene.Y(2);
        Scene.Step(30);
        CHECK(Scene.Y(2) == doctest::Approx(Y));

        // The old floor no longer exists for a new body.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {2}));
        Scene.Step(120);
        CHECK(Scene.Y(2) < 0.0f);
    }

    SUBCASE("Ending the world leaves the forms asking for ODE START")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 2.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeEnd));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0, 1, 0}));
        CHECK(Scene.Error.find("ODE START") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeDestroyObject, {2}));
        CHECK(Scene.Error.find("ODE START") != std::string::npos);
    }
}

TEST_CASE("ODE forms report wrong use by name")
{
    OdeScene Scene;
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {99}));
    CHECK(Scene.Error == "ODE CREATE STATIC BOX requires object 99");
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticSphere, {99}));
    CHECK(Scene.Error == "ODE CREATE STATIC SPHERE requires object 99");
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {99}));
    CHECK(Scene.Error == "ODE CREATE DYNAMIC SPHERE requires object 99");

    // As in the plugin, destroying nothing is not an error.
    CHECK(Scene.Run(SymbolId::Command_ExtOdeDestroyObject, {99}));

    Scene.Floor();
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {1, 0, 1, 0}));
    CHECK(Scene.Error.find("is static") != std::string::npos);
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetActive, {1, 1}));
    CHECK(Scene.Error.find("is static") != std::string::npos);

    Scene.DynamicSphere(2, 5.0f);
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {2, 1.0e300, 0, 0}));
    CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetBodyFriction, {2, -1}));
    CHECK(Scene.Error.find("non-negative") != std::string::npos);
}

TEST_CASE("ODE setters wake bodies that have gone to sleep")
{
    const auto SleepingSphere = [](OdeScene& Scene)
    {
        Scene.Floor();
        Scene.DynamicSphere(2, 1.1f);
        Scene.Step(600);
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) == doctest::Approx(0.0));
    };

    SUBCASE("Linear velocity")
    {
        OdeScene Scene;
        SleepingSphere(Scene);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0, 5, 0}));
        Scene.Step(10);
        CHECK(Scene.Y(2) > 1.5f);
    }

    SUBCASE("World gravity")
    {
        OdeScene Scene;
        SleepingSphere(Scene);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldGravity, {0, 20, 0}));
        Scene.Step(30);
        CHECK(Scene.Y(2) > 2.0f);
    }

    SUBCASE("Body gravity, switched off and on again")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 50.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        Scene.Step(30);
        CHECK(Scene.Y(2) == doctest::Approx(50.0f));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 1}));
        Scene.Step(30);
        CHECK(Scene.Y(2) < 49.0f);
    }

    SUBCASE("Disabling the floor drops what rests on it")
    {
        OdeScene Scene;
        SleepingSphere(Scene);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetEnabled, {1, 0}));
        Scene.Step(30);
        CHECK(Scene.Y(2) < 0.0f);
    }

    SUBCASE("A disabled body falls through, and a deactivated one stays put until woken")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DynamicSphere(2, 2.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetEnabled, {2, 0}));
        Scene.Step(60);
        CHECK(Scene.Y(2) < 0.0f);

        Scene.DynamicSphere(3, 50.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetActive, {3, 0}));
        Scene.Step(30);
        CHECK(Scene.Y(3) == doctest::Approx(50.0f));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetActive, {3, 1}));
        Scene.Step(30);
        CHECK(Scene.Y(3) < 49.0f);
    }
}
