#pragma once

// Pure math / utility helpers shared across engine .cpp files.
// All functions are [[nodiscard]] inline so no .cpp is needed.
// Extracted from resource_systems.cpp and graphics_device.cpp (batch 4.1).

#include "engine_types.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace dbc
{

    // -----------------------------------------------------------------------
    // Scalar
    // -----------------------------------------------------------------------

    [[nodiscard]] inline float DegToRad(float Degrees)
    {
        return Degrees * static_cast<float>(M_PI / 180.0);
    }

    // -----------------------------------------------------------------------
    // Vec3 operations
    // -----------------------------------------------------------------------

    [[nodiscard]] inline Vec3 Vec3Sub(const Vec3& A, const Vec3& B)
    {
        return Vec3{A.X - B.X, A.Y - B.Y, A.Z - B.Z};
    }

    [[nodiscard]] inline float Vec3Dot(const Vec3& A, const Vec3& B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
    }

    [[nodiscard]] inline Vec3 Vec3Cross(const Vec3& A, const Vec3& B)
    {
        return Vec3{
            A.Y * B.Z - A.Z * B.Y,
            A.Z * B.X - A.X * B.Z,
            A.X * B.Y - A.Y * B.X,
        };
    }

    [[nodiscard]] inline Vec3 Vec3Normalize(const Vec3& V)
    {
        const float Len = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
        if (Len <= 0.000001f)
        {
            return Vec3{0.0f, 0.0f, 1.0f};
        }
        const float Inv = 1.0f / Len;
        return Vec3{V.X * Inv, V.Y * Inv, V.Z * Inv};
    }

    // -----------------------------------------------------------------------
    // Vec3 rotation (degrees)
    // -----------------------------------------------------------------------

    [[nodiscard]] inline Vec3 RotateX(const Vec3& P, float Degrees)
    {
        const float A = Degrees * static_cast<float>(M_PI / 180.0);
        const float C = std::cos(A);
        const float S = std::sin(A);
        return Vec3{P.X, P.Y * C - P.Z * S, P.Y * S + P.Z * C};
    }

    [[nodiscard]] inline Vec3 RotateY(const Vec3& P, float Degrees)
    {
        const float A = Degrees * static_cast<float>(M_PI / 180.0);
        const float C = std::cos(A);
        const float S = std::sin(A);
        return Vec3{P.X * C + P.Z * S, P.Y, -P.X * S + P.Z * C};
    }

    [[nodiscard]] inline Vec3 RotateZ(const Vec3& P, float Degrees)
    {
        const float A = Degrees * static_cast<float>(M_PI / 180.0);
        const float C = std::cos(A);
        const float S = std::sin(A);
        return Vec3{P.X * C - P.Y * S, P.X * S + P.Y * C, P.Z};
    }

    // -----------------------------------------------------------------------
    // Mat3 operations
    // -----------------------------------------------------------------------

    [[nodiscard]] inline Mat3 Mat3Mul(const Mat3& A, const Mat3& B)
    {
        Mat3 Out{};
        for (int R = 0; R < 3; ++R)
        {
            for (int C = 0; C < 3; ++C)
            {
                Out.M[R][C] = A.M[R][0] * B.M[0][C] + A.M[R][1] * B.M[1][C] + A.M[R][2] * B.M[2][C];
            }
        }
        return Out;
    }

    [[nodiscard]] inline Vec3 Mat3Apply(const Vec3& V, const Mat3& M)
    {
        return Vec3{
            V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0],
            V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1],
            V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2],
        };
    }

    [[nodiscard]] inline Mat3 Mat3RotateX(float Degrees)
    {
        const float A = DegToRad(Degrees);
        const float C = std::cos(A);
        const float S = std::sin(A);
        Mat3 Out{};
        Out.M[0][0] = 1.0f;
        Out.M[0][1] = 0.0f;
        Out.M[0][2] = 0.0f;
        Out.M[1][0] = 0.0f;
        Out.M[1][1] = C;
        Out.M[1][2] = S;
        Out.M[2][0] = 0.0f;
        Out.M[2][1] = -S;
        Out.M[2][2] = C;
        return Out;
    }

    [[nodiscard]] inline Mat3 Mat3RotateY(float Degrees)
    {
        const float A = DegToRad(Degrees);
        const float C = std::cos(A);
        const float S = std::sin(A);
        Mat3 Out{};
        Out.M[0][0] = C;
        Out.M[0][1] = 0.0f;
        Out.M[0][2] = -S;
        Out.M[1][0] = 0.0f;
        Out.M[1][1] = 1.0f;
        Out.M[1][2] = 0.0f;
        Out.M[2][0] = S;
        Out.M[2][1] = 0.0f;
        Out.M[2][2] = C;
        return Out;
    }

    [[nodiscard]] inline Mat3 Mat3RotateZ(float Degrees)
    {
        const float A = DegToRad(Degrees);
        const float C = std::cos(A);
        const float S = std::sin(A);
        Mat3 Out{};
        Out.M[0][0] = C;
        Out.M[0][1] = S;
        Out.M[0][2] = 0.0f;
        Out.M[1][0] = -S;
        Out.M[1][1] = C;
        Out.M[1][2] = 0.0f;
        Out.M[2][0] = 0.0f;
        Out.M[2][1] = 0.0f;
        Out.M[2][2] = 1.0f;
        return Out;
    }

    [[nodiscard]] inline Mat3 Mat3FromEulerXyz(float XDegrees, float YDegrees, float ZDegrees)
    {
        return Mat3Mul(Mat3Mul(Mat3RotateX(XDegrees), Mat3RotateY(YDegrees)), Mat3RotateZ(ZDegrees));
    }

    [[nodiscard]] inline Mat3 Mat3FromEulerZyx(float XDegrees, float YDegrees, float ZDegrees)
    {
        return Mat3Mul(Mat3Mul(Mat3RotateZ(ZDegrees), Mat3RotateY(YDegrees)), Mat3RotateX(XDegrees));
    }

    // -----------------------------------------------------------------------
    // Color
    // -----------------------------------------------------------------------

    [[nodiscard]] inline SDL_FColor UnpackColor(int Packed)
    {
        // Packed colours follow DarkBASIC: 0x00RRGGBB.
        const float R = static_cast<float>((Packed >> 16) & 0xFF) / 255.0f;
        const float G = static_cast<float>((Packed >> 8) & 0xFF) / 255.0f;
        const float B = static_cast<float>(Packed & 0xFF) / 255.0f;
        return SDL_FColor{R, G, B, 1.0f};
    }

    // -----------------------------------------------------------------------
    // Path helpers
    // -----------------------------------------------------------------------

    [[nodiscard]] inline std::string ToLowerAscii(std::string S)
    {
        std::transform(S.begin(), S.end(), S.begin(),
                       [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        return S;
    }

    inline bool PathExists(const std::filesystem::path& Path, bool& Exists, std::string* Error = nullptr)
    {
        std::error_code FileError;
        const bool Found = std::filesystem::exists(Path, FileError);
        if (FileError && FileError != std::errc::no_such_file_or_directory && FileError != std::errc::not_a_directory)
        {
            if (Error)
                *Error = "Cannot inspect path '" + Path.string() + "': " + FileError.message();
            return false;
        }
        Exists = Found;
        return true;
    }

    inline bool ResolveCaseInsensitivePath(std::filesystem::path& Output, const std::filesystem::path& Base,
                                           const std::filesystem::path& Relative, std::string* Error = nullptr)
    {
        namespace fs = std::filesystem;
        fs::path Current = Relative.is_absolute() ? Relative.root_path() : Base;
        const fs::path Remainder = Relative.is_absolute() ? Relative.relative_path() : Relative;
        for (const auto& Part : Remainder)
        {
            const std::string Piece = Part.string();
            if (Piece.empty() || Piece == ".")
                continue;
            if (Piece == "..")
            {
                Current = Current.parent_path();
                continue;
            }
            const fs::path Exact = Current / Part;
            bool Exists = false;
            if (!PathExists(Exact, Exists, Error))
                return false;
            if (Exists)
            {
                Current = Exact;
                continue;
            }
            if (!PathExists(Current, Exists, Error))
                return false;
            if (!Exists)
            {
                Output.clear();
                return true;
            }
            std::error_code FileError;
            const bool Directory = fs::is_directory(Current, FileError);
            if (FileError)
            {
                if (Error)
                    *Error = "Cannot inspect directory '" + Current.string() + "': " + FileError.message();
                return false;
            }
            if (!Directory)
            {
                Output.clear();
                return true;
            }
            const std::string Wanted = ToLowerAscii(Piece);
            fs::path Found;
            for (fs::directory_iterator It(Current, FileError), End; !FileError && It != End; It.increment(FileError))
            {
                if (ToLowerAscii(It->path().filename().string()) == Wanted)
                {
                    Found = It->path();
                    break;
                }
            }
            if (FileError)
            {
                if (Error)
                    *Error = "Cannot enumerate directory '" + Current.string() + "': " + FileError.message();
                return false;
            }
            if (Found.empty())
            {
                Output.clear();
                return true;
            }
            Current = std::move(Found);
        }
        bool Exists = false;
        if (!PathExists(Current, Exists, Error))
            return false;
        Output = Exists ? Current : fs::path{};
        return true;
    }

} // namespace dbc
