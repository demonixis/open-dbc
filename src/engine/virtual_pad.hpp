#pragma once

#include <SDL3/SDL_rect.h>

#include <algorithm>
#include <cmath>

namespace dbc
{
    // The keys and mouse buttons a touch screen without a keyboard offers on screen.
    enum class PadButton
    {
        Up,
        Down,
        Left,
        Right,
        Space,
        MouseLeft,
        MouseRight,
    };

    inline constexpr int KPadButtonCount = 7;

    // What stands for the arrow keys: four buttons, or a stick that also reads as a joystick.
    enum class PadStyle
    {
        Arrows,
        Stick,
    };

    // The stick's base, where the arrows otherwise lie: its centre in x and y, its radius in w.
    [[nodiscard]] inline SDL_FRect PadStickBase(float Width, float Height)
    {
        const float Unit = std::min(Width, Height);
        const float Radius = Unit * 0.15f;
        const float Margin = Unit * 0.035f;
        return SDL_FRect{Margin + Radius, Height - Margin - Radius, Radius, 0.0f};
    }

    // Whether a finger landing there takes the stick: a thumb aims loosely.
    [[nodiscard]] inline bool PadStickAt(float X, float Y, float Width, float Height)
    {
        const SDL_FRect Base = PadStickBase(Width, Height);
        return std::hypot(X - Base.x, Y - Base.y) <= Base.w * 1.25f;
    }

    // How far the stick leans under a finger, each axis in -1..1 and the whole no longer than 1.
    [[nodiscard]] inline SDL_FPoint PadStickLean(float X, float Y, float Width, float Height)
    {
        const SDL_FRect Base = PadStickBase(Width, Height);
        SDL_FPoint Lean{(X - Base.x) / Base.w, (Y - Base.y) / Base.w};
        const float Length = std::hypot(Lean.x, Lean.y);
        if (Length > 1.0f)
        {
            Lean.x /= Length;
            Lean.y /= Length;
        }
        return Lean;
    }

    // Past this lean an axis holds its arrow key.
    inline constexpr float KPadStickKeyLean = 0.4f;

    // Where a button lies on a display of the given size: the arrows as on a keyboard at the bottom
    // left, space under the two mouse buttons at the bottom right.
    [[nodiscard]] inline SDL_FRect PadButtonRect(PadButton Button, float Width, float Height)
    {
        const float Unit = std::min(Width, Height);
        const float Size = Unit * 0.13f;
        const float Gap = Unit * 0.02f;
        const float Margin = Unit * 0.035f;
        const float Step = Size + Gap;
        const float Bottom = Height - Margin - Size;
        const float Top = Bottom - Step;
        const float RightEdge = Width - Margin;
        switch (Button)
        {
            case PadButton::Up:
                return SDL_FRect{Margin + Step, Top, Size, Size};
            case PadButton::Down:
                return SDL_FRect{Margin + Step, Bottom, Size, Size};
            case PadButton::Left:
                return SDL_FRect{Margin, Bottom, Size, Size};
            case PadButton::Right:
                return SDL_FRect{Margin + 2.0f * Step, Bottom, Size, Size};
            case PadButton::Space:
                return SDL_FRect{RightEdge - Size - Step, Bottom, Size + Step, Size};
            case PadButton::MouseLeft:
                return SDL_FRect{RightEdge - Size - Step, Top, Size, Size};
            case PadButton::MouseRight:
                return SDL_FRect{RightEdge - Size, Top, Size, Size};
        }
        return SDL_FRect{};
    }

    // The button under a point of the display, or -1.
    [[nodiscard]] inline int PadButtonAt(float X, float Y, float Width, float Height,
                                         PadStyle Style = PadStyle::Arrows)
    {
        // The stick replaces the four arrows, which come first.
        for (int Index = Style == PadStyle::Stick ? 4 : 0; Index < KPadButtonCount; ++Index)
        {
            const SDL_FRect Rect = PadButtonRect(static_cast<PadButton>(Index), Width, Height);
            if (X >= Rect.x && X < Rect.x + Rect.w && Y >= Rect.y && Y < Rect.y + Rect.h)
                return Index;
        }
        return -1;
    }
} // namespace dbc
