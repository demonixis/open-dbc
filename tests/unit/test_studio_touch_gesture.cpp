#include <doctest/doctest.h>

#include "core/touch_gesture.hpp"

#include <vector>

using namespace dbc::studio::core;
using Kind = TouchAction::Kind;

namespace
{
    std::vector<Kind> Kinds(const std::vector<TouchAction>& Actions)
    {
        std::vector<Kind> Result;
        for (const TouchAction& Action : Actions)
            Result.push_back(Action.Type);
        return Result;
    }

    const TouchGesture::ScrollQuery Vertical = [](bool Horizontal) { return !Horizontal; };
    const TouchGesture::ScrollQuery Nothing = [](bool) { return false; };
} // namespace

TEST_CASE("a tap is a click where the finger landed, and the pointer leaves")
{
    TouchGesture Gesture;
    std::vector<TouchAction> Out;
    Gesture.Down(1, {100.0f, 50.0f}, 0, Out);
    Gesture.Motion(1, {103.0f, 52.0f}, Vertical, Out);
    Gesture.Up(1, {103.0f, 52.0f}, Out);
    REQUIRE(Kinds(Out) == std::vector<Kind>{Kind::Hover, Kind::Press, Kind::Release, Kind::Leave});
    CHECK(Out[1].At.X == doctest::Approx(100.0f));
    CHECK(Out[2].At.Y == doctest::Approx(50.0f));
}

TEST_CASE("a drag scrolls what scrolls along it and never clicks")
{
    TouchGesture Gesture;
    std::vector<TouchAction> Out;
    Gesture.Down(1, {100.0f, 200.0f}, 0, Out);
    Gesture.Motion(1, {101.0f, 170.0f}, Vertical, Out);
    Gesture.Motion(1, {101.0f, 150.0f}, Vertical, Out);
    Gesture.Up(1, {101.0f, 150.0f}, Out);
    REQUIRE(Kinds(Out) == std::vector<Kind>{Kind::Hover, Kind::Scroll, Kind::Scroll, Kind::Leave});
    CHECK(Out[1].Delta.Y == doctest::Approx(-30.0f));
    CHECK(Out[2].Delta.Y == doctest::Approx(-20.0f));
}

TEST_CASE("a drag across what does not scroll that way is a mouse drag from where it began")
{
    TouchGesture Gesture;
    std::vector<TouchAction> Out;
    Gesture.Down(1, {100.0f, 200.0f}, 0, Out);
    // Horizontal, over a list that only scrolls vertically: a slider.
    Gesture.Motion(1, {140.0f, 201.0f}, Vertical, Out);
    Gesture.Up(1, {160.0f, 201.0f}, Out);
    REQUIRE(Kinds(Out) ==
            std::vector<Kind>{Kind::Hover, Kind::Press, Kind::Move, Kind::Move, Kind::Release, Kind::Leave});
    CHECK(Out[1].At.X == doctest::Approx(100.0f));
    CHECK(Out[4].At.X == doctest::Approx(160.0f));

    Out.clear();
    Gesture.Down(2, {10.0f, 10.0f}, 1000, Out);
    Gesture.Motion(2, {10.0f, 60.0f}, Nothing, Out);
    CHECK(Kinds(Out) == std::vector<Kind>{Kind::Hover, Kind::Press, Kind::Move});
}

TEST_CASE("a finger held still starts a mouse drag even where a drag would scroll")
{
    TouchGesture Gesture;
    std::vector<TouchAction> Out;
    Gesture.Down(1, {100.0f, 200.0f}, 1000, Out);
    Gesture.Update(1000 + TouchGesture::KLongPressMs - 1, Out);
    CHECK(Kinds(Out) == std::vector<Kind>{Kind::Hover});
    Gesture.Update(1000 + TouchGesture::KLongPressMs, Out);
    Gesture.Motion(1, {100.0f, 260.0f}, Vertical, Out);
    Gesture.Up(1, {100.0f, 260.0f}, Out);
    CHECK(Kinds(Out) ==
          std::vector<Kind>{Kind::Hover, Kind::Press, Kind::Move, Kind::Move, Kind::Release, Kind::Leave});
}

TEST_CASE("a second finger ends the gesture until every finger is lifted")
{
    TouchGesture Gesture;
    std::vector<TouchAction> Out;
    Gesture.Down(1, {100.0f, 200.0f}, 0, Out);
    Gesture.Motion(1, {160.0f, 200.0f}, Nothing, Out);
    Out.clear();
    Gesture.Down(2, {300.0f, 200.0f}, 10, Out);
    CHECK(Kinds(Out) == std::vector<Kind>{Kind::Release, Kind::Leave});

    Out.clear();
    Gesture.Motion(1, {200.0f, 200.0f}, Nothing, Out);
    Gesture.Up(1, {200.0f, 200.0f}, Out);
    Gesture.Motion(2, {320.0f, 200.0f}, Nothing, Out);
    Gesture.Up(2, {320.0f, 200.0f}, Out);
    CHECK(Out.empty());

    Gesture.Down(3, {50.0f, 50.0f}, 500, Out);
    Gesture.Up(3, {50.0f, 50.0f}, Out);
    CHECK(Kinds(Out) == std::vector<Kind>{Kind::Hover, Kind::Press, Kind::Release, Kind::Leave});
}

TEST_CASE("resetting releases a held button")
{
    TouchGesture Gesture;
    std::vector<TouchAction> Out;
    Gesture.Down(1, {100.0f, 200.0f}, 0, Out);
    Gesture.Update(TouchGesture::KLongPressMs, Out);
    Out.clear();
    Gesture.Reset(Out);
    CHECK(Kinds(Out) == std::vector<Kind>{Kind::Release, Kind::Leave});
    Out.clear();
    Gesture.Reset(Out);
    CHECK(Out.empty());
}
