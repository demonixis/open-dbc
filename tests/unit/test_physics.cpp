#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/physics_system.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
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

        // What the engine does beyond the ODE forms, which no command reaches yet.
        bool AddTorque(int ObjectId, float X, float Y, float Z)
        {
            Error.clear();
            return Kernel.Physics().AddTorque(ObjectId, X, Y, Z, &Error);
        }

        bool AddImpulse(int ObjectId, float X, float Y, float Z)
        {
            Error.clear();
            return Kernel.Physics().AddImpulse(ObjectId, X, Y, Z, &Error);
        }

        bool SetBodyKinematic(int ObjectId, int Kinematic)
        {
            Error.clear();
            return Kernel.Physics().SetBodyKinematic(ObjectId, Kinematic, &Error);
        }

        bool SetBodyDamping(int ObjectId, float Linear, float Angular)
        {
            Error.clear();
            return Kernel.Physics().SetBodyDamping(ObjectId, Linear, Angular, &Error);
        }

        double AngularVelocity(int ObjectId, int Axis)
        {
            Error.clear();
            float Output = 0.0f;
            REQUIRE_MESSAGE(Kernel.Physics().AngularVelocity(Output, ObjectId, Axis, &Error), Error);
            return Output;
        }

        const dbc::PhysicsCollisionMessage& Contact()
        {
            Error.clear();
            const dbc::PhysicsCollisionMessage* Message = Kernel.Physics().CollisionMessage(&Error);
            REQUIRE_MESSAGE(Message != nullptr, Error);
            return *Message;
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

        // Turns about X and keeps the initial turn about Y a plain may be made with, as XROTATE OBJECT does.
        void PlainFacingUp(int ObjectId, float XDegrees)
        {
            std::string ObjectError;
            REQUIRE_MESSAGE(Kernel.Objects().MakeObjectPlain(ObjectId, 40.0f, 40.0f, 0, &ObjectError), ObjectError);
            Kernel.Objects().RotateObject(ObjectId, XDegrees, Kernel.Objects().ObjectAngleY(ObjectId), 0.0f);
        }

        void DropSphere(int ObjectId, float SphereX, float SphereY, float Diameter = 2.0f)
        {
            Kernel.Objects().MakeObjectSphere(ObjectId, Diameter, 8, 8);
            Kernel.Objects().PositionObject(ObjectId, SphereX, SphereY, 0.0f);
            REQUIRE_MESSAGE(Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {static_cast<double>(ObjectId)}), Error);
        }

        // Limb 0 is a box 4 wide and 2 tall around the origin, limb 1 the same box ten units along X.
        void TwoLimbObject(int ObjectId)
        {
            std::string ObjectError;
            Kernel.Objects().MakeObjectBox(ObjectId, 4.0f, 2.0f, 4.0f);
            REQUIRE_MESSAGE(Kernel.Objects().MakeMeshFromObject(50, ObjectId, 0, &ObjectError), ObjectError);
            REQUIRE_MESSAGE(Kernel.Objects().AddLimb(ObjectId, 1, 50, &ObjectError), ObjectError);
            REQUIRE_MESSAGE(Kernel.Objects().OffsetLimb(ObjectId, 1, 10.0f, 0.0f, 0.0f, 0, &ObjectError), ObjectError);
        }

        // The lowest vertex of an upright cone made centred on its object, relative to the object.
        [[nodiscard]] float ObjectMinimumY(int ObjectId)
        {
            return -0.5f * Kernel.Objects().ObjectSizeY(ObjectId);
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

TEST_CASE("ODE boxes measure their objects or take explicit sizes")
{
    SUBCASE("A box takes its object's size and lands on its lowest face")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectBox(2, 2.0f, 4.0f, 6.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 10.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2}) == doctest::Approx(4.0));
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(2.0f).epsilon(0.02));
    }

    SUBCASE("A collision box set on the object replaces its vertices")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 10.0f, 0.0f);
        REQUIRE(Scene.Kernel.Objects().MakeObjectCollisionBox(2, -1.0f, -3.0f, -1.0f, 1.0f, 3.0f, 1.0f, 0));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2}) == doctest::Approx(6.0));
    }

    SUBCASE("An explicit size overrides the object's")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 1.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 10.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, 4, 6, 4}));
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(3.0f).epsilon(0.02));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, 4, 0, 4}));
        CHECK(Scene.Error.find("positive") != std::string::npos);
    }

    SUBCASE("A manual box places its object and its own shape")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 1.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBoxManual, {2, 3, 10, -2, 6, 2, 2, 0, 0, 90}));
        CHECK(Scene.X(2) == doctest::Approx(3.0f));
        CHECK(Scene.Y(2) == doctest::Approx(10.0f));
        CHECK(Scene.Z(2) == doctest::Approx(-2.0f));
        CHECK(Scene.Kernel.Objects().ObjectAngleZ(2) == doctest::Approx(90.0f));
        // Turned on its end, the long side stands up.
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(3.0f).epsilon(0.02));
    }

    SUBCASE("Weight, friction and bounce use the plugin's scales, with -1 for the default")
    {
        const auto SpeedAfterPush = [](double Weight)
        {
            OdeScene Scene;
            Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, 0, Weight, -1, -1}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            REQUIRE(Scene.AddImpulse(2, 3, 0, 0));
            return Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2});
        };
        CHECK(SpeedAfterPush(-1) == doctest::Approx(3.0));
        CHECK(SpeedAfterPush(100) == doctest::Approx(3.0));
        CHECK(SpeedAfterPush(300) == doctest::Approx(1.0));

        const auto SlideAfter = [](double Friction)
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
            Scene.Kernel.Objects().PositionObject(2, 0.0f, 1.0f, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, 0, -1, Friction, -1}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyFriction, {1, 1}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 4, 0, 0}));
            Scene.Step(60);
            return Scene.X(2);
        };
        CHECK(SlideAfter(0) > 3.5f);
        CHECK(SlideAfter(100) < 1.0f);

        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 6.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, 0, -1, -1, 100}));
        // Fall for a second to the floor, then expect to be most of the way back up.
        Scene.Step(90);
        CHECK(Scene.Y(2) > 3.0f);
    }

    SUBCASE("Tip-over modes keep a box from settling flat, and mode 11 only reports contacts")
    {
        const auto RestingAngle = [](double Mode)
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
            Scene.Kernel.Objects().PositionObject(2, 0.0f, 3.0f, 0.0f);
            // The slightest lean decides which way the plate falls.
            Scene.Kernel.Objects().RotateObject(2, 0.0f, 0.0f, 2.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, Mode}));
            Scene.Step(600);
            return std::abs(Scene.Kernel.Objects().ObjectAngleX(2)) + std::abs(Scene.Kernel.Objects().ObjectAngleZ(2));
        };
        CHECK(RestingAngle(0) < 1.0f);
        CHECK(RestingAngle(1) > 5.0f);

        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 3.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, 11}));
        Scene.Step(120);
        CHECK(Scene.Y(2) < -2.0f);

        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, 5}));
        CHECK(Scene.Error.find("tip-over") != std::string::npos);
    }

    SUBCASE("A limb's box measures that limb only")
    {
        OdeScene Scene;
        Scene.TwoLimbObject(1);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBoxLimb, {1, 1}));
        Scene.DropSphere(2, 10.0f, 8.0f);
        Scene.DropSphere(3, 0.0f, 8.0f);
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(2.0f).epsilon(0.02));
        CHECK(Scene.Y(3) < -5.0f);

        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBoxLimb, {1, 9}));
        CHECK(Scene.Error == "ODE CREATE STATIC BOX LIMB requires limb 9 of object 1");
    }
}

TEST_CASE("ODE cylinders and capsules behave like their shapes")
{
    SUBCASE("A cylinder stands at half its height and rolls when laid down")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCylinder(2, 2.0f, 6.0f, 16);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 8.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicCylinder, {2}));
        const double Height = Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2});
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(0.5 * Height).epsilon(0.03));

        // The object's axis along Z: a push along X has to roll it about Z.
        Scene.Kernel.Objects().MakeObjectCylinder(3, 2.0f, 6.0f, 16);
        Scene.Kernel.Objects().PositionObject(3, -10.0f, 2.0f, 10.0f);
        Scene.Kernel.Objects().RotateObject(3, 90.0f, 0.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicCylinder, {3}));
        Scene.Step(30);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {3, 3, 0, 0}));
        Scene.Step(60);
        // Friction leaves two thirds of the push once it rolls without slipping, at speed over radius.
        CHECK(Scene.AngularVelocity(3, 2) == doctest::Approx(-1.0).epsilon(0.05));
        CHECK(Scene.X(3) > -8.0f);
    }

    SUBCASE("A static cylinder takes its own place and size, with radians, and leaves its object")
    {
        const auto RestingY = [](double XRadians)
        {
            OdeScene Scene;
            Scene.Kernel.Objects().MakeObjectCube(1, 1.0f);
            Scene.Kernel.Objects().PositionObject(1, 40.0f, 40.0f, 40.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticCylinder, {1, 0, 0, 0, 40, 10, 40, XRadians, 0, 0}));
            CHECK(Scene.Y(1) == doctest::Approx(40.0f));
            Scene.DropSphere(2, 0.0f, 50.0f);
            Scene.Step(420);
            return Scene.Y(2);
        };
        CHECK(RestingY(0.0) == doctest::Approx(6.0f).epsilon(0.03));
        // Laid on its side, the top of the rounded surface is at its radius.
        CHECK(RestingY(std::numbers::pi / 2.0) == doctest::Approx(21.0f).epsilon(0.03));
    }

    SUBCASE("A dynamic capsule stands on the object's lowest point and never turns")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectBox(2, 2.0f, 6.0f, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 8.0f, 0.0f);
        Scene.Kernel.Objects().RotateObject(2, 0.0f, 45.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicCapsule, {2, 0, 0}));
        Scene.Step(300);
        CHECK(Scene.Y(2) == doctest::Approx(3.0f).epsilon(0.02));

        // The program keeps turning what the object shows.
        Scene.Kernel.Objects().RotateObject(2, 0.0f, 120.0f, 0.0f);
        REQUIRE(Scene.AddImpulse(2, 6, 0, 0));
        Scene.Step(30);
        CHECK(Scene.X(2) > 0.5f);
        CHECK(Scene.Kernel.Objects().ObjectAngleY(2) == doctest::Approx(120.0f));
        CHECK(Scene.AngularVelocity(2, 2) == doctest::Approx(0.0));

        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicCapsule, {2, -1, 0}));
        CHECK(Scene.Error.find("scale modifier") != std::string::npos);
    }

    SUBCASE("The scale modifier and the raised height shape the capsule")
    {
        const auto Capsule = [](double Modifier, double Raised)
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.Kernel.Objects().MakeObjectBox(2, 2.0f, 6.0f, 2.0f);
            Scene.Kernel.Objects().PositionObject(2, 0.0f, 8.0f, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicCapsule, {2, Modifier, Raised}));
            const double Height = Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2});
            Scene.Step(300);
            return std::pair<double, float>(Height, Scene.Y(2));
        };
        const auto Plain = Capsule(1, 0);
        // A cylinder of 3.3 and hemispheres of radius 0.967.
        CHECK(Plain.first == doctest::Approx(5.234).epsilon(0.01));
        CHECK(Capsule(2, 0).first == doctest::Approx(8.534).epsilon(0.01));
        CHECK(Capsule(1, 2).second == doctest::Approx(Plain.second - 2.0f).epsilon(0.02));
    }

    SUBCASE("A static capsule holds what lands on it")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(1, 2.0f, 6.0f, 2.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticCapsule, {1}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {1}) == doctest::Approx(5.234).epsilon(0.01));
        Scene.DropSphere(2, 0.0f, 6.0f);
        Scene.Step(90);
        // The capsule's foot is at the object's lowest point, 3 below its origin; the sphere's radius is 1.
        CHECK(Scene.Y(2) == doctest::Approx(-3.0f + 5.234f + 1.0f).epsilon(0.03));
    }
}

TEST_CASE("ODE triangle meshes are the real surface of their objects")
{
    SUBCASE("A static mesh has the gaps its bounds would hide")
    {
        OdeScene Scene;
        Scene.TwoLimbObject(1);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1}));
        Scene.DropSphere(2, 0.0f, 8.0f);
        Scene.DropSphere(3, 10.0f, 8.0f);
        Scene.DropSphere(4, 5.0f, 8.0f);
        Scene.Step(300);
        CHECK(Scene.Y(2) == doctest::Approx(2.0f).epsilon(0.02));
        CHECK(Scene.Y(3) == doctest::Approx(2.0f).epsilon(0.02));
        CHECK(Scene.Y(4) < -5.0f);
    }

    SUBCASE("Every side of a closed mesh is solid from outside")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(1, 4.0f, 4.0f, 4.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1}));

        struct Approach
        {
            double X, Y, Z;
        };

        int ObjectId = 2;
        for (const Approach Direction : {Approach{1, 0, 0}, Approach{-1, 0, 0}, Approach{0, 1, 0}, Approach{0, -1, 0},
                                         Approach{0, 0, 1}, Approach{0, 0, -1}})
        {
            CAPTURE(ObjectId);
            Scene.Kernel.Objects().MakeObjectSphere(ObjectId, 1.0f, 8, 8);
            Scene.Kernel.Objects().PositionObject(ObjectId, static_cast<float>(5.0 * Direction.X),
                                                  static_cast<float>(5.0 * Direction.Y),
                                                  static_cast<float>(5.0 * Direction.Z));
            const double Id = ObjectId;
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {Id}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {Id, 0}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity,
                              {Id, -6.0 * Direction.X, -6.0 * Direction.Y, -6.0 * Direction.Z}));
            Scene.Step(40);
            const double Along =
                Scene.X(ObjectId) * Direction.X + Scene.Y(ObjectId) * Direction.Y + Scene.Z(ObjectId) * Direction.Z;
            CHECK(Along == doctest::Approx(2.5).epsilon(0.03));
            ++ObjectId;
        }
    }

    SUBCASE("The object's place, turn and scale reach the mesh")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(1, 20.0f, 1.0f, 20.0f);
        Scene.Kernel.Objects().ScaleObject(1, 200.0f, 200.0f, 200.0f);
        Scene.Kernel.Objects().PositionObject(1, 5.0f, 3.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTerrainMesh, {1}));
        Scene.DropSphere(2, 20.0f, 10.0f);
        Scene.Step(240);
        CHECK(Scene.Y(2) == doctest::Approx(5.0f).epsilon(0.02));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {1}) == doctest::Approx(2.0).epsilon(0.01));
    }

    SUBCASE("A flat mesh collides on its visible side")
    {
        OdeScene Scene;
        Scene.PlainFacingUp(1, 270.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTerrainMesh, {1}));
        Scene.DropSphere(2, 0.0f, 5.0f);
        Scene.Step(180);
        CHECK(Scene.Y(2) == doctest::Approx(1.0f).epsilon(0.03));

        // Its back is open, as Jolt's simulation ignores the back of a triangle.
        OdeScene Flipped;
        Flipped.PlainFacingUp(1, 90.0f);
        REQUIRE(Flipped.Run(SymbolId::Command_ExtOdeCreateStaticTerrainMesh, {1}));
        Flipped.DropSphere(2, 0.0f, 5.0f);
        Flipped.Step(180);
        CHECK(Flipped.Y(2) < -5.0f);
    }

    SUBCASE("A limb alone, a limb with scaling, and bad arguments")
    {
        {
            OdeScene Scene;
            Scene.TwoLimbObject(1);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMeshLimb, {1, 0}));
            Scene.DropSphere(2, 0.0f, 8.0f);
            Scene.DropSphere(3, 10.0f, 8.0f);
            Scene.Step(240);
            CHECK(Scene.Y(2) == doctest::Approx(2.0f).epsilon(0.02));
            CHECK(Scene.Y(3) < -5.0f);
        }
        {
            OdeScene Scene;
            Scene.TwoLimbObject(1);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1, 1, 200}));
            CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {1}) == doctest::Approx(4.0).epsilon(0.01));
            // Scaling about the object's origin doubles the limb's distance too.
            Scene.DropSphere(2, 20.0f, 8.0f);
            Scene.DropSphere(3, 0.0f, 8.0f);
            Scene.Step(240);
            CHECK(Scene.Y(2) == doctest::Approx(3.0f).epsilon(0.02));
            CHECK(Scene.Y(3) < -5.0f);

            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1, -1, 100, 0}));
            CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1, -1, 0, 0}));
            CHECK(Scene.Error.find("positive") != std::string::npos);
            CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMeshLimb, {1, 7}));
            CHECK(Scene.Error == "ODE CREATE STATIC TRIANGLE MESH LIMB requires limb 7 of object 1");
        }
    }

    SUBCASE("A body too fast for one step still cannot cross a mesh")
    {
        OdeScene Scene;
        Scene.PlainFacingUp(1, 270.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1}));
        Scene.DropSphere(2, 0.0f, 20.0f, 0.5f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0, -500, 0}));
        Scene.Step(30);
        CHECK(Scene.Y(2) > 0.0f);
    }

    SUBCASE("A moving mesh is its object's convex hull")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCone(2, 4.0f, 8.0f, 12);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 12.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicTriangleMesh, {2}));
        const double Height = Scene.Read(SymbolId::Function_ExtOdeGetBodyHeight, {2});
        CHECK(Height == doctest::Approx(8.0).epsilon(0.02));
        Scene.Step(300);
        // It lands on the base, however far that lies from the object's origin.
        const float Lowest = Scene.Y(2) + Scene.ObjectMinimumY(2);
        CHECK(Lowest == doctest::Approx(0.0f).epsilon(0.05));
        CHECK(std::abs(Scene.Kernel.Objects().ObjectAngleX(2)) < 1.0f);

        // A flat object has no volume to simulate, and says what to use instead.
        std::string PlainError;
        REQUIRE_MESSAGE(Scene.Kernel.Objects().MakeObjectPlain(3, 4.0f, 4.0f, 0, &PlainError), PlainError);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicTriangleMesh, {3}));
        CHECK(Scene.Error.find("to have volume") != std::string::npos);
    }
}

TEST_CASE("ODE forces act over the whole of one update")
{
    SUBCASE("The same force gives the same motion at any update rate")
    {
        const auto SpeedAfterASecond = [](double UpdatesPerSecond)
        {
            OdeScene Scene;
            Scene.DynamicSphere(2, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            for (int Index = 0; Index < static_cast<int>(UpdatesPerSecond); ++Index)
            {
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {2, 2, 0, 0, 0, 0, 0}));
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / UpdatesPerSecond}));
            }
            return Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2});
        };
        CHECK(SpeedAfterASecond(60.0) == doctest::Approx(2.0).epsilon(0.02));
        CHECK(SpeedAfterASecond(30.0) == doctest::Approx(2.0).epsilon(0.02));
        CHECK(SpeedAfterASecond(120.0) == doctest::Approx(2.0).epsilon(0.02));
    }

    SUBCASE("A force ends with its update, and forces add up")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {2, 30, 0, 0, 0, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {2, 30, 0, 0, 0, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 60.0}));
        const double Speed = Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2});
        CHECK(Speed == doctest::Approx(1.0).epsilon(0.01));
        Scene.Step(30);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}) == doctest::Approx(Speed).epsilon(0.02));
    }

    SUBCASE("A force away from the centre spins the body; one through it does not")
    {
        const auto SpinAfter = [](double PointX)
        {
            OdeScene Scene;
            Scene.DynamicSphere(2, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {2, 0, 0, 60, PointX, 0, 0}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 60.0}));
            CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityZ, {2}) ==
                  doctest::Approx(1.0).epsilon(0.01));
            return Scene.AngularVelocity(2, 1);
        };
        CHECK(SpinAfter(0.0) == doctest::Approx(0.0));
        // Torque (1,0,0) x (0,0,60) over 1/60 s on a solid sphere of inertia 2/5.
        CHECK(SpinAfter(1.0) == doctest::Approx(-2.5).epsilon(0.05));
    }

    SUBCASE("Torques and impulses, on all axes")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        REQUIRE(Scene.AddTorque(2, 6, -12, 24));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 6.0}));
        CHECK(Scene.AngularVelocity(2, 0) == doctest::Approx(2.5).epsilon(0.05));
        CHECK(Scene.AngularVelocity(2, 1) == doctest::Approx(-5.0).epsilon(0.05));
        CHECK(Scene.AngularVelocity(2, 2) == doctest::Approx(10.0).epsilon(0.05));

        REQUIRE(Scene.AddImpulse(2, 1, -2, 3));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}) == doctest::Approx(1.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) == doctest::Approx(-2.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityZ, {2}) == doctest::Approx(3.0));
    }

    SUBCASE("All of them wake a sleeping body and refuse a static one")
    {
        for (const bool Torque : {true, false})
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.DynamicSphere(2, 1.1f);
            Scene.Step(600);
            REQUIRE(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) == doctest::Approx(0.0));
            REQUIRE((Torque ? Scene.AddTorque(2, 1, 1, 1) : Scene.AddImpulse(2, 1, 1, 1)));
            Scene.Step(5);
            const double Moving = std::abs(Scene.AngularVelocity(2, 1)) +
                                  std::abs(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}));
            CHECK(Moving > 0.0);
            CHECK_FALSE((Torque ? Scene.AddTorque(1, 1, 1, 1) : Scene.AddImpulse(1, 1, 1, 1)));
            CHECK(Scene.Error.find("is static") != std::string::npos);
        }

        OdeScene Scene;
        Scene.Floor();
        Scene.DynamicSphere(2, 1.1f);
        Scene.Step(600);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {2, 0, 600, 0, 0, 0, 0}));
        Scene.Step(10);
        CHECK(Scene.Y(2) > 1.5f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {1, 1, 1, 1, 0, 0, 0}));
        CHECK(Scene.Error.find("is static") != std::string::npos);
    }

    SUBCASE("Mass divides the acceleration, and is replaced rather than multiplied")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyMass, {2, 4}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyMass, {2, 2}));
        REQUIRE(Scene.AddImpulse(2, 3, 0, 0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}) == doctest::Approx(1.5));
        // Inertia follows: a torque now turns it half as fast as a mass of 1.
        REQUIRE(Scene.AddTorque(2, 0, 60, 0));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 60.0}));
        CHECK(Scene.AngularVelocity(2, 1) == doctest::Approx(1.25).epsilon(0.05));

        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetBodyMass, {2, 0}));
        CHECK(Scene.Error.find("positive") != std::string::npos);
        Scene.Floor();
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetBodyMass, {1, 2}));
        CHECK(Scene.Error.find("is static") != std::string::npos);
    }
}

TEST_CASE("ODE bodies can leave the simulation's control")
{
    SUBCASE("A kinematic body ignores gravity and forces, follows its velocity and pushes others")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectBox(2, 2.0f, 4.0f, 8.0f);
        Scene.Kernel.Objects().PositionObject(2, -6.0f, 2.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        REQUIRE(Scene.SetBodyKinematic(2, 1));
        Scene.DropSphere(3, 0.0f, 1.0f);
        Scene.Step(60);
        CHECK(Scene.X(2) == doctest::Approx(-6.0f));
        CHECK(Scene.X(3) == doctest::Approx(0.0f).epsilon(0.1));

        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 5, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddForce, {2, 0, 1000, 0, 0, 0, 0}));
        Scene.Step(120);
        CHECK(Scene.X(2) == doctest::Approx(4.0f).epsilon(0.02));
        CHECK(Scene.Y(2) == doctest::Approx(2.0f));
        // The sphere has been pushed ahead of the moving face.
        CHECK(Scene.X(3) >= doctest::Approx(6.0f).epsilon(0.03));

        // Back to dynamic: gravity takes over, with no velocity kept from before.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {2, 0, 20, 0}));
        REQUIRE(Scene.SetBodyKinematic(2, 0));
        Scene.Step(30);
        CHECK(Scene.Y(2) < 19.0f);
    }

    SUBCASE("Damping drains speed, and refuses negative values")
    {
        const auto SpeedAfter = [](double Linear, double Angular)
        {
            OdeScene Scene;
            Scene.DynamicSphere(2, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            REQUIRE(Scene.SetBodyDamping(2, Linear, Angular));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 10, 0, 0}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetAngularVelocity, {2, 0, 10, 0}));
            Scene.Step(60);
            return std::pair<double, double>(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}),
                                             Scene.AngularVelocity(2, 1));
        };
        CHECK(SpeedAfter(0, 0).first == doctest::Approx(10.0));
        CHECK(SpeedAfter(0, 0).second == doctest::Approx(10.0));
        CHECK(SpeedAfter(1, 0).first == doctest::Approx(10.0 * std::exp(-1.0)).epsilon(0.02));
        CHECK(SpeedAfter(1, 0).second == doctest::Approx(10.0));
        CHECK(SpeedAfter(0, 1).second == doctest::Approx(10.0 * std::exp(-1.0)).epsilon(0.02));

        OdeScene Scene;
        Scene.DynamicSphere(2, 0.0f);
        CHECK_FALSE(Scene.SetBodyDamping(2, -1, 0));
        CHECK(Scene.Error.find("non-negative") != std::string::npos);
    }

    SUBCASE("Response mode 2 holds a body still until mode 0 frees it")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 10.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 3, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetResponse, {2, 2}));
        Scene.DropSphere(3, 0.0f, 20.0f);
        Scene.Step(120);
        CHECK(Scene.X(2) == doctest::Approx(0.0f));
        CHECK(Scene.Y(2) == doctest::Approx(10.0f));
        CHECK(Scene.Y(3) > 11.0f);

        // Mode 1 means nothing without the universe mesh and answers like mode 0.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetResponse, {2, 1}));
        Scene.Step(30);
        CHECK(Scene.Y(2) < 9.0f);

        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetResponse, {2, 3}));
        CHECK(Scene.Error.find("pseudo-static") != std::string::npos);
    }
}

TEST_CASE("ODE world settings reach the solver")
{
    SUBCASE("The world step is the length of the fixed step")
    {
        // Each step of semi-implicit Euler falls an extra half step's worth of speed, so its length shows.
        const auto FallAfterASecond = [](double Step)
        {
            OdeScene Scene;
            Scene.DynamicSphere(2, 0.0f);
            REQUIRE(Scene.SetBodyDamping(2, 0, 0));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldStep, {Step}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0}));
            return -static_cast<double>(Scene.Y(2));
        };
        CHECK(FallAfterASecond(0.1) == doctest::Approx(0.5 * 9.81 * 1.1).epsilon(0.002));
        CHECK(FallAfterASecond(0.01) == doctest::Approx(0.5 * 9.81 * 1.01).epsilon(0.002));

        OdeScene Scene;
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetWorldStep, {0}));
        CHECK(Scene.Error.find("between") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetWorldStep, {1}));
    }

    SUBCASE("Ray cast mode turns continuous collision on and off for one body")
    {
        const auto AfterWall = [](double Mode)
        {
            OdeScene Scene;
            Scene.Kernel.Objects().MakeObjectBox(1, 20.0f, 20.0f, 0.2f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {1}));
            Scene.Kernel.Objects().MakeObjectSphere(2, 0.2f, 8, 8);
            Scene.Kernel.Objects().PositionObject(2, 0.0f, 0.0f, -4.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {2}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetRayCast, {2, Mode}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0, 0, 500}));
            Scene.Step(10);
            return Scene.Z(2);
        };
        CHECK(AfterWall(1) < 0.0f);
        CHECK(AfterWall(0) > 1.0f);
    }

    SUBCASE("ERP sets how fast overlaps are pushed apart")
    {
        const auto AfterOverlap = [](double Erp)
        {
            OdeScene Scene;
            Scene.Floor();
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldErp, {Erp}));
            Scene.DropSphere(2, 0.0f, 0.5f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
            Scene.Step(2);
            return Scene.Y(2);
        };
        CHECK(AfterOverlap(0.0) == doctest::Approx(0.5f));
        CHECK(AfterOverlap(0.2) > 0.55f);
        CHECK(AfterOverlap(0.8) > AfterOverlap(0.2) + 0.05f);

        OdeScene Scene;
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetWorldErp, {1.5}));
        CHECK(Scene.Error.find("between 0 and 1") != std::string::npos);
    }

    SUBCASE("More iterations hold a heavy body up better, and exact mode gives the most")
    {
        // A heavy block on a light one: how far it sags in its first moments measures the solver's effort.
        const auto Sag = [](SymbolId Form, double Value)
        {
            OdeScene Scene;
            Scene.Floor();
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldQuickStepNumIterations, {2}));
            REQUIRE(Scene.Run(Form, {Value}));
            Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
            Scene.Kernel.Objects().PositionObject(2, 0.0f, 1.0f, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
            Scene.Kernel.Objects().MakeObjectCube(3, 2.0f);
            Scene.Kernel.Objects().PositionObject(3, 0.0f, 3.0f, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {3}));
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyMass, {3, 200}));
            Scene.Step(20);
            return 3.0f - Scene.Y(3);
        };
        const float Few = Sag(SymbolId::Command_ExtOdeSetWorldQuickStepNumIterations, 2);
        const float Many = Sag(SymbolId::Command_ExtOdeSetWorldQuickStepNumIterations, 30);
        const float Exact = Sag(SymbolId::Command_ExtOdeSetStepMode, 1);
        const float Quick = Sag(SymbolId::Command_ExtOdeSetStepMode, 0);
        CAPTURE(Few);
        CAPTURE(Many);
        CAPTURE(Exact);
        CAPTURE(Quick);
        CHECK(Many < Few);
        CHECK(Exact < Few);
        CHECK(Quick == doctest::Approx(Few));

        OdeScene Scene;
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetWorldQuickStepNumIterations, {0}));
        CHECK(Scene.Error.find("at least one") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetStepMode, {2}));
        CHECK(Scene.Error.find("exact") != std::string::npos);
    }

    SUBCASE("Settings end with the world")
    {
        OdeScene Scene;
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldStep, {0.1}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStart));
        Scene.DynamicSphere(2, 100.0f);
        Scene.Step();
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) ==
              doctest::Approx(-9.81 / 60.0).epsilon(0.02));
    }
}

TEST_CASE("ODE collisions arrive as messages")
{
    SUBCASE("A landing reports both objects, the moment of impact and where it happened")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DropSphere(2, 1.0f, 3.0f);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0);
        Scene.Step(5);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0);

        // Falls 2 units in about 0.64 s, at about 6.3 units per second.
        int Steps = 0;
        while (Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0)
        {
            Scene.Step();
            REQUIRE(++Steps < 120);
        }
        CHECK(Steps + 5 == doctest::Approx(38).epsilon(0.1));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 2.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectB) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAContact) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBContact) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAVelocityY) == doctest::Approx(-6.3).epsilon(0.05));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBVelocityY) == 0.0);
        CHECK(Scene.Contact().Point[0] == doctest::Approx(1.0).epsilon(0.01));
        CHECK(Scene.Contact().Point[1] == doctest::Approx(0.0).epsilon(0.05));
        CHECK(Scene.Contact().Normal[1] == doctest::Approx(-1.0));
        CHECK(Scene.Contact().Normal[0] == doctest::Approx(0.0));
        CHECK(Scene.Contact().Depth >= 0.0);
        CHECK(Scene.Contact().Depth < 0.1);
    }

    SUBCASE("Two moving bodies each report their own motion, and depth grows with overlap")
    {
        OdeScene Scene;
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldGravity, {0, 0, 0}));
        Scene.DropSphere(7, 0.0f, 0.0f);
        Scene.DropSphere(4, 1.5f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {4, -1, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {7, 1, 0, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetAngularVelocity, {7, 0, 3, 0}));
        Scene.Step();
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        // The lower number is object A, and the normal points to object B.
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 4.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectB) == 7.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAContact) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBContact) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAVelocityX) == doctest::Approx(-1.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBVelocityX) == doctest::Approx(1.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBAngularVelocityY) == doctest::Approx(3.0).epsilon(0.01));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAAngularVelocityY) == doctest::Approx(0.0));
        CHECK(Scene.Contact().Normal[0] == doctest::Approx(-1.0));
        CHECK(Scene.Contact().Depth == doctest::Approx(0.5).epsilon(0.05));
        // The point lies on object A.
        CHECK(Scene.Contact().Point[0] == doctest::Approx(0.5).epsilon(0.05));

        // Reading other axes reads other values.
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAVelocityY) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAVelocityZ) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBVelocityY) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBVelocityZ) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAAngularVelocityX) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectAAngularVelocityZ) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBAngularVelocityX) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectBAngularVelocityZ) == 0.0);
        CHECK(Scene.Contact().Normal[2] == doctest::Approx(0.0));
        CHECK(Scene.Contact().Point[2] == doctest::Approx(0.0));
    }

    SUBCASE("A pair reports once a step, newest first, and reading empties the stack")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DropSphere(2, 0.0f, 1.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {3.0 / 60.0}));

        std::vector<double> Heights;
        while (Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0)
        {
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
            Heights.push_back(Scene.Read(SymbolId::Function_ExtOdeGetObjectAVelocityY));
            REQUIRE(Heights.size() < 10);
        }
        CHECK(Heights.size() == 3);

        // Asking clears what was read, and reading past the end gives an empty message rather than a stale one.
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 0.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 0.0);

        Scene.Step();
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 2.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 0.0);
    }

    SUBCASE("A compound body reports its deepest contact once")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 1.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2, -1, 1}));
        Scene.Step();
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        // The plate reaches half a unit below the box's face.
        CHECK(Scene.Contact().Depth > 0.4);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0);
    }

    SUBCASE("Bodies at rest fall silent, and a mode 11 box still reports")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DropSphere(2, 0.0f, 1.0f);
        Scene.Step(600);
        while (Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0)
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        Scene.Step(10);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0);

        Scene.Kernel.Objects().MakeObjectCube(3, 2.0f);
        Scene.Kernel.Objects().PositionObject(3, 8.0f, 2.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {3, -1, 11}));
        Scene.Step(60);
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 3.0);
        CHECK(Scene.Y(3) < 1.0f);
    }

    SUBCASE("The stack keeps the newest messages when nobody reads it")
    {
        OdeScene Scene;
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldGravity, {0, 0, 0}));
        // Fifty overlapping pairs for one hundred steps are five thousand messages.
        for (int Pair = 0; Pair < 50; ++Pair)
        {
            const float Z = 10.0f * static_cast<float>(Pair);
            for (int Side = 0; Side < 2; ++Side)
            {
                const int ObjectId = 10 + 2 * Pair + Side;
                Scene.Kernel.Objects().MakeObjectSphere(ObjectId, 2.0f, 8, 8);
                Scene.Kernel.Objects().PositionObject(ObjectId, Side == 0 ? 0.0f : 1.8f, 0.0f, Z);
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {static_cast<double>(ObjectId)}));
                REQUIRE(Scene.SetBodyDamping(static_cast<double>(ObjectId), 0, 0));
            }
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyMass, {10.0 + 2.0 * Pair, 1000}));
        }
        int Reports = 0;
        for (int Step = 0; Step < 100; ++Step)
        {
            for (int Pair = 0; Pair < 50; ++Pair)
            {
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {10.0 + 2.0 * Pair, 0, 0, 10.0 * Pair}));
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {11.0 + 2.0 * Pair, 1.8, 0, 10.0 * Pair}));
            }
            Scene.Step();
        }
        while (Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0)
        {
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
            // Newest first: within a step, the highest pair comes out first.
            if (Reports == 0)
                CHECK(Scene.Read(SymbolId::Function_ExtOdeGetObjectA) == 108.0);
            REQUIRE(++Reports <= 4096);
        }
        CHECK(Reports == 4096);
    }

    SUBCASE("The world's end takes its messages with it")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DropSphere(2, 0.0f, 1.0f);
        Scene.Step(2);
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStart));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 0.0);

        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeEnd));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
        CHECK(Scene.Error.find("ODE START") != std::string::npos);
        BuiltinValue Result;
        std::string Error;
        CHECK_FALSE(InvokeFunction(Scene.Context, SymbolId::Function_ExtOdeGetObjectA, {}, Result, Error));
        CHECK(Error.find("ODE START") != std::string::npos);
        Error.clear();
        CHECK_FALSE(InvokeFunction(Scene.Context, SymbolId::Function_ExtOdeCollisionMessageExists, {}, Result, Error));
        CHECK(Error.find("ODE START") != std::string::npos);
    }

    SUBCASE("The same scene gives the same messages on every run")
    {
        const auto Pile = []
        {
            OdeScene Scene;
            Scene.Floor();
            for (int Index = 0; Index < 12; ++Index)
            {
                Scene.DropSphere(2 + Index, 0.2f * static_cast<float>(Index % 3),
                                 2.0f + 2.2f * static_cast<float>(Index));
            }
            std::vector<double> Record;
            for (int Step = 0; Step < 240; ++Step)
            {
                Scene.Step();
                while (Scene.Read(SymbolId::Function_ExtOdeCollisionMessageExists) == 1.0)
                {
                    REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCollisionGetMessage));
                    Record.push_back(Scene.Read(SymbolId::Function_ExtOdeGetObjectA));
                    Record.push_back(Scene.Read(SymbolId::Function_ExtOdeGetObjectB));
                }
            }
            return Record;
        };
        const std::vector<double> First = Pile();
        CHECK(First.size() > 100);
        CHECK(Pile() == First);
    }
}

TEST_CASE("ODE contact surfaces bounce only when asked to")
{
    // How high a sphere comes back after falling 4 units onto the floor, at about 8.9 units per second.
    const auto ReboundHeight = [](auto&& Configure)
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.DropSphere(2, 0.0f, 5.0f);
        REQUIRE(Scene.SetBodyDamping(2, 0, 0));
        Configure(Scene);
        float Highest = 0.0f;
        bool Landed = false;
        for (int Step = 0; Step < 180; ++Step)
        {
            Scene.Step();
            if (Scene.Y(2) < 1.1f)
                Landed = true;
            if (Landed)
                Highest = std::max(Highest, Scene.Y(2) - 1.0f);
        }
        REQUIRE(Landed);
        return Highest;
    };

    SUBCASE("The bounce needs its surface mode, as in the plugin")
    {
        CHECK(ReboundHeight([](OdeScene&) {}) < 0.2f);
        CHECK(ReboundHeight([](OdeScene& Scene)
                            { REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounce, {2, 0.8})); }) < 0.2f);

        const float Bouncing = ReboundHeight(
            [](OdeScene& Scene)
            {
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounce, {2, 0.8}));
                REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceModeContactBounce, {2, 1}));
            });
        // A restitution of 0.8 returns 0.64 of the height.
        CHECK(Bouncing == doctest::Approx(0.64f * 4.0f).epsilon(0.1));

        CHECK(ReboundHeight(
                  [](OdeScene& Scene)
                  {
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounce, {2, 0.8}));
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceModeContactBounce, {2, 1}));
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceModeContactBounce, {2, 0}));
                  }) < 0.2f);

        // A static floor can carry the bounce too.
        CHECK(ReboundHeight(
                  [](OdeScene& Scene)
                  {
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounce, {1, 0.8}));
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceModeContactBounce, {1, 1}));
                  }) > 2.0f);
    }

    SUBCASE("The general forms reach the same settings")
    {
        CHECK(ReboundHeight(
                  [](OdeScene& Scene)
                  {
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContact, {2, 2, 1}));
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceMode, {2, 2, 1}));
                  }) == doctest::Approx(4.0f).epsilon(0.1));

        OdeScene Scene;
        Scene.DropSphere(2, 0.0f, 5.0f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetContact, {2, 4, 1}));
        CHECK(Scene.Error.find("no Jolt counterpart") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceMode, {2, 0, 1}));
        CHECK(Scene.Error.find("no Jolt counterpart") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounce, {2, 2}));
        CHECK(Scene.Error.find("between 0 and 1") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounceVelocity, {2, -1}));
        CHECK(Scene.Error.find("non-negative") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetContactVelocity, {2, -1}));
        CHECK(Scene.Error.find("non-negative") != std::string::npos);
    }

    SUBCASE("A body slower than its bounce velocity does not bounce")
    {
        const auto WithThreshold = [&](SymbolId Form, double Threshold)
        {
            return ReboundHeight(
                [&](OdeScene& Scene)
                {
                    REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContactBounce, {2, 0.8}));
                    REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceModeContactBounce, {2, 1}));
                    REQUIRE(Scene.Run(Form, {2, Threshold}));
                });
        };
        CHECK(WithThreshold(SymbolId::Command_ExtOdeSetContactBounceVelocity, 5.0) > 1.5f);
        CHECK(WithThreshold(SymbolId::Command_ExtOdeSetContactBounceVelocity, 20.0) < 0.2f);
        CHECK(WithThreshold(SymbolId::Command_ExtOdeSetContactVelocity, 20.0) < 0.2f);

        CHECK(ReboundHeight(
                  [](OdeScene& Scene)
                  {
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContact, {2, 2, 1}));
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetsurfaceModeContactBounce, {2, 1}));
                      REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContact, {2, 3, 20}));
                  }) < 0.2f);
    }

    SUBCASE("FDIR1 sets the friction of the contact, as the plugin's export does")
    {
        const auto SlideAfter = [](auto&& Configure)
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
            Scene.Kernel.Objects().PositionObject(2, 0.0f, 1.0f, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
            Configure(Scene);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 4, 0, 0}));
            Scene.Step(60);
            return Scene.X(2);
        };
        const float Default = SlideAfter([](OdeScene&) {});
        CHECK(SlideAfter([](OdeScene& Scene) { REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContactFdir1, {2, 0})); }) >
              Default + 1.0f);
        CHECK(SlideAfter([](OdeScene& Scene) { REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetContact, {2, 0, 0})); }) >
              Default + 1.0f);
    }
}

TEST_CASE("ODE rays find the first surface they meet")
{
    SUBCASE("The nearest of several hits gives its object, point and normal")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 5.0f, 1.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {2}));
        Scene.Kernel.Objects().MakeObjectCube(3, 2.0f);
        Scene.Kernel.Objects().PositionObject(3, 9.0f, 1.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {3}));

        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 1, 0, 20, 1, 0}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 2.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionX) == doctest::Approx(4.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionY) == doctest::Approx(1.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionZ) == doctest::Approx(0.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayNormalX) == doctest::Approx(-1.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayNormalY) == doctest::Approx(0.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayNormalZ) == doctest::Approx(0.0));

        // Coming from the other side, the other box and the opposite normal.
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 20, 1, 0, 0, 1, 0}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 3.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionX) == doctest::Approx(10.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayNormalX) == doctest::Approx(1.0));

        // A ray stops at its end point.
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 1, 0, 3, 1, 0}) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 1, 0, 0, 1, 0}) == 0.0);
    }

    SUBCASE("An object number limits the ray to that body")
    {
        OdeScene Scene;
        Scene.DropSphere(2, 5.0f, 0.0f);
        Scene.DropSphere(3, 9.0f, 0.0f);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {3, 0, 0, 0, 20, 0, 0}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 3.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionX) == doctest::Approx(8.0));
        // Without a body, there is nothing to hit.
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {99, 0, 0, 0, 20, 0, 0}) == 0.0);
    }

    SUBCASE("Rays see where a body has moved to, and skip disabled bodies")
    {
        OdeScene Scene;
        Scene.DropSphere(2, 5.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {2, 12, 0, 0}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 0, 0, 20, 0, 0}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionX) == doctest::Approx(11.0));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetEnabled, {2, 0}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 0, 0, 20, 0, 0}) == 0.0);
    }

    SUBCASE("The mask picks terrain, objects and capsules")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(1, 20.0f, 2.0f, 20.0f);
        Scene.Kernel.Objects().PositionObject(1, 0.0f, -1.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticTerrainMesh, {1}));
        Scene.Kernel.Objects().MakeObjectCube(2, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 3.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {2}));
        Scene.Kernel.Objects().MakeObjectBox(3, 2.0f, 6.0f, 2.0f);
        Scene.Kernel.Objects().PositionObject(3, 0.0f, 9.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticCapsule, {3}));

        const auto Downwards = [&](double Mask)
        {
            REQUIRE(Scene.Read(SymbolId::Function_ExtOdeRay, {0, 30, 0, 0, -30, 0, Mask}) == 1.0);
            return Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit);
        };
        CHECK(Downwards(1) == 1.0);
        CHECK(Downwards(2) == 2.0);
        CHECK(Downwards(4) == 3.0);
        CHECK(Downwards(3) == 2.0);
        CHECK(Downwards(7) == 3.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRay, {0, 30, 0, 0, -30, 0, 8}) == 0.0);

        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayTerrain, {0, 30, 0, 0, -30, 0}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayCollisionY) == doctest::Approx(0.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayNormalY) == doctest::Approx(1.0));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayTerrain, {30, 30, 0, 30, -30, 0}) == 0.0);
    }

    SUBCASE("A ray starting inside a solid does not hit it, and a triangle answers from behind")
    {
        OdeScene Scene;
        Scene.DropSphere(2, 0.0f, 0.0f);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 0, 0, 20, 0, 0}) == 0.0);

        OdeScene Meshes;
        Meshes.PlainFacingUp(1, 270.0f);
        REQUIRE(Meshes.Run(SymbolId::Command_ExtOdeCreateStaticTriangleMesh, {1}));
        CHECK(Meshes.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, -5, 0, 0, 5, 0}) == 1.0);
        CHECK(Meshes.Read(SymbolId::Function_ExtOdeGetRayNormalY) == doctest::Approx(-1.0));
        CHECK(Meshes.Read(SymbolId::Function_ExtOdeRayCast, {0, 0, 5, 0, 0, -5, 0}) == 1.0);
        CHECK(Meshes.Read(SymbolId::Function_ExtOdeGetRayNormalY) == doctest::Approx(1.0));
    }

    SUBCASE("A ray force pushes what it hits along the ray, from where it hits")
    {
        OdeScene Scene;
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldGravity, {0, 0, 0}));
        Scene.DropSphere(2, 0.0f, 0.0f);
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeRayForce, {-5, 0, 0, 5, 0, 0, 60}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 2.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}) == 0.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 60.0}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityX, {2}) == doctest::Approx(1.0).epsilon(0.01));
        CHECK(Scene.AngularVelocity(2, 1) == doctest::Approx(0.0));

        // Off the centre it also spins the body, and static bodies only report the hit.
        Scene.DropSphere(3, 0.0f, 20.0f);
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeRayForce, {-5, 20.5, 0, 5, 20.5, 0, 60}) == 1.0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 60.0}));
        CHECK(std::abs(Scene.AngularVelocity(3, 2)) > 0.5);

        Scene.Kernel.Objects().MakeObjectCube(4, 2.0f);
        Scene.Kernel.Objects().PositionObject(4, 0.0f, 40.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {4}));
        REQUIRE(Scene.Read(SymbolId::Function_ExtOdeRayForce, {-5, 40, 0, 5, 40, 0, 60}) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetRayObjectHit) == 4.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeRayForce, {-5, 80, 0, 5, 80, 0, 60}) == 0.0);
    }

    SUBCASE("Rays ask for a world and finite coordinates")
    {
        OdeScene Scene;
        BuiltinValue Result;
        std::string Error;
        const auto Number = [](double Value) { return BuiltinValue::FromNumber(Value); };
        CHECK_FALSE(InvokeFunction(Scene.Context, SymbolId::Function_ExtOdeRay,
                                   {Number(1.0e300), Number(0), Number(0), Number(1), Number(0), Number(0), Number(7)},
                                   Result, Error));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeEnd));
        Error.clear();
        CHECK_FALSE(InvokeFunction(Scene.Context, SymbolId::Function_ExtOdeRay,
                                   {Number(0), Number(0), Number(0), Number(1), Number(0), Number(0), Number(7)},
                                   Result, Error));
        CHECK(Error == "ODE RAY requires ODE START");
        Error.clear();
        CHECK_FALSE(InvokeFunction(Scene.Context, SymbolId::Function_ExtOdeGetRayObjectHit, {}, Result, Error));
        CHECK(Error == "ODE GET RAY OBJECT HIT requires ODE START");
    }
}

namespace
{
    // A cube hanging freely from a static post above it, over nothing.
    void HangCube(OdeScene& Scene, int Post, int Cube)
    {
        Scene.Kernel.Objects().MakeObjectCube(Post, 1.0f);
        Scene.Kernel.Objects().PositionObject(Post, 0.0f, 20.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {static_cast<double>(Post)}));
        Scene.Kernel.Objects().MakeObjectCube(Cube, 1.0f);
        Scene.Kernel.Objects().PositionObject(Cube, 5.0f, 20.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {static_cast<double>(Cube)}));
    }

    double DistanceToPost(OdeScene& Scene, int Cube)
    {
        const double DeltaX = Scene.X(Cube);
        const double DeltaY = Scene.Y(Cube) - 20.0;
        const double DeltaZ = Scene.Z(Cube);
        return std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
    }
} // namespace

TEST_CASE("ODE joints hold bodies together")
{
    SUBCASE("A ball joint swings like a pendulum at a constant distance")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 20.0, 0.0}), Scene.Error);
        double Lowest = 20.0;
        double Leftmost = 5.0;
        for (int Index = 0; Index < 180; ++Index)
        {
            Scene.Step();
            Lowest = std::min<double>(Lowest, Scene.Y(3));
            Leftmost = std::min<double>(Leftmost, Scene.X(3));
            REQUIRE(DistanceToPost(Scene, 3) == doctest::Approx(5.0).epsilon(0.02));
        }
        CHECK(Lowest == doctest::Approx(15.0).epsilon(0.02));
        // Free, it would have fallen 44 units; held, it swings through the bottom and out on the other side.
        CHECK(Leftmost < -3.0);
    }

    SUBCASE("A hinge turns only about its axis")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeCreateHingeJoint, {1, 3, 2, 0.0, 0.0, 1.0, 0.0, 20.0, 0.0}),
                        Scene.Error);
        // Pushing along the axis moves nothing; a ball joint would swing out of the plane.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetWorldGravity, {0.0, 0.0, 0.0}));
        REQUIRE(Scene.AddImpulse(3, 0.0, 0.0, 5.0));
        Scene.Step(30);
        // Free, it would have gone 2.5. On so long a lever the solver brings it back to within a degree before the
        // body comes to rest and goes to sleep.
        CHECK(std::abs(Scene.Z(3)) < 0.25f);
        Scene.Step(120);
        CHECK(std::abs(Scene.Z(3)) < 0.1f);
        CHECK(Scene.X(3) == doctest::Approx(5.0f).epsilon(0.02));
        // Across the axis it turns freely around the pin.
        REQUIRE(Scene.AddImpulse(3, 0.0, -4.0, 0.0));
        Scene.Step(30);
        CHECK(Scene.Y(3) < 19.0f);
        CHECK(std::abs(Scene.Z(3)) < 0.1f);
        CHECK(DistanceToPost(Scene, 3) == doctest::Approx(5.0).epsilon(0.02));
    }

    SUBCASE("A hinge limit stops the swing, measured as object A's turn about the axis")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateHingeJoint, {7, 3, 2, 0.0, 0.0, 1.0, 0.0, 20.0, 0.0}));
        // The cube lies on +X, so falling is a negative turn about +Z.
        const double Low = -0.5;
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {7, Low, 1.0}), Scene.Error);
        double Lowest = 20.0;
        for (int Index = 0; Index < 240; ++Index)
        {
            Scene.Step();
            Lowest = std::min<double>(Lowest, Scene.Y(3));
        }
        CHECK(Lowest == doctest::Approx(20.0 + 5.0 * std::sin(Low)).epsilon(0.02));
        CHECK(Scene.Y(3) == doctest::Approx(20.0 + 5.0 * std::sin(Low)).epsilon(0.02));
    }

    SUBCASE("Bodies a joint connects do not collide")
    {
        OdeScene Scene;
        // The cube's hinge pin is inside the post, as a door's is inside its frame.
        Scene.Kernel.Objects().MakeObjectBox(2, 2.0f, 2.0f, 2.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 20.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {2}));
        Scene.Kernel.Objects().MakeObjectBox(3, 4.0f, 1.0f, 1.0f);
        Scene.Kernel.Objects().PositionObject(3, 1.5f, 20.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {3}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateHingeJoint, {1, 3, 2, 0.0, 0.0, 1.0, 0.0, 20.0, 0.0}));
        Scene.Step(60);
        // It swings from where it overlaps the post; the post would otherwise throw it out or hold it level.
        CHECK(Scene.Y(3) < 19.0f);
        CHECK(Scene.X(3) * Scene.X(3) + (Scene.Y(3) - 20.0f) * (Scene.Y(3) - 20.0f) ==
              doctest::Approx(2.25f).epsilon(0.05));
        int Exists = 0;
        REQUIRE(Scene.Kernel.Physics().CollisionMessageExists(Exists, &Scene.Error));
        CHECK(Exists == 0);
    }

    SUBCASE("Two moving bodies can share a joint, and destroying one frees the other")
    {
        OdeScene Scene;
        Scene.DynamicSphere(2, 10.0f);
        Scene.Kernel.Objects().MakeObjectSphere(3, 2.0f, 8, 8);
        Scene.Kernel.Objects().PositionObject(3, 3.0f, 10.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicSphere, {3}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 3.0, 10.0, 0.0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0.0, 6.0, 0.0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {2, 0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetGravity, {3, 0}));
        Scene.Step(30);
        const auto Separation = [&Scene]
        {
            const float DeltaX = Scene.X(2) - Scene.X(3);
            const float DeltaY = Scene.Y(2) - Scene.Y(3);
            return std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
        };
        CHECK(Separation() == doctest::Approx(3.0f).epsilon(0.02));
        CHECK(Scene.Y(3) > 10.2f);

        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeDestroyObject, {2}));
        const float YBefore = Scene.Y(3);
        const double Velocity = Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {3});
        Scene.Step(30);
        // Free, and it keeps the velocity it had; the number can be used again.
        CHECK(Scene.Y(3) == doctest::Approx(YBefore + 0.5 * Velocity).epsilon(0.05));
        Scene.DynamicSphere(2, 30.0f);
        CHECK(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 0.0, 0.0}));
    }

    SUBCASE("A deleted object takes its joints with it")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 20.0, 0.0}));
        Scene.Step(10);
        Scene.Kernel.Objects().DeleteObject(2);
        Scene.Kernel.Physics().ObjectDeleted(2);
        Scene.Step(60);
        // The cube falls free once the post has gone.
        CHECK(DistanceToPost(Scene, 3) > 5.5);
    }

    SUBCASE("Wrong use is reported")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        Scene.Kernel.Objects().MakeObjectCube(4, 1.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {4}));

        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 99, 0.0, 0.0, 0.0}));
        CHECK(Scene.Error.find("ODE body for object 99") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 3, 3, 0.0, 0.0, 0.0}));
        CHECK(Scene.Error.find("two different bodies") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 4, 0.0, 0.0, 0.0}));
        CHECK(Scene.Error.find("one moving body") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateHingeJoint, {1, 3, 2, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
        CHECK(Scene.Error.find("axis") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {1, -1.0, 1.0}));
        CHECK(Scene.Error.find("requires joint 1") != std::string::npos);

        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 20.0, 0.0}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 20.0, 0.0}));
        CHECK(Scene.Error.find("joint 1 exists") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {1, -1.0, 1.0}));
        CHECK(Scene.Error.find("ball joint") != std::string::npos);

        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateHingeJoint, {2, 3, 2, 0.0, 0.0, 1.0, 0.0, 20.0, 0.0}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {2, 0.5, 1.0}));
        CHECK(Scene.Error.find("low limit") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {2, -1.0, 4.0}));
    }

    SUBCASE("The eight-argument hinge takes its number from the first object")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeCreateHingeJoint, {3, 2, 0.0, 0.0, 1.0, 0.0, 20.0, 0.0}),
                        Scene.Error);
        CHECK(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {3, -0.5, 0.5}));
        Scene.Step(60);
        CHECK(DistanceToPost(Scene, 3) == doctest::Approx(5.0).epsilon(0.02));
    }

    SUBCASE("A restarted world has no joints")
    {
        OdeScene Scene;
        HangCube(Scene, 2, 3);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 20.0, 0.0}));
        Scene.Step(5);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStart));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetHingeJointLimit, {1, -1.0, 1.0}));
        CHECK(Scene.Error.find("requires joint 1") != std::string::npos);
    }
}

TEST_CASE("ODE swing door hinges hold a body to the world")
{
    SUBCASE("The door turns about its own Y axis through a point relative to its origin")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(2, 4.0f, 6.0f, 0.5f);
        Scene.Kernel.Objects().PositionObject(2, 10.0f, 3.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCreateSwingdoorHinge, {2, -2.0, 0.0, 0.0, 90.0}) == 1.0);
        // Already turning about the hinge at (8, 3, 0), so it keeps a whole radian per second.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetAngularVelocity, {2, 0.0, 1.0, 0.0}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 0.0, 0.0, -2.0}));
        Scene.Step(60);
        // Gravity cannot take it down, and it stays at the door's width from the hinge.
        CHECK(Scene.Y(2) == doctest::Approx(3.0f).epsilon(0.01));
        const float DeltaX = Scene.X(2) - 8.0f;
        const float DeltaZ = Scene.Z(2);
        CHECK(DeltaX * DeltaX + DeltaZ * DeltaZ == doctest::Approx(4.0f).epsilon(0.02));
        CHECK(DeltaZ < -1.0f);
    }

    SUBCASE("A number without a body answers 0, and a static body is refused")
    {
        OdeScene Scene;
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCreateSwingdoorHinge, {50, 0.0, 0.0, 0.0, 0.0}) == 0.0);
        Scene.Floor();
        BuiltinValue Result;
        std::vector<BuiltinValue> Arguments;
        for (const double Value : {1.0, 0.0, 0.0, 0.0, 0.0})
            Arguments.push_back(BuiltinValue::FromNumber(Value));
        std::string Error;
        CHECK_FALSE(
            InvokeFunction(Scene.Context, SymbolId::Function_ExtOdeCreateSwingdoorHinge, Arguments, Result, Error));
        CHECK(Error.find("moving body") != std::string::npos);
    }

    SUBCASE("Destroying the door removes its hinge")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(2, 4.0f, 6.0f, 0.5f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeCreateSwingdoorHinge, {2, -2.0, 0.0, 0.0, 0.0}) == 1.0);
        Scene.Step(5);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeDestroyObject, {2}));
        Scene.Step(5);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        const float Before = Scene.Y(2);
        Scene.Step(30);
        CHECK(Scene.Y(2) < Before - 0.5f);
    }
}

TEST_CASE("ODE static objects are made of boxes")
{
    const auto MakeCompound = [](OdeScene& Scene, int Material)
    {
        // Two platforms with a gap between them, each a box of its own, at the finished object's height.
        Scene.Kernel.Objects().MakeObjectCube(2, 1.0f);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 5.0f, 0.0f);
        Scene.Kernel.Objects().MakeObjectBox(3, 4.0f, 2.0f, 4.0f);
        Scene.Kernel.Objects().PositionObject(3, -5.0f, 0.0f, 0.0f);
        Scene.Kernel.Objects().MakeObjectBox(4, 4.0f, 2.0f, 4.0f);
        Scene.Kernel.Objects().PositionObject(4, 5.0f, 0.0f, 0.0f);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeStartStaticObject, {2}), Scene.Error);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {2, 3}), Scene.Error);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {2, 4}), Scene.Error);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeEndStaticObject, {2, static_cast<double>(Material)}),
                        Scene.Error);
    };

    SUBCASE("Spheres land on each box and fall through the gap")
    {
        OdeScene Scene;
        MakeCompound(Scene, 0);
        Scene.DropSphere(5, -5.0f, 12.0f);
        Scene.DropSphere(6, 5.0f, 12.0f);
        Scene.DropSphere(7, 0.0f, 12.0f);
        Scene.Step(180);
        // The boxes sit five above their own positions, since they count from the finished object.
        CHECK(Scene.Y(5) == doctest::Approx(7.0f).epsilon(0.02));
        CHECK(Scene.Y(6) == doctest::Approx(7.0f).epsilon(0.02));
        CHECK(Scene.Y(7) < 0.0f);
    }

    SUBCASE("A box's rotation and centre count too")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectCube(2, 1.0f);
        // A tall thin box laid on its side by turning it about Z, so it stops a sphere well away from its origin.
        Scene.Kernel.Objects().MakeObjectBox(3, 1.0f, 12.0f, 4.0f);
        Scene.Kernel.Objects().RotateObject(3, 0.0f, 0.0f, 90.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStartStaticObject, {2}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {2, 3}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeEndStaticObject, {2, 0}));
        Scene.DropSphere(5, 5.0f, 10.0f);
        Scene.Step(180);
        CHECK(Scene.Y(5) == doctest::Approx(1.5f).epsilon(0.02));
    }

    SUBCASE("Material 1 makes it terrain for rays")
    {
        OdeScene Scene;
        MakeCompound(Scene, 1);
        int Hit = 0;
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, -5.0f, 20.0f, 0.0f, -5.0f, -20.0f, 0.0f, 1, &Scene.Error));
        CHECK(Hit == 1);
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, -5.0f, 20.0f, 0.0f, -5.0f, -20.0f, 0.0f, 2, &Scene.Error));
        CHECK(Hit == 0);
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, 0.0f, 20.0f, 0.0f, 0.0f, -20.0f, 0.0f, 1, &Scene.Error));
        CHECK(Hit == 0);
    }

    SUBCASE("Any other material makes it an object for rays")
    {
        OdeScene Scene;
        MakeCompound(Scene, 3);
        int Hit = 0;
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, -5.0f, 20.0f, 0.0f, -5.0f, -20.0f, 0.0f, 2, &Scene.Error));
        CHECK(Hit == 1);
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, -5.0f, 20.0f, 0.0f, -5.0f, -20.0f, 0.0f, 1, &Scene.Error));
        CHECK(Hit == 0);
    }

    SUBCASE("Wrong use is reported")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectCube(2, 1.0f);
        Scene.Kernel.Objects().MakeObjectCube(3, 1.0f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {2, 3}));
        CHECK(Scene.Error.find("ODE START STATIC OBJECT") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeEndStaticObject, {2, 0}));
        CHECK(Scene.Error.find("ODE START STATIC OBJECT") != std::string::npos);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStartStaticObject, {2}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {2, 99}));
        CHECK(Scene.Error.find("object 99") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {4, 3}));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeEndStaticObject, {2, 0}));
        CHECK(Scene.Error.find("ODE ADD STATIC OBJECT BOX first") != std::string::npos);
        // The failed end closed the object.
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeEndStaticObject, {2, 0}));
        CHECK(Scene.Error.find("ODE START STATIC OBJECT") != std::string::npos);
    }

    SUBCASE("A restarted world forgets an unfinished object")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectCube(2, 1.0f);
        Scene.Kernel.Objects().MakeObjectCube(3, 1.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStartStaticObject, {2}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeAddStaticObjectBox, {2, 3}));
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStart));
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeEndStaticObject, {2, 0}));
    }
}

namespace
{
    // A character the size and proportions of a person two units tall, half a unit in radius.
    void MakeCharacter(OdeScene& Scene, int ObjectId, float X, float Y, float Z, double Gravity = 20.0,
                       double FallSpeed = 100.0, double MaximumSlope = 45.0)
    {
        Scene.Kernel.Objects().MakeObjectBox(ObjectId, 1.0f, 2.0f, 1.0f);
        Scene.Kernel.Objects().PositionObject(ObjectId, X, Y, Z);
        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController,
                                  {static_cast<double>(ObjectId), Gravity, FallSpeed, MaximumSlope}),
                        Scene.Error);
    }

    struct CharacterInput
    {
        double YDegrees = 0.0;
        double XDegrees = 0.0;
        double Speed = 0.0;
        double Jump = 0.0;
        double Ducking = 0.0;
        double PushDegrees = 0.0;
        double PushForce = 0.0;
        double Thrust = 0.0;
    };

    bool ControlCharacter(OdeScene& Scene, int ObjectId, const CharacterInput& Input)
    {
        return Scene.Run(SymbolId::Command_ExtOdeControlCharacterController,
                         {static_cast<double>(ObjectId), Input.YDegrees, Input.XDegrees, Input.Speed, Input.Jump,
                          Input.Ducking, Input.PushDegrees, Input.PushForce, Input.Thrust});
    }

    // The character's feet are at its object's lowest point, one below its position.
    constexpr float StandingY = 1.0f;
} // namespace

TEST_CASE("ODE character controllers walk and fall")
{
    SUBCASE("It falls by its own gravity and comes to rest on the floor")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, 11.0f, 0.0f);
        Scene.Step(30);
        // Half a second at 20, not the world's 9.81.
        CHECK(Scene.Y(2) == doctest::Approx(11.0f - 0.5f * 20.0f * 0.25f).epsilon(0.03));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) == doctest::Approx(-10.0).epsilon(0.03));
        Scene.Step(120);
        CHECK(Scene.Y(2) == doctest::Approx(StandingY).epsilon(0.03));
    }

    SUBCASE("The fall speed limits how fast it falls")
    {
        OdeScene Scene;
        MakeCharacter(Scene, 2, 0.0f, 1000.0f, 0.0f, 20.0, 5.0);
        Scene.Step(120);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) == doctest::Approx(-5.0).epsilon(0.01));
        CHECK(Scene.Y(2) > 1000.0f - 2.0f * 5.0f - 0.1f);
    }

    SUBCASE("Walking moves it at speed per 1/120 s, the way it faces")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Step(5);
        CharacterInput Input;
        Input.Speed = 0.05;
        REQUIRE_MESSAGE(ControlCharacter(Scene, 2, Input), Scene.Error);
        Scene.Step(60);
        CHECK(Scene.Z(2) == doctest::Approx(6.0f).epsilon(0.03));
        CHECK(Scene.X(2) == doctest::Approx(0.0f).epsilon(0.02));
        CHECK(Scene.Y(2) == doctest::Approx(StandingY).epsilon(0.03));

        // The walk lasts until the next control, here turning a quarter to the right.
        Input.YDegrees = 90.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(30);
        CHECK(Scene.X(2) == doctest::Approx(3.0f).epsilon(0.03));
        CHECK(Scene.Z(2) == doctest::Approx(6.0f).epsilon(0.03));

        // Pitch slows the walk by its cosine, up or down.
        Input.YDegrees = 0.0;
        Input.XDegrees = -60.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        const float ZBefore = Scene.Z(2);
        Scene.Step(60);
        CHECK(Scene.Z(2) - ZBefore == doctest::Approx(3.0f).epsilon(0.03));
        CHECK(Scene.Y(2) == doctest::Approx(StandingY).epsilon(0.03));
    }

    SUBCASE("A push replaces the walk")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Step(5);
        CharacterInput Input;
        Input.Speed = 0.05;
        Input.PushDegrees = 270.0;
        Input.PushForce = 0.025;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(60);
        CHECK(Scene.X(2) == doctest::Approx(-3.0f).epsilon(0.03));
        CHECK(Scene.Z(2) == doctest::Approx(0.0f).epsilon(0.03));
    }

    SUBCASE("A wall stops it")
    {
        OdeScene Scene;
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectBox(3, 10.0f, 10.0f, 1.0f);
        Scene.Kernel.Objects().PositionObject(3, 0.0f, 5.0f, 5.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {3}));
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        CharacterInput Input;
        Input.Speed = 0.05;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(120);
        // The wall's near face is at 4.5, less the radius of 0.5.
        CHECK(Scene.Z(2) == doctest::Approx(4.0f).epsilon(0.02));
    }

    SUBCASE("It climbs a low step and not a high one")
    {
        const auto ZAfterWalkingAt = [](float StepHeight)
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.Kernel.Objects().MakeObjectBox(3, 20.0f, StepHeight, 20.0f);
            Scene.Kernel.Objects().PositionObject(3, 0.0f, 0.5f * StepHeight, 12.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {3}));
            MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
            CharacterInput Input;
            Input.Speed = 0.05;
            REQUIRE(ControlCharacter(Scene, 2, Input));
            Scene.Step(120);
            return std::pair{Scene.Z(2), Scene.Y(2)};
        };
        // 10.35 in 70 of a height of 2 climbs up to about 0.3.
        const auto [LowZ, LowY] = ZAfterWalkingAt(0.2f);
        CHECK(LowZ > 8.0f);
        CHECK(LowY == doctest::Approx(StandingY + 0.2f).epsilon(0.03));
        const auto [HighZ, HighY] = ZAfterWalkingAt(1.0f);
        CHECK(HighZ == doctest::Approx(1.5f).epsilon(0.03));
        CHECK(HighY == doctest::Approx(StandingY).epsilon(0.03));
    }

    SUBCASE("A slope is walked up below the maximum and not above it")
    {
        const auto HeightAfterWalkingUp = [](float SlopeDegrees)
        {
            OdeScene Scene;
            Scene.Floor();
            Scene.Kernel.Objects().MakeObjectBox(3, 20.0f, 1.0f, 40.0f);
            // A ramp rising along +Z, its top surface at the height of the floor at Z = 3.
            const float Radians = SlopeDegrees * std::numbers::pi_v<float> / 180.0f;
            Scene.Kernel.Objects().PositionObject(3, 0.0f, 20.0f * std::sin(Radians) - 0.5f * std::cos(Radians),
                                                  3.0f + 20.0f * std::cos(Radians) + 0.5f * std::sin(Radians));
            Scene.Kernel.Objects().RotateObject(3, -SlopeDegrees, 0.0f, 0.0f);
            REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {3}));
            MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
            CharacterInput Input;
            Input.Speed = 0.05;
            REQUIRE(ControlCharacter(Scene, 2, Input));
            Scene.Step(240);
            return Scene.Y(2);
        };
        CHECK(HeightAfterWalkingUp(30.0f) > 4.0f);
        CHECK(HeightAfterWalkingUp(60.0f) < 2.0f);
    }

    SUBCASE("It jumps only from the ground")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Step(5);
        CharacterInput Input;
        Input.Jump = 10.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Input.Jump = 0.0;
        Scene.Step(15);
        // A quarter of a second after leaving at 10: 10t - 10t^2.
        CHECK(Scene.Y(2) == doctest::Approx(StandingY + 10.0f * 0.25f - 0.5f * 20.0f * 0.0625f).epsilon(0.05));
        const double Rising = Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2});
        CHECK(Rising == doctest::Approx(5.0).epsilon(0.1));

        // In the air, a second jump does nothing.
        Input.Jump = 10.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Input.Jump = 0.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step();
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetBodyLinearVelocityY, {2}) < Rising);

        Scene.Step(120);
        CHECK(Scene.Y(2) == doctest::Approx(StandingY).epsilon(0.03));
    }

    SUBCASE("Thrust carries it up while it lasts")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Step(5);
        CharacterInput Input;
        Input.Thrust = 3.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(60);
        CHECK(Scene.Y(2) == doctest::Approx(StandingY + 3.0f).epsilon(0.03));
        Input.Thrust = 0.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(120);
        CHECK(Scene.Y(2) == doctest::Approx(StandingY).epsilon(0.03));
    }

    SUBCASE("Landing is reported once, with half the distance fallen")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY + 10.0f, 0.0f);
        Scene.Step(5);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterHitFloor) == 0.0);
        Scene.Step(120);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterHitFloor) == 1.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterHitFloor) == 0.0);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterFallDistance) == doctest::Approx(5.0).epsilon(0.03));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterFallDistance) == 0.0);
        Scene.Step(30);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterHitFloor) == 0.0);
    }

    SUBCASE("The object's X rotation says whether it stands, and its other angles stay the program's")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, 5.0f, 0.0f);
        Scene.Kernel.Objects().RotateObject(2, 0.0f, 33.0f, 0.0f);
        Scene.Step(5);
        CHECK(Scene.Kernel.Objects().ObjectAngleX(2) == doctest::Approx(0.0f));
        Scene.Step(120);
        CHECK(Scene.Kernel.Objects().ObjectAngleX(2) == doctest::Approx(1.0f));
        CHECK(Scene.Kernel.Objects().ObjectAngleY(2) == doctest::Approx(33.0f));
    }

    SUBCASE("Short updates extrapolate its motion, and not gravity's pull while it stands")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Step(5);
        CharacterInput Input;
        Input.Speed = 0.05;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step();
        const float ZBefore = Scene.Z(2);
        const float YBefore = Scene.Y(2);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeUpdate, {1.0 / 240.0}));
        CHECK(Scene.Z(2) == doctest::Approx(ZBefore + 6.0f / 240.0f).epsilon(0.01));
        CHECK(Scene.Y(2) == doctest::Approx(YBefore).epsilon(0.001));
    }
}

TEST_CASE("ODE character controllers duck under what they cannot pass")
{
    // A slab whose underside is 1.8 above the floor, starting four along Z.
    const auto MakeLowCeiling = [](OdeScene& Scene)
    {
        Scene.Floor();
        Scene.Kernel.Objects().MakeObjectBox(3, 10.0f, 1.0f, 10.0f);
        Scene.Kernel.Objects().PositionObject(3, 0.0f, 2.3f, 9.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateStaticBox, {3}));
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Step(5);
    };

    SUBCASE("Standing it cannot pass, and ducked it can")
    {
        OdeScene Scene;
        MakeLowCeiling(Scene);
        CharacterInput Input;
        Input.Speed = 0.05;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(90);
        // Its rounded top meets the slab a little before the slab's edge is over its axis.
        CHECK(Scene.Z(2) < 4.0f);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterControllerDucking, {2}) == 0.0);

        Input.Ducking = 1.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterControllerDucking, {2}) == 1.0);
        Scene.Step(60);
        CHECK(Scene.Z(2) > 6.0f);
        CHECK(Scene.Y(2) == doctest::Approx(StandingY).epsilon(0.03));
    }

    SUBCASE("Under the slab it stays ducked until there is room")
    {
        OdeScene Scene;
        MakeLowCeiling(Scene);
        CharacterInput Input;
        Input.Speed = 0.05;
        Input.Ducking = 1.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(90);
        REQUIRE(Scene.Z(2) > 8.0f);

        // Told to stand under the slab, it cannot; walking back out it stands by itself.
        Input.Ducking = 0.0;
        Input.Speed = 0.0;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterControllerDucking, {2}) == 1.0);
        Input.Speed = -0.05;
        for (int Index = 0; Index < 120; ++Index)
        {
            REQUIRE(ControlCharacter(Scene, 2, Input));
            Scene.Step();
        }
        CHECK(Scene.Z(2) < 3.0f);
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterControllerDucking, {2}) == 0.0);
    }

    SUBCASE("Only exactly 1 ducks")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        CharacterInput Input;
        Input.Ducking = 0.5;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        CHECK(Scene.Read(SymbolId::Function_ExtOdeGetCharacterControllerDucking, {2}) == 0.0);
    }
}

TEST_CASE("ODE character controllers share the world with bodies")
{
    SUBCASE("It rides what it stands on")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(3, 20.0f, 1.0f, 20.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {3}));
        REQUIRE(Scene.SetBodyKinematic(3, 1));
        MakeCharacter(Scene, 2, 0.0f, 1.5f, 0.0f);
        Scene.Step(5);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {3, 2.0, 0.0, 0.0}));
        Scene.Step(60);
        CHECK(Scene.X(2) == doctest::Approx(2.0f).epsilon(0.05));
        CHECK(Scene.Y(2) == doctest::Approx(1.5f).epsilon(0.03));
    }

    SUBCASE("It pushes a loose body")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.Kernel.Objects().MakeObjectCube(3, 1.0f);
        Scene.Kernel.Objects().PositionObject(3, 0.0f, 0.5f, 3.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {3}));
        Scene.Step(5);
        CharacterInput Input;
        Input.Speed = 0.03;
        REQUIRE(ControlCharacter(Scene, 2, Input));
        Scene.Step(120);
        CHECK(Scene.Z(3) > 3.5f);
    }

    SUBCASE("A falling body lands on it rather than passing through")
    {
        OdeScene Scene;
        Scene.Floor();
        MakeCharacter(Scene, 2, 0.0f, StandingY, 0.0f);
        Scene.DropSphere(3, 0.0f, 8.0f);
        Scene.Step(30);
        int Touching = 0;
        Scene.Step(60);
        while (true)
        {
            int Exists = 0;
            REQUIRE(Scene.Kernel.Physics().CollisionMessageExists(Exists, &Scene.Error));
            if (Exists == 0)
                break;
            REQUIRE(Scene.Kernel.Physics().NextCollisionMessage(&Scene.Error));
            const dbc::PhysicsCollisionMessage* Message = Scene.Kernel.Physics().CollisionMessage(&Scene.Error);
            REQUIRE(Message);
            if ((Message->ObjectA == 2 && Message->ObjectB == 3) || (Message->ObjectA == 3 && Message->ObjectB == 2))
                ++Touching;
        }
        CHECK(Touching > 0);
    }

    SUBCASE("Rays see it as a capsule")
    {
        OdeScene Scene;
        MakeCharacter(Scene, 2, 0.0f, 10.0f, 0.0f);
        int Hit = 0;
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4, &Scene.Error));
        CHECK(Hit == 1);
        REQUIRE(Scene.Kernel.Physics().RayObjectHit(Hit, &Scene.Error));
        CHECK(Hit == 2);
        float Point = 0.0f;
        REQUIRE(Scene.Kernel.Physics().RayCollision(Point, 1, &Scene.Error));
        CHECK(Point == doctest::Approx(11.0f).epsilon(0.02));
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, &Scene.Error));
        CHECK(Hit == 0);
    }

    SUBCASE("It moves by position, and its feet stay on the object's lowest point")
    {
        OdeScene Scene;
        Scene.Floor();
        // A model whose origin is at its feet stands at the floor's height, not half sunk.
        Scene.Kernel.Objects().MakeObjectBox(2, 1.0f, 2.0f, 1.0f);
        Scene.Kernel.Objects().OffsetLimb(2, 0, 0.0f, 1.0f, 0.0f, 0, &Scene.Error);
        Scene.Kernel.Objects().PositionObject(2, 0.0f, 3.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {2, 20.0, 100.0, 45.0}));
        Scene.Step(120);
        CHECK(Scene.Y(2) == doctest::Approx(0.0f).epsilon(0.03));

        REQUIRE_MESSAGE(Scene.Run(SymbolId::Command_ExtOdeSetBodyPosition, {2, 5.0, 0.0, 5.0}), Scene.Error);
        CHECK(Scene.X(2) == doctest::Approx(5.0f));
        Scene.Step(5);
        CHECK(Scene.X(2) == doctest::Approx(5.0f));
        CHECK(Scene.Z(2) == doctest::Approx(5.0f));
    }

    SUBCASE("Rigid body forms that cannot apply say so")
    {
        OdeScene Scene;
        MakeCharacter(Scene, 2, 0.0f, 5.0f, 0.0f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 1.0, 0.0, 0.0}));
        CHECK(Scene.Error.find("character controller of object 2") != std::string::npos);
        CHECK_FALSE(Scene.AddImpulse(2, 1.0, 0.0, 0.0));
        CHECK(Scene.Error.find("character controller of object 2") != std::string::npos);
        Scene.DynamicSphere(3, 5.0f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateBallJoint, {1, 2, 3, 0.0, 0.0, 0.0}));
        CHECK(Scene.Error.find("character controller") != std::string::npos);
    }

    SUBCASE("A disabled character stands still and rays pass it")
    {
        OdeScene Scene;
        MakeCharacter(Scene, 2, 0.0f, 10.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetEnabled, {2, 0}));
        Scene.Step(30);
        CHECK(Scene.Y(2) == doctest::Approx(10.0f));
        int Hit = 1;
        REQUIRE(Scene.Kernel.Physics().Ray(Hit, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4, &Scene.Error));
        CHECK(Hit == 0);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeSetEnabled, {2, 1}));
        Scene.Step(30);
        CHECK(Scene.Y(2) < 9.0f);
    }

    SUBCASE("Bad values and missing characters are reported")
    {
        OdeScene Scene;
        Scene.Kernel.Objects().MakeObjectBox(2, 1.0f, 2.0f, 1.0f);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {99, 20.0, 100.0, 45.0}));
        CHECK(Scene.Error.find("object 99") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {2, -1.0, 100.0, 45.0}));
        CHECK(Scene.Error.find("negative gravity") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {2, 20.0, 0.0, 45.0}));
        CHECK(Scene.Error.find("fall speed") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {2, 20.0, 100.0, 0.0}));
        CHECK(Scene.Error.find("maximum slope") != std::string::npos);
        CHECK_FALSE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {2, 20.0, 100.0, 91.0}));

        CharacterInput Input;
        CHECK_FALSE(ControlCharacter(Scene, 2, Input));
        CHECK(Scene.Error.find("ODE body for object 2") != std::string::npos);
        Scene.DynamicSphere(3, 5.0f);
        CHECK_FALSE(ControlCharacter(Scene, 3, Input));
        CHECK(Scene.Error.find("requires a character controller for object 3") != std::string::npos);

        int Value = 0;
        float Distance = 0.0f;
        CHECK_FALSE(Scene.Kernel.Physics().CharacterHitFloor(Value, &Scene.Error));
        CHECK(Scene.Error.find("ODE CREATE CHARACTER CONTROLLER") != std::string::npos);
        CHECK_FALSE(Scene.Kernel.Physics().CharacterFallDistance(Distance, &Scene.Error));
    }

    SUBCASE("Characters follow the life of their objects and of the world")
    {
        OdeScene Scene;
        MakeCharacter(Scene, 2, 0.0f, 50.0f, 0.0f);
        Scene.Step(5);
        // Made again, the old character has gone.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateCharacterController, {2, 40.0, 100.0, 45.0}));
        const float Before = Scene.Y(2);
        Scene.Step(30);
        CHECK(Scene.Y(2) == doctest::Approx(Before - 0.5f * 40.0f * 0.25f).epsilon(0.03));

        // Replaced by an ordinary body, it obeys that body's forms.
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeCreateDynamicBox, {2}));
        CHECK(Scene.Run(SymbolId::Command_ExtOdeSetLinearVelocity, {2, 1.0, 0.0, 0.0}));
        MakeCharacter(Scene, 3, 0.0f, 20.0f, 0.0f);

        // Deleting the object or destroying the body ends it, leaving nothing to step.
        Scene.Kernel.Objects().DeleteObject(3);
        Scene.Step(2);
        int Value = 0;
        CHECK_FALSE(Scene.Kernel.Physics().CharacterHitFloor(Value, &Scene.Error));
        MakeCharacter(Scene, 4, 5.0f, 20.0f, 0.0f);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeDestroyObject, {4}));
        Scene.Step(2);
        CHECK_FALSE(Scene.Kernel.Physics().CharacterHitFloor(Value, &Scene.Error));

        MakeCharacter(Scene, 5, 10.0f, 20.0f, 0.0f);
        Scene.Step(2);
        REQUIRE(Scene.Run(SymbolId::Command_ExtOdeStart));
        CHECK_FALSE(Scene.Kernel.Physics().CharacterHitFloor(Value, &Scene.Error));
        CHECK_FALSE(ControlCharacter(Scene, 5, CharacterInput{}));
    }
}
