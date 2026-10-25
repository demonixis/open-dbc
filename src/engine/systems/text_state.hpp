#pragma once

#include <string>

namespace dbc
{
    struct BasicTextState
    {
        int CursorX = 0;
        int CursorY = 0;
        int Size = 16;
        std::string Font = "Arial";
        int Style = 0;
        bool Opaque = false;
    };

} // namespace dbc
