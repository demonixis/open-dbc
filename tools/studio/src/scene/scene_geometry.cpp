#include "scene/scene_geometry.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#include <SDL3_image/SDL_image.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <utility>

#include "engine/systems/renderer_system.hpp"
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace dbc::studio
{
    namespace scene
    {
        namespace
        {
            constexpr float KPi = 3.14159265358979323846f;

            float Radians(float Degrees)
            {
                return Degrees * KPi / 180.0f;
            }

            // The engine's AddTriangle: every face of a box, cylinder or cone gets
            // these three UVs, and its normal comes from the winding.
            void AddTriangle(CpuMesh& Mesh, const glm::vec3& A, const glm::vec3& B, const glm::vec3& C,
                             glm::vec2 Ta = {0.0f, 0.0f}, glm::vec2 Tb = {1.0f, 0.0f}, glm::vec2 Tc = {1.0f, 1.0f})
            {
                const glm::vec3 Cross = glm::cross(B - A, C - A);
                const float Length = glm::length(Cross);
                const glm::vec3 Normal = Length > 0.0f ? Cross / Length : glm::vec3(0.0f, 1.0f, 0.0f);
                Mesh.Vertices.push_back({A, Normal, Ta});
                Mesh.Vertices.push_back({B, Normal, Tb});
                Mesh.Vertices.push_back({C, Normal, Tc});
            }

            // An engine mesh as a triangle list. Meshes without normals get the
            // engine's render-time face normals; `Flip` reverses the winding.
            // A texture inside the model: compressed file bytes when mHeight is 0, raw texels otherwise.
            bool DecodeEmbedded(const aiTexture& Texture, ModelImage& Out)
            {
                if (Texture.mHeight == 0)
                {
                    SDL_IOStream* Io = SDL_IOFromConstMem(Texture.pcData, Texture.mWidth);
                    SDL_Surface* Loaded = Io != nullptr ? IMG_Load_IO(Io, true) : nullptr;
                    SDL_Surface* Pixels =
                        Loaded != nullptr ? SDL_ConvertSurface(Loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
                    if (Loaded != nullptr)
                        SDL_DestroySurface(Loaded);
                    if (Pixels == nullptr)
                        return false;
                    Out.Width = Pixels->w;
                    Out.Height = Pixels->h;
                    Out.Rgba.resize(static_cast<std::size_t>(Out.Width) * Out.Height * 4);
                    for (int Y = 0; Y < Out.Height; ++Y)
                        std::memcpy(&Out.Rgba[static_cast<std::size_t>(Y) * Out.Width * 4],
                                    static_cast<const std::uint8_t*>(Pixels->pixels) +
                                        static_cast<std::size_t>(Y) * Pixels->pitch,
                                    static_cast<std::size_t>(Out.Width) * 4);
                    SDL_DestroySurface(Pixels);
                    return Out.Width > 0 && Out.Height > 0;
                }
                Out.Width = static_cast<int>(Texture.mWidth);
                Out.Height = static_cast<int>(Texture.mHeight);
                const std::size_t Count = static_cast<std::size_t>(Texture.mWidth) * Texture.mHeight;
                Out.Rgba.resize(Count * 4);
                for (std::size_t Index = 0; Index < Count; ++Index)
                {
                    const aiTexel& Texel = Texture.pcData[Index];
                    std::uint8_t* Pixel = &Out.Rgba[Index * 4];
                    Pixel[0] = Texel.r;
                    Pixel[1] = Texel.g;
                    Pixel[2] = Texel.b;
                    Pixel[3] = Texel.a;
                }
                return Count != 0;
            }

            // ObjectManager::LoadObject's material read: the diffuse colour and the
            // first texture found in its search order.
            void ReadMaterial(const aiScene& Scene, const aiMaterial& Material, MeshPart& Part,
                              std::vector<ModelImage>& Images, std::unordered_map<std::string, int>& Embedded)
            {
                aiColor4D Diffuse(1.0f, 1.0f, 1.0f, 1.0f);
                if (Material.Get(AI_MATKEY_COLOR_DIFFUSE, Diffuse) == aiReturn_SUCCESS)
                    Part.Color = glm::vec4(Diffuse.r, Diffuse.g, Diffuse.b, Diffuse.a);
                static constexpr aiTextureType KSearchOrder[] = {
                    aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR, aiTextureType_EMISSIVE, aiTextureType_NORMALS,
                    aiTextureType_HEIGHT,  aiTextureType_SPECULAR,   aiTextureType_AMBIENT,  aiTextureType_UNKNOWN,
                };
                for (aiTextureType Type : KSearchOrder)
                {
                    aiString Name;
                    if (Material.GetTextureCount(Type) == 0 || Material.GetTexture(Type, 0, &Name) != aiReturn_SUCCESS)
                        continue;
                    std::string Texture = Name.C_Str();
                    std::replace(Texture.begin(), Texture.end(), '\\', '/');
                    if (Texture.empty())
                        continue;
                    if (Texture.front() != '*')
                    {
                        // Resolved by the renderer, which knows the project's folder.
                        Part.Texture = std::move(Texture);
                        return;
                    }
                    if (const auto Found = Embedded.find(Texture); Found != Embedded.end())
                    {
                        Part.Image = Found->second;
                        return;
                    }
                    const aiTexture* Source = Scene.GetEmbeddedTexture(Texture.c_str());
                    ModelImage Image;
                    if (Source != nullptr && DecodeEmbedded(*Source, Image))
                    {
                        Part.Image = static_cast<int>(Images.size());
                        Images.push_back(std::move(Image));
                        Embedded.emplace(std::move(Texture), Part.Image);
                        return;
                    }
                }
            }

            CpuMesh FromEngine(const dbc::Mesh& Source, bool Flip = false)
            {
                CpuMesh Mesh;
                const auto Vec = [](const dbc::Vec3& Value) { return glm::vec3(Value.X, Value.Y, Value.Z); };
                for (std::size_t Index = 0; Index + 2 < Source.Geometry().Indices.size(); Index += 3)
                {
                    std::uint32_t Corner[3] = {Source.Geometry().Indices[Index], Source.Geometry().Indices[Index + 1],
                                               Source.Geometry().Indices[Index + 2]};
                    if (Flip)
                        std::swap(Corner[1], Corner[2]);
                    MeshVertex Triangle[3];
                    for (int Side = 0; Side < 3; ++Side)
                    {
                        Triangle[Side].Position = Vec(Source.Geometry().Vertices[Corner[Side]]);
                        if (Corner[Side] < Source.Geometry().Uvs.size())
                            Triangle[Side].Uv = {Source.Geometry().Uvs[Corner[Side]].x,
                                                 Source.Geometry().Uvs[Corner[Side]].y};
                    }
                    if (Source.Geometry().Normals.size() == Source.Geometry().Vertices.size())
                    {
                        for (int Side = 0; Side < 3; ++Side)
                            Triangle[Side].Normal = Vec(Source.Geometry().Normals[Corner[Side]]);
                        Mesh.Vertices.insert(Mesh.Vertices.end(), Triangle, Triangle + 3);
                    }
                    else
                    {
                        AddTriangle(Mesh, Triangle[0].Position, Triangle[1].Position, Triangle[2].Position,
                                    Triangle[0].Uv, Triangle[1].Uv, Triangle[2].Uv);
                    }
                }
                return Mesh;
            }

            // Grids are seen from above: their normals point up whatever the winding.
            void FaceUp(CpuMesh& Mesh)
            {
                for (MeshVertex& Vertex : Mesh.Vertices)
                    if (Vertex.Normal.y < 0.0f)
                        Vertex.Normal = -Vertex.Normal;
            }

            CpuMesh Matrix(const SceneNode& Node)
            {
                CpuMesh Mesh;
                const int Xs = std::max(1, Node.XSegments), Zs = std::max(1, Node.ZSegments);
                const float Sx = Node.Width / static_cast<float>(Xs), Sz = Node.Depth / static_cast<float>(Zs);
                const std::vector<float>& Heights = MatrixHeights(Node);
                const auto Point = [&](int X, int Z)
                {
                    const float Height = Heights.empty() ? 0.0f : Heights[static_cast<std::size_t>(Z) * (Xs + 1) + X];
                    return glm::vec3(X * Sx, Height, Z * Sz);
                };
                // Every tile shows the FILL MATRIX tile of the PREPARE MATRIX TEXTURE grid.
                const int Across = std::max(1, Node.TilesAcross), Down = std::max(1, Node.TilesDown);
                const int Tile = MatrixTile(Node) - 1;
                const float U0 = static_cast<float>(Tile % Across) / Across;
                const float U1 = static_cast<float>(Tile % Across + 1) / Across;
                const float VTop = static_cast<float>(Tile / Across) / Down;
                const float VBottom = static_cast<float>(Tile / Across + 1) / Down;
                for (int Z = 0; Z < Zs; ++Z)
                    for (int X = 0; X < Xs; ++X)
                    {
                        const glm::vec3 P00 = Point(X, Z), P10 = Point(X + 1, Z), P11 = Point(X + 1, Z + 1),
                                        P01 = Point(X, Z + 1);
                        AddTriangle(Mesh, P00, P10, P11, {U0, VBottom}, {U1, VBottom}, {U1, VTop});
                        AddTriangle(Mesh, P00, P11, P01, {U0, VBottom}, {U1, VTop}, {U0, VTop});
                    }
                FaceUp(Mesh);
                return Mesh;
            }

        } // namespace

        void ComputeBounds(CpuMesh& Mesh)
        {
            if (Mesh.Vertices.empty())
            {
                Mesh.Min = Mesh.Max = glm::vec3(0.0f);
                return;
            }
            Mesh.Min = glm::vec3(std::numeric_limits<float>::max());
            Mesh.Max = glm::vec3(std::numeric_limits<float>::lowest());
            for (const MeshVertex& Vertex : Mesh.Vertices)
            {
                Mesh.Min = glm::min(Mesh.Min, Vertex.Position);
                Mesh.Max = glm::max(Mesh.Max, Vertex.Position);
            }
        }

        CpuMesh BuildMesh(const SceneNode& Node, dbc::language::LanguageProfile Profile)
        {
            CpuMesh Mesh;
            switch (Node.Kind)
            {
                case NodeKind::Cube:
                    Mesh = FromEngine(RendererSystem::MakeBoxMesh(Node.Size, Node.Size, Node.Size));
                    break;
                case NodeKind::Box:
                    Mesh = FromEngine(RendererSystem::MakeBoxMesh(Node.Extent.x, Node.Extent.y, Node.Extent.z));
                    break;
                case NodeKind::Sphere:
                    // MAKE OBJECT SPHERE takes a diameter.
                    Mesh = FromEngine(RendererSystem::MakeSphereMesh(
                        Node.Size * 0.5f, Node.Detail ? Node.Detail->x : 12, Node.Detail ? Node.Detail->y : 16));
                    break;
                case NodeKind::Cylinder:
                    Mesh = FromEngine(RendererSystem::MakeCylinderMesh(Node.Size * 0.5f, Node.Size, 30, true, true));
                    break;
                case NodeKind::Cone:
                    Mesh = FromEngine(RendererSystem::MakeConeMesh(Node.Size * 0.5f, Node.Size, 16));
                    break;
                case NodeKind::Plain:
                    // A Classic plain is wound to face -Z.
                    Mesh = FromEngine(RendererSystem::MakePlainMesh(Node.Extent.x, Node.Extent.y),
                                      Profile == dbc::language::LanguageProfile::Dbc);
                    break;
                case NodeKind::Triangle:
                    AddTriangle(Mesh, Node.Corners[0], Node.Corners[1], Node.Corners[2]);
                    break;
                case NodeKind::Matrix:
                    Mesh = Matrix(Node);
                    break;
                default:
                    break;
            }
            ComputeBounds(Mesh);
            return Mesh;
        }

        bool ReadModelAnimation(const std::filesystem::path& Path, ModelAnimation& Out, std::string& Error)
        {
            // ObjectManager's import that keeps the skeleton and its animations.
            constexpr unsigned int Flags =
                aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                aiProcess_ImproveCacheLocality | aiProcess_ConvertToLeftHanded | aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
                aiProcess_FindInvalidData | aiProcess_FindDegenerates;
            const AssimpImportResult Imported = RendererSystem::ImportSceneWithFlags(dbc::PathToUtf8(Path), Flags);
            if (Imported.Scene == nullptr)
            {
                Error = Imported.ImporterValue && *Imported.ImporterValue->GetErrorString()
                            ? Imported.ImporterValue->GetErrorString()
                            : "unsupported or unreadable model";
                return false;
            }
            Out = {};
            const aiScene& Scene = *Imported.Scene;
            if (Scene.mNumAnimations == 0 || Scene.mAnimations[0] == nullptr)
                return true;
            // The frame count ObjectManager gives the clip: its duration at 25 frames a second.
            const aiAnimation& Animation = *Scene.mAnimations[0];
            const double Ticks = std::isfinite(Animation.mDuration) ? std::max(0.0, Animation.mDuration) : 0.0;
            const double PerSecond = std::isfinite(Animation.mTicksPerSecond) && Animation.mTicksPerSecond > 0.0
                                         ? Animation.mTicksPerSecond
                                         : 25.0;
            Out.Name = Animation.mName.C_Str();
            Out.LastFrame = static_cast<int>(std::lround(
                std::clamp(Ticks * 25.0 / PerSecond, 0.0, static_cast<double>(std::numeric_limits<int>::max()))));
            Out.Count = Scene.mNumAnimations;
            return true;
        }

        bool LoadModelMesh(const std::filesystem::path& Path, CpuMesh& Out, std::string& Error)
        {
            // ObjectManager's pre-transformed import flags: one mesh list, hierarchy applied.
            constexpr unsigned int Flags =
                aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                aiProcess_ImproveCacheLocality | aiProcess_ConvertToLeftHanded | aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
                aiProcess_FindInvalidData | aiProcess_FindDegenerates | aiProcess_PreTransformVertices |
                aiProcess_OptimizeMeshes;
            const AssimpImportResult Imported = RendererSystem::ImportSceneWithFlags(dbc::PathToUtf8(Path), Flags);
            if (Imported.Scene == nullptr)
            {
                Error = Imported.ImporterValue && *Imported.ImporterValue->GetErrorString()
                            ? Imported.ImporterValue->GetErrorString()
                            : "unsupported or unreadable model";
                return false;
            }
            Out = {};
            const aiScene& Scene = *Imported.Scene;
            std::unordered_map<std::string, int> Embedded;
            for (unsigned int MeshIndex = 0; MeshIndex < Scene.mNumMeshes; ++MeshIndex)
            {
                const aiMesh& Mesh = *Scene.mMeshes[MeshIndex];
                MeshPart Part;
                Part.First = Out.Vertices.size();
                if (Mesh.mMaterialIndex < Scene.mNumMaterials && Scene.mMaterials[Mesh.mMaterialIndex] != nullptr)
                    ReadMaterial(Scene, *Scene.mMaterials[Mesh.mMaterialIndex], Part, Out.Images, Embedded);
                for (unsigned int FaceIndex = 0; FaceIndex < Mesh.mNumFaces; ++FaceIndex)
                {
                    const aiFace& Face = Mesh.mFaces[FaceIndex];
                    if (Face.mNumIndices != 3)
                        continue;
                    for (unsigned int Corner = 0; Corner < 3; ++Corner)
                    {
                        const unsigned int Index = Face.mIndices[Corner];
                        MeshVertex Vertex;
                        Vertex.Position = {Mesh.mVertices[Index].x, Mesh.mVertices[Index].y, Mesh.mVertices[Index].z};
                        if (Mesh.HasNormals())
                            Vertex.Normal = {Mesh.mNormals[Index].x, Mesh.mNormals[Index].y, Mesh.mNormals[Index].z};
                        if (Mesh.HasTextureCoords(0))
                            Vertex.Uv = {Mesh.mTextureCoords[0][Index].x, Mesh.mTextureCoords[0][Index].y};
                        Out.Vertices.push_back(Vertex);
                    }
                }
                Part.Count = Out.Vertices.size() - Part.First;
                if (Part.Count != 0)
                    Out.Parts.push_back(std::move(Part));
            }
            ComputeBounds(Out);
            if (Out.Vertices.empty())
            {
                Error = "The model has no triangles";
                return false;
            }
            return true;
        }

        bool LoadTerrainMesh(const std::filesystem::path& Heightmap, CpuMesh& Out, std::string& Error)
        {
            SDL_Surface* Loaded = IMG_Load(dbc::PathToUtf8(Heightmap).c_str());
            if (Loaded == nullptr)
            {
                Error = SDL_GetError();
                return false;
            }
            SDL_Surface* Pixels = SDL_ConvertSurface(Loaded, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(Loaded);
            if (Pixels == nullptr)
            {
                Error = SDL_GetError();
                return false;
            }
            const int Size = Pixels->w;
            if (Size < 2 || Pixels->h != Size)
            {
                SDL_DestroySurface(Pixels);
                Error = "A terrain heightmap must be square and at least 2x2";
                return false;
            }
            const auto Height = [&](int X, int Z)
            {
                const auto* Row =
                    static_cast<const Uint8*>(Pixels->pixels) + static_cast<std::size_t>(Z) * Pixels->pitch;
                const Uint8* Pixel = Row + static_cast<std::size_t>(X) * 4;
                return (Pixel[0] + Pixel[1] + Pixel[2]) / (3.0f * 255.0f) * KTerrainHeightScale;
            };
            // Width is the pixel count over Size - 1 segments; row 0 lies at the position's Z.
            const int Segments = Size - 1;
            const float Step = static_cast<float>(Size) / static_cast<float>(Segments);
            const auto Point = [&](int X, int Z) { return glm::vec3(X * Step, Height(X, Z), -Z * Step); };
            const auto Uv = [&](int X, int Z)
            { return glm::vec2(static_cast<float>(X) / Segments, static_cast<float>(Z) / Segments); };
            Out = {};
            for (int Z = 0; Z < Segments; ++Z)
                for (int X = 0; X < Segments; ++X)
                {
                    AddTriangle(Out, Point(X, Z), Point(X + 1, Z + 1), Point(X + 1, Z), Uv(X, Z), Uv(X + 1, Z + 1),
                                Uv(X + 1, Z));
                    AddTriangle(Out, Point(X, Z), Point(X, Z + 1), Point(X + 1, Z + 1), Uv(X, Z), Uv(X, Z + 1),
                                Uv(X + 1, Z + 1));
                }
            SDL_DestroySurface(Pixels);
            FaceUp(Out);
            ComputeBounds(Out);
            return true;
        }

        glm::mat3 RotationMatrix(const glm::vec3& Degrees)
        {
            const glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), Radians(Degrees.x), glm::vec3(1, 0, 0));
            const glm::mat4 Ry = glm::rotate(glm::mat4(1.0f), Radians(Degrees.y), glm::vec3(0, 1, 0));
            const glm::mat4 Rz = glm::rotate(glm::mat4(1.0f), Radians(Degrees.z), glm::vec3(0, 0, 1));
            return glm::mat3(Rz * Ry * Rx);
        }

        glm::vec3 EulerDegrees(const glm::mat3& R)
        {
            // R = Rz * Ry * Rx; glm indexes [column][row].
            const float SinY = std::clamp(-R[0][2], -1.0f, 1.0f);
            float X = 0.0f, Y = std::asin(SinY), Z = 0.0f;
            if (std::abs(SinY) < 0.99999f)
            {
                X = std::atan2(R[1][2], R[2][2]);
                Z = std::atan2(R[0][1], R[0][0]);
            }
            else
            {
                // Gimbal lock: only X + Z or X - Z is defined, so Z takes 0.
                X = std::atan2(-R[2][1], R[1][1]);
            }
            const auto Degrees = [](float Value)
            {
                const float Result = Value * 180.0f / KPi;
                return std::abs(Result) < 1e-4f ? 0.0f : Result;
            };
            return {Degrees(X), Degrees(Y), Degrees(Z)};
        }

        glm::vec3 LightDirection(const glm::vec3& Degrees)
        {
            const float Yaw = Radians(Degrees.y), Pitch = Radians(Degrees.x);
            return glm::normalize(
                glm::vec3(std::sin(Yaw) * std::cos(Pitch), -std::sin(Pitch), std::cos(Yaw) * std::cos(Pitch)));
        }

        glm::mat4 NodeWorld(const SceneNode& Node)
        {
            glm::mat4 World = glm::translate(glm::mat4(1.0f), Node.Position);
            if (IdSpaceOf(Node.Kind) != IdSpace::Object)
                return World;
            World *= glm::mat4(RotationMatrix(Node.Rotation));
            return glm::scale(World, Node.Scale / 100.0f);
        }

        glm::vec3 SnapChangedAxes(const glm::vec3& Value, const glm::vec3& Start, float Step)
        {
            if (Step <= 0.0f)
                return Value;
            glm::vec3 Snapped = Value;
            for (int Axis = 0; Axis < 3; ++Axis)
                // Float noise on an axis the gizmo did not drag must not snap it.
                if (std::abs(Value[Axis] - Start[Axis]) > 1e-4f * std::max(1.0f, std::abs(Start[Axis])))
                    Snapped[Axis] = std::round(Value[Axis] / Step) * Step;
            return Snapped;
        }

        glm::vec3 ProportionalScale(const glm::vec3& Start, const glm::vec3& Edited)
        {
            int Axis = 0;
            for (int Index = 1; Index < 3; ++Index)
                if (std::abs(Edited[Index] - Start[Index]) > std::abs(Edited[Axis] - Start[Axis]))
                    Axis = Index;
            if (std::abs(Start[Axis]) < 1e-6f)
                return Edited;
            return Start * (Edited[Axis] / Start[Axis]);
        }

        void ApplyGroupChange(const GroupChange& Change, const SceneNode& Start, SceneNode& Node)
        {
            const glm::vec3 Local = glm::transpose(Change.Axes) * (Start.Position - Change.Pivot);
            Node.Position = Change.Pivot + Change.Move + Change.Turn * (Change.Axes * (Local * Change.Factor));
            const bool Object = IdSpaceOf(Node.Kind) == IdSpace::Object;
            // Angles are only rewritten by a turn, so moving keeps them exact.
            if ((Object || Node.Kind == NodeKind::Light) && Change.Turn != glm::mat3(1.0f))
                Node.Rotation = EulerDegrees(Change.Turn * RotationMatrix(Start.Rotation));
            else
                Node.Rotation = Start.Rotation;
            // Snapping down to 0 would collapse the object for good.
            Node.Scale = Object ? glm::max(Start.Scale * Change.Factor, glm::vec3(0.01f)) : Start.Scale;
        }

        glm::vec3 ShadowSource(const SceneDocument& Document)
        {
            const int Mode = Document.Env.ShadowMode.value_or(0);
            if (Mode == -1)
                return Document.Env.ShadowPosition;
            const SceneNode* Light = Mode > 0 ? Document.Find(IdSpace::Light, Mode) : nullptr;
            return Light != nullptr ? Light->Position : glm::vec3(0.0f);
        }

        void AppendShadowVolume(const CpuMesh& Mesh, const glm::mat4& World, const glm::vec3& Light, float Range,
                                std::vector<glm::vec3>& Out)
        {
            if (Range <= 0.0f)
                return;
            for (std::size_t Index = 0; Index + 2 < Mesh.Vertices.size(); Index += 3)
            {
                glm::vec3 Base[3];
                for (int Corner = 0; Corner < 3; ++Corner)
                    Base[Corner] = glm::vec3(World * glm::vec4(Mesh.Vertices[Index + Corner].Position, 1.0f));
                const glm::vec3 Face = glm::cross(Base[1] - Base[0], Base[2] - Base[0]);
                const glm::vec3 Center = (Base[0] + Base[1] + Base[2]) / 3.0f;
                if (glm::dot(Face, Light - Center) <= 0.000001f)
                    continue;
                const float FaceLength = glm::length(Face);
                const glm::vec3 Fallback = FaceLength > 0.0f ? -Face / FaceLength : glm::vec3(0.0f, -1.0f, 0.0f);
                glm::vec3 Far[3];
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const glm::vec3 Away = Base[Corner] - Light;
                    const float Length = glm::length(Away);
                    const glm::vec3 Direction = Length * Length > 0.000001f ? Away / Length : Fallback;
                    Base[Corner] += Direction * 0.1f;
                    Far[Corner] = Base[Corner] + Direction * Range;
                }
                const auto Append = [&](const glm::vec3& A, const glm::vec3& B, const glm::vec3& C)
                { Out.insert(Out.end(), {A, B, C}); };
                Append(Base[0], Base[1], Base[2]);
                Append(Far[2], Far[1], Far[0]);
                for (int Edge = 0; Edge < 3; ++Edge)
                {
                    const int Next = (Edge + 1) % 3;
                    Append(Base[Edge], Far[Next], Base[Next]);
                    Append(Base[Edge], Far[Edge], Far[Next]);
                }
            }
        }

        std::optional<float> Intersect(const Ray& Value, const CpuMesh& Mesh, const glm::mat4& World)
        {
            std::optional<float> Nearest;
            for (std::size_t Index = 0; Index + 2 < Mesh.Vertices.size(); Index += 3)
            {
                const glm::vec3 A = glm::vec3(World * glm::vec4(Mesh.Vertices[Index].Position, 1.0f));
                const glm::vec3 B = glm::vec3(World * glm::vec4(Mesh.Vertices[Index + 1].Position, 1.0f));
                const glm::vec3 C = glm::vec3(World * glm::vec4(Mesh.Vertices[Index + 2].Position, 1.0f));
                // Moller-Trumbore.
                const glm::vec3 E1 = B - A, E2 = C - A;
                const glm::vec3 P = glm::cross(Value.Direction, E2);
                const float Determinant = glm::dot(E1, P);
                if (std::abs(Determinant) < 1e-8f)
                    continue;
                const float Inverse = 1.0f / Determinant;
                const glm::vec3 T = Value.Origin - A;
                const float U = glm::dot(T, P) * Inverse;
                if (U < 0.0f || U > 1.0f)
                    continue;
                const glm::vec3 Q = glm::cross(T, E1);
                const float V = glm::dot(Value.Direction, Q) * Inverse;
                if (V < 0.0f || U + V > 1.0f)
                    continue;
                const float Distance = glm::dot(E2, Q) * Inverse;
                if (Distance > 0.0f && (!Nearest || Distance < *Nearest))
                    Nearest = Distance;
            }
            return Nearest;
        }
    } // namespace scene

} // namespace dbc::studio
