#pragma once

#include <array>
#include <cmath>

// 4x4 matrix maths for the DBPro MATRIX4 commands.
//
// Layout and conventions follow DarkBASIC Professional, which sat on DirectX 9:
// row-major storage with the translation in the fourth row, and row vectors, so
// a point is transformed as v * M and matrices concatenate left to right.
// Keeping that convention is what makes MULTIPLY MATRIX4 produce the same
// result a DBPro program expects.
//
// Deliberately free of glm: the interpreter does not depend on it, and the
// vector commands already store their data as plain float arrays.
namespace dbc::interpreter::detail
{
    using Mat4 = std::array<float, 16>;

    constexpr int Mat4Index(int Row, int Column)
    {
        return Row * 4 + Column;
    }

    inline Mat4 Mat4Identity()
    {
        return Mat4{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    }

    inline Mat4 Mat4Add(const Mat4& A, const Mat4& B)
    {
        Mat4 Out{};
        for (std::size_t I = 0; I < Out.size(); ++I)
        {
            Out[I] = A[I] + B[I];
        }
        return Out;
    }

    inline Mat4 Mat4Subtract(const Mat4& A, const Mat4& B)
    {
        Mat4 Out{};
        for (std::size_t I = 0; I < Out.size(); ++I)
        {
            Out[I] = A[I] - B[I];
        }
        return Out;
    }

    inline Mat4 Mat4Scale(const Mat4& M, float Value)
    {
        Mat4 Out{};
        for (std::size_t I = 0; I < Out.size(); ++I)
        {
            Out[I] = M[I] * Value;
        }
        return Out;
    }

    inline Mat4 Mat4Multiply(const Mat4& A, const Mat4& B)
    {
        Mat4 Out{};
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Column = 0; Column < 4; ++Column)
            {
                float Sum = 0.0f;
                for (int K = 0; K < 4; ++K)
                {
                    Sum += A[Mat4Index(Row, K)] * B[Mat4Index(K, Column)];
                }
                Out[Mat4Index(Row, Column)] = Sum;
            }
        }
        return Out;
    }

    inline Mat4 Mat4Transpose(const Mat4& M)
    {
        Mat4 Out{};
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Column = 0; Column < 4; ++Column)
            {
                Out[Mat4Index(Row, Column)] = M[Mat4Index(Column, Row)];
            }
        }
        return Out;
    }

    inline float Mat4Determinant(const Mat4& M)
    {
        const float A0 = M[0] * M[5] - M[1] * M[4];
        const float A1 = M[0] * M[6] - M[2] * M[4];
        const float A2 = M[0] * M[7] - M[3] * M[4];
        const float A3 = M[1] * M[6] - M[2] * M[5];
        const float A4 = M[1] * M[7] - M[3] * M[5];
        const float A5 = M[2] * M[7] - M[3] * M[6];
        const float B0 = M[8] * M[13] - M[9] * M[12];
        const float B1 = M[8] * M[14] - M[10] * M[12];
        const float B2 = M[8] * M[15] - M[11] * M[12];
        const float B3 = M[9] * M[14] - M[10] * M[13];
        const float B4 = M[9] * M[15] - M[11] * M[13];
        const float B5 = M[10] * M[15] - M[11] * M[14];
        return A0 * B5 - A1 * B4 + A2 * B3 + A3 * B2 - A4 * B1 + A5 * B0;
    }

    // Returns the identity when the matrix is singular, which is what DBPro
    // does rather than producing infinities.
    inline Mat4 Mat4Inverse(const Mat4& M)
    {
        const float A0 = M[0] * M[5] - M[1] * M[4];
        const float A1 = M[0] * M[6] - M[2] * M[4];
        const float A2 = M[0] * M[7] - M[3] * M[4];
        const float A3 = M[1] * M[6] - M[2] * M[5];
        const float A4 = M[1] * M[7] - M[3] * M[5];
        const float A5 = M[2] * M[7] - M[3] * M[6];
        const float B0 = M[8] * M[13] - M[9] * M[12];
        const float B1 = M[8] * M[14] - M[10] * M[12];
        const float B2 = M[8] * M[15] - M[11] * M[12];
        const float B3 = M[9] * M[14] - M[10] * M[13];
        const float B4 = M[9] * M[15] - M[11] * M[13];
        const float B5 = M[10] * M[15] - M[11] * M[14];

        const float Det = A0 * B5 - A1 * B4 + A2 * B3 + A3 * B2 - A4 * B1 + A5 * B0;
        if (std::fabs(Det) < 1e-12f)
        {
            return Mat4Identity();
        }
        const float Inv = 1.0f / Det;

        Mat4 Out{};
        Out[0] = (M[5] * B5 - M[6] * B4 + M[7] * B3) * Inv;
        Out[1] = (-M[1] * B5 + M[2] * B4 - M[3] * B3) * Inv;
        Out[2] = (M[13] * A5 - M[14] * A4 + M[15] * A3) * Inv;
        Out[3] = (-M[9] * A5 + M[10] * A4 - M[11] * A3) * Inv;
        Out[4] = (-M[4] * B5 + M[6] * B2 - M[7] * B1) * Inv;
        Out[5] = (M[0] * B5 - M[2] * B2 + M[3] * B1) * Inv;
        Out[6] = (-M[12] * A5 + M[14] * A2 - M[15] * A1) * Inv;
        Out[7] = (M[8] * A5 - M[10] * A2 + M[11] * A1) * Inv;
        Out[8] = (M[4] * B4 - M[5] * B2 + M[7] * B0) * Inv;
        Out[9] = (-M[0] * B4 + M[1] * B2 - M[3] * B0) * Inv;
        Out[10] = (M[12] * A4 - M[13] * A2 + M[15] * A0) * Inv;
        Out[11] = (-M[8] * A4 + M[9] * A2 - M[11] * A0) * Inv;
        Out[12] = (-M[4] * B3 + M[5] * B1 - M[6] * B0) * Inv;
        Out[13] = (M[0] * B3 - M[1] * B1 + M[2] * B0) * Inv;
        Out[14] = (-M[12] * A3 + M[13] * A1 - M[14] * A0) * Inv;
        Out[15] = (M[8] * A3 - M[9] * A1 + M[10] * A0) * Inv;
        return Out;
    }

    inline Mat4 Mat4Translation(float X, float Y, float Z)
    {
        Mat4 Out = Mat4Identity();
        Out[Mat4Index(3, 0)] = X;
        Out[Mat4Index(3, 1)] = Y;
        Out[Mat4Index(3, 2)] = Z;
        return Out;
    }

    inline Mat4 Mat4Scaling(float X, float Y, float Z)
    {
        Mat4 Out = Mat4Identity();
        Out[Mat4Index(0, 0)] = X;
        Out[Mat4Index(1, 1)] = Y;
        Out[Mat4Index(2, 2)] = Z;
        return Out;
    }

    inline float Mat4DegreesToRadians(float Degrees)
    {
        return Degrees * 3.14159265358979323846f / 180.0f;
    }

    inline Mat4 Mat4RotationX(float Radians)
    {
        const float C = std::cos(Radians);
        const float S = std::sin(Radians);
        Mat4 Out = Mat4Identity();
        Out[Mat4Index(1, 1)] = C;
        Out[Mat4Index(1, 2)] = S;
        Out[Mat4Index(2, 1)] = -S;
        Out[Mat4Index(2, 2)] = C;
        return Out;
    }

    inline Mat4 Mat4RotationY(float Radians)
    {
        const float C = std::cos(Radians);
        const float S = std::sin(Radians);
        Mat4 Out = Mat4Identity();
        Out[Mat4Index(0, 0)] = C;
        Out[Mat4Index(0, 2)] = -S;
        Out[Mat4Index(2, 0)] = S;
        Out[Mat4Index(2, 2)] = C;
        return Out;
    }

    inline Mat4 Mat4RotationZ(float Radians)
    {
        const float C = std::cos(Radians);
        const float S = std::sin(Radians);
        Mat4 Out = Mat4Identity();
        Out[Mat4Index(0, 0)] = C;
        Out[Mat4Index(0, 1)] = S;
        Out[Mat4Index(1, 0)] = -S;
        Out[Mat4Index(1, 1)] = C;
        return Out;
    }

    inline Mat4 Mat4RotationYpr(float Yaw, float Pitch, float Roll)
    {
        return Mat4Multiply(Mat4Multiply(Mat4RotationZ(Roll), Mat4RotationX(Pitch)), Mat4RotationY(Yaw));
    }

    inline Mat4 Mat4RotationAxis(float X, float Y, float Z, float Radians)
    {
        const float Length = std::sqrt(X * X + Y * Y + Z * Z);
        if (Length > 1e-12f)
        {
            X /= Length;
            Y /= Length;
            Z /= Length;
        }

        const float C = std::cos(Radians);
        const float S = std::sin(Radians);
        const float T = 1.0f - C;
        return Mat4{T * X * X + C,
                    T * X * Y + S * Z,
                    T * X * Z - S * Y,
                    0.0f,
                    T * X * Y - S * Z,
                    T * Y * Y + C,
                    T * Y * Z + S * X,
                    0.0f,
                    T * X * Z + S * Y,
                    T * Y * Z - S * X,
                    T * Z * Z + C,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f};
    }

    inline Mat4 Mat4Reflection(float A, float B, float C, float D)
    {
        const float Length = std::sqrt(A * A + B * B + C * C);
        if (Length > 1e-12f)
        {
            A /= Length;
            B /= Length;
            C /= Length;
            D /= Length;
        }
        return Mat4{1.0f - 2.0f * A * A, -2.0f * A * B,       -2.0f * A * C,       0.0f,
                    -2.0f * A * B,       1.0f - 2.0f * B * B, -2.0f * B * C,       0.0f,
                    -2.0f * A * C,       -2.0f * B * C,       1.0f - 2.0f * C * C, 0.0f,
                    -2.0f * A * D,       -2.0f * B * D,       -2.0f * C * D,       1.0f};
    }

    inline Mat4 Mat4PerspectiveFov(float FovRadians, float Aspect, float NearPlane, float FarPlane, bool LeftHanded)
    {
        const float H = 1.0f / std::tan(FovRadians * 0.5f);
        const float W = H / (Aspect != 0.0f ? Aspect : 1.0f);
        const float Range = FarPlane - NearPlane;

        Mat4 Out{};
        Out[Mat4Index(0, 0)] = W;
        Out[Mat4Index(1, 1)] = H;
        Out[Mat4Index(2, 2)] = LeftHanded ? (FarPlane / Range) : (FarPlane / -Range);
        Out[Mat4Index(2, 3)] = LeftHanded ? 1.0f : -1.0f;
        Out[Mat4Index(3, 2)] = -NearPlane * FarPlane / Range;
        return Out;
    }

    // D3DXMatrixPerspectiveLH/RH: the view volume is given by its width and
    // height at the near plane, not by a field of view.
    inline Mat4 Mat4PerspectiveWH(float Width, float Height, float NearPlane, float FarPlane, bool LeftHanded)
    {
        const float Range = FarPlane - NearPlane;

        Mat4 Out{};
        Out[Mat4Index(0, 0)] = 2.0f * NearPlane / (Width != 0.0f ? Width : 1.0f);
        Out[Mat4Index(1, 1)] = 2.0f * NearPlane / (Height != 0.0f ? Height : 1.0f);
        Out[Mat4Index(2, 2)] = LeftHanded ? (FarPlane / Range) : (FarPlane / -Range);
        Out[Mat4Index(2, 3)] = LeftHanded ? 1.0f : -1.0f;
        Out[Mat4Index(3, 2)] = -NearPlane * FarPlane / Range;
        return Out;
    }

    inline Mat4 Mat4Ortho(float Width, float Height, float NearPlane, float FarPlane, bool LeftHanded)
    {
        const float Range = FarPlane - NearPlane;

        Mat4 Out = Mat4Identity();
        Out[Mat4Index(0, 0)] = 2.0f / (Width != 0.0f ? Width : 1.0f);
        Out[Mat4Index(1, 1)] = 2.0f / (Height != 0.0f ? Height : 1.0f);
        Out[Mat4Index(2, 2)] = LeftHanded ? (1.0f / Range) : (1.0f / -Range);
        Out[Mat4Index(3, 2)] = -NearPlane / Range;
        return Out;
    }

    inline Mat4 Mat4LookAt(float EyeX, float EyeY, float EyeZ, float AtX, float AtY, float AtZ, float UpX, float UpY,
                           float UpZ, bool LeftHanded)
    {
        const auto Normalize = [](float& X, float& Y, float& Z)
        {
            const float Length = std::sqrt(X * X + Y * Y + Z * Z);
            if (Length > 1e-12f)
            {
                X /= Length;
                Y /= Length;
                Z /= Length;
            }
        };
        const auto Cross =
            [](float Ax, float Ay, float Az, float Bx, float By, float Bz, float& Ox, float& Oy, float& Oz)
        {
            Ox = Ay * Bz - Az * By;
            Oy = Az * Bx - Ax * Bz;
            Oz = Ax * By - Ay * Bx;
        };

        float Zx = LeftHanded ? (AtX - EyeX) : (EyeX - AtX);
        float Zy = LeftHanded ? (AtY - EyeY) : (EyeY - AtY);
        float Zz = LeftHanded ? (AtZ - EyeZ) : (EyeZ - AtZ);
        Normalize(Zx, Zy, Zz);

        float Xx = 0.0f;
        float Xy = 0.0f;
        float Xz = 0.0f;
        Cross(UpX, UpY, UpZ, Zx, Zy, Zz, Xx, Xy, Xz);
        Normalize(Xx, Xy, Xz);

        float Yx = 0.0f;
        float Yy = 0.0f;
        float Yz = 0.0f;
        Cross(Zx, Zy, Zz, Xx, Xy, Xz, Yx, Yy, Yz);

        Mat4 Out = Mat4Identity();
        Out[Mat4Index(0, 0)] = Xx;
        Out[Mat4Index(1, 0)] = Xy;
        Out[Mat4Index(2, 0)] = Xz;
        Out[Mat4Index(0, 1)] = Yx;
        Out[Mat4Index(1, 1)] = Yy;
        Out[Mat4Index(2, 1)] = Yz;
        Out[Mat4Index(0, 2)] = Zx;
        Out[Mat4Index(1, 2)] = Zy;
        Out[Mat4Index(2, 2)] = Zz;
        Out[Mat4Index(3, 0)] = -(Xx * EyeX + Xy * EyeY + Xz * EyeZ);
        Out[Mat4Index(3, 1)] = -(Yx * EyeX + Yy * EyeY + Yz * EyeZ);
        Out[Mat4Index(3, 2)] = -(Zx * EyeX + Zy * EyeY + Zz * EyeZ);
        return Out;
    }

    inline bool Mat4IsIdentity(const Mat4& M)
    {
        const Mat4 Identity = Mat4Identity();
        for (int I = 0; I < 16; ++I)
        {
            if (M[I] != Identity[I])
            {
                return false;
            }
        }
        return true;
    }

    inline bool Mat4IsEqual(const Mat4& A, const Mat4& B)
    {
        for (int I = 0; I < 16; ++I)
        {
            if (A[I] != B[I])
            {
                return false;
            }
        }
        return true;
    }
} // namespace dbc::interpreter::detail
