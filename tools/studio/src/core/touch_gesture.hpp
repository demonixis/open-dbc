#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        struct TouchPoint
        {
            float X = 0.0f;
            float Y = 0.0f;
        };

        struct TouchAction
        {
            enum class Kind
            {
                // The pointer goes there without a button: what is under the finger gets to know it.
                Hover,
                Press,
                // The pointer moves with the button held.
                Move,
                Release,
                // What is under the finger scrolls by Delta, which follows the finger.
                Scroll,
                // What scrolled goes back to where it was when the finger landed: a drag follows.
                ScrollBack,
                // The pointer leaves the window, so that nothing stays highlighted under a lifted finger.
                Leave,
            };

            Kind Type = Kind::Hover;
            TouchPoint At;
            TouchPoint Delta;
        };

        // Turns one finger into the mouse an interface written for one expects:
        // - a tap is a click;
        // - a drag scrolls what is under the finger when that scrolls along the drag, and is a mouse
        //   drag otherwise (a slider, a splitter, a viewport);
        // - a finger held still, then moved, is always a mouse drag: it selects text in a list that scrolls.
        // - a scroll that began on something draggable and leaves what scrolls becomes a mouse drag of
        //   it: that is how a tile is dragged out of a list.
        // A second finger ends the gesture: two fingers belong to pinches.
        class TouchGesture
        {
          public:
            // Whether what is under the finger scrolls horizontally, or vertically.
            using ScrollQuery = std::function<bool(bool Horizontal)>;
            // Whether the scroll began on something draggable and the finger, now there, has left what scrolls.
            using DragOutQuery = std::function<bool(TouchPoint At)>;

            // How far a finger may slide and still be tapping, in the unit of the points given.
            static constexpr float KSlop = 10.0f;
            static constexpr std::uint64_t KLongPressMs = 400;

            void Down(std::uint64_t Finger, TouchPoint At, std::uint64_t NowMs, std::vector<TouchAction>& Out);
            void Motion(std::uint64_t Finger, TouchPoint At, const ScrollQuery& CanScroll,
                        std::vector<TouchAction>& Out, const DragOutQuery& DragsOut = {});
            void Up(std::uint64_t Finger, TouchPoint At, std::vector<TouchAction>& Out);
            // Call once per frame: a long press has no event of its own.
            void Update(std::uint64_t NowMs, std::vector<TouchAction>& Out);
            // Forgets the fingers, releasing a held button.
            void Reset(std::vector<TouchAction>& Out);

          private:
            enum class State
            {
                Idle,
                // Down, and not yet a tap, a scroll or a drag.
                Pending,
                Scrolling,
                Dragging,
                // More than one finger: nothing until all of them are lifted.
                Ignored,
            };

            State State_ = State::Idle;
            int Fingers_ = 0;
            std::uint64_t Finger_ = 0;
            std::uint64_t DownAtMs_ = 0;
            TouchPoint Start_;
            TouchPoint Last_;
        };
    } // namespace core

} // namespace dbc::studio
