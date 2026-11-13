#include "touch_gesture.hpp"

#include <cmath>

namespace dbc::studio
{
    namespace core
    {
        void TouchGesture::Down(std::uint64_t Finger, TouchPoint At, std::uint64_t NowMs, std::vector<TouchAction>& Out)
        {
            ++Fingers_;
            if (Fingers_ > 1)
            {
                if (State_ == State::Dragging)
                    Out.push_back({TouchAction::Kind::Release, Last_, {}});
                if (State_ != State::Ignored)
                    Out.push_back({TouchAction::Kind::Leave, Last_, {}});
                State_ = State::Ignored;
                return;
            }
            State_ = State::Pending;
            Finger_ = Finger;
            DownAtMs_ = NowMs;
            Start_ = At;
            Last_ = At;
            Out.push_back({TouchAction::Kind::Hover, At, {}});
        }

        void TouchGesture::Motion(std::uint64_t Finger, TouchPoint At, const ScrollQuery& CanScroll,
                                  std::vector<TouchAction>& Out)
        {
            if (Finger != Finger_ || State_ == State::Idle || State_ == State::Ignored)
                return;
            const TouchPoint Delta{At.X - Last_.X, At.Y - Last_.Y};
            if (State_ == State::Pending)
            {
                const float FromStartX = At.X - Start_.X;
                const float FromStartY = At.Y - Start_.Y;
                if (std::hypot(FromStartX, FromStartY) <= KSlop)
                    return;
                if (CanScroll && CanScroll(std::fabs(FromStartX) > std::fabs(FromStartY)))
                {
                    State_ = State::Scrolling;
                    Out.push_back({TouchAction::Kind::Scroll, At, {FromStartX, FromStartY}});
                }
                else
                {
                    State_ = State::Dragging;
                    Out.push_back({TouchAction::Kind::Press, Start_, {}});
                    Out.push_back({TouchAction::Kind::Move, At, {}});
                }
            }
            else if (State_ == State::Scrolling)
            {
                Out.push_back({TouchAction::Kind::Scroll, At, Delta});
            }
            else
            {
                Out.push_back({TouchAction::Kind::Move, At, {}});
            }
            Last_ = At;
        }

        void TouchGesture::Up(std::uint64_t Finger, TouchPoint At, std::vector<TouchAction>& Out)
        {
            if (Fingers_ > 0)
                --Fingers_;
            if (State_ == State::Ignored || State_ == State::Idle)
            {
                if (Fingers_ == 0)
                    State_ = State::Idle;
                return;
            }
            if (Finger != Finger_)
                return;
            if (State_ == State::Pending)
            {
                Out.push_back({TouchAction::Kind::Press, Start_, {}});
                Out.push_back({TouchAction::Kind::Release, Start_, {}});
            }
            else if (State_ == State::Dragging)
            {
                Out.push_back({TouchAction::Kind::Move, At, {}});
                Out.push_back({TouchAction::Kind::Release, At, {}});
            }
            Out.push_back({TouchAction::Kind::Leave, At, {}});
            State_ = Fingers_ == 0 ? State::Idle : State::Ignored;
        }

        void TouchGesture::Update(std::uint64_t NowMs, std::vector<TouchAction>& Out)
        {
            if (State_ != State::Pending || NowMs - DownAtMs_ < KLongPressMs)
                return;
            State_ = State::Dragging;
            Out.push_back({TouchAction::Kind::Press, Start_, {}});
        }

        void TouchGesture::Reset(std::vector<TouchAction>& Out)
        {
            if (State_ == State::Dragging)
                Out.push_back({TouchAction::Kind::Release, Last_, {}});
            if (State_ != State::Idle && State_ != State::Ignored)
                Out.push_back({TouchAction::Kind::Leave, Last_, {}});
            State_ = State::Idle;
            Fingers_ = 0;
        }
    } // namespace core

} // namespace dbc::studio
