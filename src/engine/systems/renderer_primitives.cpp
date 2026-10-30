#include "engine_kernel.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    void AddTriangle(Mesh& MeshValue, const Vec3& A, const Vec3& B, const Vec3& C, SDL_FPoint Ta, SDL_FPoint Tb,
                     SDL_FPoint Tc)
    {
        MeshGeometry& Geometry = MeshValue.EditGeometry();
        const uint32_t Base = static_cast<uint32_t>(Geometry.Vertices.size());
        Geometry.Vertices.push_back(A);
        Geometry.Vertices.push_back(B);
        Geometry.Vertices.push_back(C);
        Geometry.Uvs.push_back(Ta);
        Geometry.Uvs.push_back(Tb);
        Geometry.Uvs.push_back(Tc);
        Geometry.Indices.push_back(Base + 0);
        Geometry.Indices.push_back(Base + 1);
        Geometry.Indices.push_back(Base + 2);
    }

    void RegenerateMeshNormals(Mesh& MeshValue)
    {
        MeshGeometry& Geometry = MeshValue.EditGeometry();
        if (Geometry.Vertices.empty())
        {
            Geometry.Normals.clear();
            return;
        }
        Geometry.Normals.assign(Geometry.Vertices.size(), Vec3{0.0f, 0.0f, 0.0f});
        for (size_t I = 0; I + 2 < Geometry.Indices.size(); I += 3)
        {
            const uint32_t I0 = Geometry.Indices[I];
            const uint32_t I1 = Geometry.Indices[I + 1];
            const uint32_t I2 = Geometry.Indices[I + 2];
            if (I0 >= Geometry.Vertices.size() || I1 >= Geometry.Vertices.size() || I2 >= Geometry.Vertices.size())
            {
                continue;
            }
            const Vec3 E1 = Vec3Sub(Geometry.Vertices[I1], Geometry.Vertices[I0]);
            const Vec3 E2 = Vec3Sub(Geometry.Vertices[I2], Geometry.Vertices[I0]);
            const Vec3 Fn = Vec3Normalize(Vec3Cross(E1, E2));
            Geometry.Normals[I0].X += Fn.X;
            Geometry.Normals[I0].Y += Fn.Y;
            Geometry.Normals[I0].Z += Fn.Z;
            Geometry.Normals[I1].X += Fn.X;
            Geometry.Normals[I1].Y += Fn.Y;
            Geometry.Normals[I1].Z += Fn.Z;
            Geometry.Normals[I2].X += Fn.X;
            Geometry.Normals[I2].Y += Fn.Y;
            Geometry.Normals[I2].Z += Fn.Z;
        }
        for (Vec3& N : Geometry.Normals)
        {
            N = Vec3Normalize(N);
        }
    }

    Mesh MakePlainMesh(float Width, float Height)
    {
        Mesh MeshValue;
        const float Hw = Width * 0.5f;
        const float Hh = Height * 0.5f;
        const Vec3 A{-Hw, Hh, 0.0f};
        const Vec3 B{Hw, Hh, 0.0f};
        const Vec3 C{Hw, -Hh, 0.0f};
        const Vec3 D{-Hw, -Hh, 0.0f};
        AddTriangle(MeshValue, A, C, B, SDL_FPoint{0.0f, 0.0f}, SDL_FPoint{1.0f, 1.0f}, SDL_FPoint{1.0f, 0.0f});
        AddTriangle(MeshValue, A, D, C, SDL_FPoint{0.0f, 0.0f}, SDL_FPoint{0.0f, 1.0f}, SDL_FPoint{1.0f, 1.0f});
        return MeshValue;
    }

    Mesh MakeBoxMesh(float Width, float Height, float Depth)
    {
        Mesh MeshValue;
        const float Hx = Width * 0.5f;
        const float Hy = Height * 0.5f;
        const float Hz = Depth * 0.5f;

        const Vec3 P000{-Hx, -Hy, -Hz};
        const Vec3 P001{-Hx, -Hy, Hz};
        const Vec3 P010{-Hx, Hy, -Hz};
        const Vec3 P011{-Hx, Hy, Hz};
        const Vec3 P100{Hx, -Hy, -Hz};
        const Vec3 P101{Hx, -Hy, Hz};
        const Vec3 P110{Hx, Hy, -Hz};
        const Vec3 P111{Hx, Hy, Hz};

        AddTriangle(MeshValue, P001, P101, P111);
        AddTriangle(MeshValue, P001, P111, P011);

        AddTriangle(MeshValue, P100, P000, P010);
        AddTriangle(MeshValue, P100, P010, P110);

        AddTriangle(MeshValue, P000, P001, P011);
        AddTriangle(MeshValue, P000, P011, P010);

        AddTriangle(MeshValue, P101, P100, P110);
        AddTriangle(MeshValue, P101, P110, P111);

        AddTriangle(MeshValue, P010, P011, P111);
        AddTriangle(MeshValue, P010, P111, P110);

        AddTriangle(MeshValue, P000, P100, P101);
        AddTriangle(MeshValue, P000, P101, P001);
        return MeshValue;
    }

    Mesh MakeSphereMesh(float Radius, int Rings, int Segments)
    {
        Mesh MeshValue;
        MeshGeometry& Geometry = MeshValue.EditGeometry();
        const int R = std::max(3, Rings);
        const int S = std::max(3, Segments);
        Geometry.Vertices.reserve(static_cast<size_t>(R + 1) * static_cast<size_t>(S + 1));
        Geometry.Normals.reserve(static_cast<size_t>(R + 1) * static_cast<size_t>(S + 1));
        Geometry.Uvs.reserve(static_cast<size_t>(R + 1) * static_cast<size_t>(S + 1));

        for (int I = 0; I <= R; ++I)
        {
            const float V = static_cast<float>(I) / static_cast<float>(R);
            const float Phi = static_cast<float>(M_PI) * (V - 0.5f);
            const float Cp = std::cos(Phi);
            const float Sp = std::sin(Phi);
            for (int J = 0; J <= S; ++J)
            {
                const float U = static_cast<float>(J) / static_cast<float>(S);
                const float Th = static_cast<float>(2.0 * M_PI) * U;
                const float Ct = std::cos(Th);
                const float St = std::sin(Th);
                Geometry.Vertices.push_back(Vec3{Radius * St * Cp, Radius * Sp, Radius * Ct * Cp});
                Geometry.Normals.push_back(Vec3Normalize(Vec3{St * Cp, Sp, Ct * Cp}));
                Geometry.Uvs.push_back(SDL_FPoint{U, 1.0f - V});
            }
        }

        const int Stride = S + 1;
        for (int I = 0; I < R; ++I)
        {
            for (int J = 0; J < S; ++J)
            {
                const uint32_t A = static_cast<uint32_t>(I * Stride + J);
                const uint32_t B = static_cast<uint32_t>(A + 1);
                const uint32_t C = static_cast<uint32_t>((I + 1) * Stride + J);
                const uint32_t D = static_cast<uint32_t>(C + 1);
                Geometry.Indices.push_back(A);
                Geometry.Indices.push_back(D);
                Geometry.Indices.push_back(C);
                Geometry.Indices.push_back(A);
                Geometry.Indices.push_back(B);
                Geometry.Indices.push_back(D);
            }
        }
        return MeshValue;
    }

    Mesh MakeCylinderMesh(float Radius, float Height, int Segments, bool TopCap, bool BottomCap)
    {
        Mesh MeshValue;
        const int S = std::max(3, Segments);
        const float Hy = Height * 0.5f;

        for (int I = 0; I < S; ++I)
        {
            const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
            const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
            const Vec3 B0{std::sin(A0) * Radius, -Hy, std::cos(A0) * Radius};
            const Vec3 B1{std::sin(A1) * Radius, -Hy, std::cos(A1) * Radius};
            const Vec3 T0{B0.X, Hy, B0.Z};
            const Vec3 T1{B1.X, Hy, B1.Z};
            AddTriangle(MeshValue, B0, T1, T0);
            AddTriangle(MeshValue, B0, B1, T1);
        }

        if (TopCap)
        {
            const Vec3 C{0.0f, Hy, 0.0f};
            for (int I = 0; I < S; ++I)
            {
                const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
                const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
                const Vec3 P0{std::sin(A0) * Radius, Hy, std::cos(A0) * Radius};
                const Vec3 P1{std::sin(A1) * Radius, Hy, std::cos(A1) * Radius};
                AddTriangle(MeshValue, C, P0, P1);
            }
        }

        if (BottomCap)
        {
            const Vec3 C{0.0f, -Hy, 0.0f};
            for (int I = 0; I < S; ++I)
            {
                const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
                const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
                const Vec3 P0{std::sin(A0) * Radius, -Hy, std::cos(A0) * Radius};
                const Vec3 P1{std::sin(A1) * Radius, -Hy, std::cos(A1) * Radius};
                AddTriangle(MeshValue, C, P1, P0);
            }
        }

        return MeshValue;
    }

    Mesh MakeConeMesh(float Radius, float Height, int Segments)
    {
        Mesh MeshValue;
        const int S = std::max(3, Segments);
        const float Hy = Height * 0.5f;
        const Vec3 Tip{0.0f, Hy, 0.0f};

        for (int I = 0; I < S; ++I)
        {
            const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
            const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
            const Vec3 P0{std::sin(A0) * Radius, -Hy, std::cos(A0) * Radius};
            const Vec3 P1{std::sin(A1) * Radius, -Hy, std::cos(A1) * Radius};
            AddTriangle(MeshValue, P0, P1, Tip);
        }
        return MeshValue;
    }
} // namespace dbc
