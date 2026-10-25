#pragma once

#include <array>
#include <cmath>

#include "dba_matrix4.hpp"

// Vector maths for the DBPro VECTOR2 / VECTOR3 / VECTOR4 commands.
//
// The operations are identical whatever the width, so they are written once
// over std::array<float, N> and instantiated for 2, 3 and 4. The interpolators
// (Hermite, Catmull-Rom, barycentric) follow the D3DX formulas DBPro used.
namespace dbc::interpreter::detail
{
    template <std::size_t N> using VecN = std::array<float, N>;

    template <std::size_t N> VecN<N> VecAdd(const VecN<N>& A, const VecN<N>& B)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = A[I] + B[I];
        }
        return Out;
    }

    template <std::size_t N> VecN<N> VecSub(const VecN<N>& A, const VecN<N>& B)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = A[I] - B[I];
        }
        return Out;
    }

    template <std::size_t N> VecN<N> VecScale(const VecN<N>& V, float K)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = V[I] * K;
        }
        return Out;
    }

    template <std::size_t N> float VecSquaredLength(const VecN<N>& V)
    {
        float Sum = 0.0f;
        for (std::size_t I = 0; I < N; ++I)
        {
            Sum += V[I] * V[I];
        }
        return Sum;
    }

    template <std::size_t N> float VecLength(const VecN<N>& V)
    {
        return std::sqrt(VecSquaredLength(V));
    }

    template <std::size_t N> VecN<N> VecNormalize(const VecN<N>& V)
    {
        const float Length = VecLength(V);
        return (Length > 1e-12f) ? VecScale(V, 1.0f / Length) : VecN<N>{};
    }

    template <std::size_t N> float VecDot(const VecN<N>& A, const VecN<N>& B)
    {
        float Sum = 0.0f;
        for (std::size_t I = 0; I < N; ++I)
        {
            Sum += A[I] * B[I];
        }
        return Sum;
    }

    template <std::size_t N> VecN<N> VecMaximize(const VecN<N>& A, const VecN<N>& B)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = (A[I] > B[I]) ? A[I] : B[I];
        }
        return Out;
    }

    template <std::size_t N> VecN<N> VecMinimize(const VecN<N>& A, const VecN<N>& B)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = (A[I] < B[I]) ? A[I] : B[I];
        }
        return Out;
    }

    template <std::size_t N> VecN<N> VecLerp(const VecN<N>& A, const VecN<N>& B, float S)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = A[I] + (B[I] - A[I]) * S;
        }
        return Out;
    }

    template <std::size_t N> bool VecIsEqual(const VecN<N>& A, const VecN<N>& B)
    {
        for (std::size_t I = 0; I < N; ++I)
        {
            if (A[I] != B[I])
            {
                return false;
            }
        }
        return true;
    }

    // Hermite spline through p0 with tangent t0 and p1 with tangent t1.
    template <std::size_t N>
    VecN<N> VecHermite(const VecN<N>& P0, const VecN<N>& T0, const VecN<N>& P1, const VecN<N>& T1, float S)
    {
        const float S2 = S * S;
        const float S3 = S2 * S;
        const float H1 = 2.0f * S3 - 3.0f * S2 + 1.0f;
        const float H2 = -2.0f * S3 + 3.0f * S2;
        const float H3 = S3 - 2.0f * S2 + S;
        const float H4 = S3 - S2;

        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = P0[I] * H1 + P1[I] * H2 + T0[I] * H3 + T1[I] * H4;
        }
        return Out;
    }

    template <std::size_t N>
    VecN<N> VecCatmullRom(const VecN<N>& P0, const VecN<N>& P1, const VecN<N>& P2, const VecN<N>& P3, float S)
    {
        const float S2 = S * S;
        const float S3 = S2 * S;

        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = 0.5f * ((2.0f * P1[I]) + (-P0[I] + P2[I]) * S +
                             (2.0f * P0[I] - 5.0f * P1[I] + 4.0f * P2[I] - P3[I]) * S2 +
                             (-P0[I] + 3.0f * P1[I] - 3.0f * P2[I] + P3[I]) * S3);
        }
        return Out;
    }

    // Barycentric coordinate: p0 + f*(p1-p0) + g*(p2-p0)
    template <std::size_t N>
    VecN<N> VecBarycentric(const VecN<N>& P0, const VecN<N>& P1, const VecN<N>& P2, float F, float G)
    {
        VecN<N> Out{};
        for (std::size_t I = 0; I < N; ++I)
        {
            Out[I] = P0[I] + F * (P1[I] - P0[I]) + G * (P2[I] - P0[I]);
        }
        return Out;
    }

    // Transforms a point: the translation row applies and the result is divided
    // by w, as D3DXVec3TransformCoord does.
    inline VecN<3> VecTransformCoords3(const VecN<3>& V, const Mat4& M)
    {
        const float X = V[0] * M[0] + V[1] * M[4] + V[2] * M[8] + M[12];
        const float Y = V[0] * M[1] + V[1] * M[5] + V[2] * M[9] + M[13];
        const float Z = V[0] * M[2] + V[1] * M[6] + V[2] * M[10] + M[14];
        const float W = V[0] * M[3] + V[1] * M[7] + V[2] * M[11] + M[15];
        const float Inv = (std::fabs(W) > 1e-12f) ? 1.0f / W : 1.0f;
        return VecN<3>{X * Inv, Y * Inv, Z * Inv};
    }

    // Transforms a direction: the translation row is ignored.
    inline VecN<3> VecTransformNormals3(const VecN<3>& V, const Mat4& M)
    {
        return VecN<3>{V[0] * M[0] + V[1] * M[4] + V[2] * M[8], V[0] * M[1] + V[1] * M[5] + V[2] * M[9],
                       V[0] * M[2] + V[1] * M[6] + V[2] * M[10]};
    }

    inline VecN<2> VecTransformCoords2(const VecN<2>& V, const Mat4& M)
    {
        const float X = V[0] * M[0] + V[1] * M[4] + M[12];
        const float Y = V[0] * M[1] + V[1] * M[5] + M[13];
        const float W = V[0] * M[3] + V[1] * M[7] + M[15];
        const float Inv = (std::fabs(W) > 1e-12f) ? 1.0f / W : 1.0f;
        return VecN<2>{X * Inv, Y * Inv};
    }

    inline VecN<4> VecTransform4(const VecN<4>& V, const Mat4& M)
    {
        return VecN<4>{V[0] * M[0] + V[1] * M[4] + V[2] * M[8] + V[3] * M[12],
                       V[0] * M[1] + V[1] * M[5] + V[2] * M[9] + V[3] * M[13],
                       V[0] * M[2] + V[1] * M[6] + V[2] * M[10] + V[3] * M[14],
                       V[0] * M[3] + V[1] * M[7] + V[2] * M[11] + V[3] * M[15]};
    }

    inline VecN<3> VecProject3(const VecN<3>& V, const Mat4& Projection, const Mat4& View, const Mat4& World,
                               float ViewportWidth, float ViewportHeight)
    {
        const VecN<3> Ndc = VecTransformCoords3(V, Mat4Multiply(Mat4Multiply(World, View), Projection));
        return VecN<3>{(1.0f + Ndc[0]) * ViewportWidth * 0.5f, (1.0f - Ndc[1]) * ViewportHeight * 0.5f, Ndc[2]};
    }

    // Z component of the 2D cross product: positive when b turns left of a.
    inline float VecCcw2(const VecN<2>& A, const VecN<2>& B)
    {
        return A[0] * B[1] - A[1] * B[0];
    }
} // namespace dbc::interpreter::detail
