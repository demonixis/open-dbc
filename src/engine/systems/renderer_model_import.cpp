#include "engine_kernel.hpp"
#include "platform_io.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    [[nodiscard]] uint64_t CountSceneTriangles(const aiScene* Scene)
    {
        if (!Scene)
        {
            return 0;
        }
        uint64_t TriCount = 0;
        for (unsigned int Mi = 0; Mi < Scene->mNumMeshes; ++Mi)
        {
            const aiMesh* Mesh = Scene->mMeshes[Mi];
            if (!Mesh)
            {
                continue;
            }
            for (unsigned int Fi = 0; Fi < Mesh->mNumFaces; ++Fi)
            {
                if (Mesh->mFaces[Fi].mNumIndices == 3)
                {
                    ++TriCount;
                }
            }
        }
        return TriCount;
    }

    [[nodiscard]] bool IsXIdentifierCharacter(char Character)
    {
        const unsigned char Value = static_cast<unsigned char>(Character);
        return std::isalnum(Value) != 0 || Character == '_';
    }

    void SkipXWhitespace(const std::string& Source, std::size_t& Position)
    {
        while (Position < Source.size() && std::isspace(static_cast<unsigned char>(Source[Position])) != 0)
        {
            ++Position;
        }
    }

    [[nodiscard]] bool ConsumeXNumber(const std::string& Source, std::size_t& Position)
    {
        SkipXWhitespace(Source, Position);
        if (Position < Source.size() && (Source[Position] == '+' || Source[Position] == '-'))
        {
            ++Position;
        }

        bool HasDigit = false;
        while (Position < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Position])) != 0)
        {
            HasDigit = true;
            ++Position;
        }
        if (Position < Source.size() && Source[Position] == '.')
        {
            ++Position;
            while (Position < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Position])) != 0)
            {
                HasDigit = true;
                ++Position;
            }
        }
        if (!HasDigit)
        {
            return false;
        }

        if (Position < Source.size() && (Source[Position] == 'e' || Source[Position] == 'E'))
        {
            ++Position;
            if (Position < Source.size() && (Source[Position] == '+' || Source[Position] == '-'))
            {
                ++Position;
            }
            const std::size_t ExponentStart = Position;
            while (Position < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Position])) != 0)
            {
                ++Position;
            }
            if (Position == ExponentStart)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool RepairSingleTerminatedFrameMatrices(std::string& Source)
    {
        if (Source.size() < 16 || Source.compare(0, 4, "xof ") != 0 || Source.compare(8, 4, "txt ") != 0)
        {
            return false;
        }

        constexpr std::string_view MatrixToken = "FrameTransformMatrix";
        bool Repaired = false;
        std::size_t SearchPosition = 0;
        while ((SearchPosition = Source.find(MatrixToken, SearchPosition)) != std::string::npos)
        {
            const std::size_t TokenEnd = SearchPosition + MatrixToken.size();
            if ((SearchPosition > 0 && IsXIdentifierCharacter(Source[SearchPosition - 1])) ||
                (TokenEnd < Source.size() && IsXIdentifierCharacter(Source[TokenEnd])))
            {
                SearchPosition = TokenEnd;
                continue;
            }

            std::size_t Position = TokenEnd;
            SkipXWhitespace(Source, Position);
            if (Position >= Source.size() || Source[Position] != '{')
            {
                SearchPosition = TokenEnd;
                continue;
            }
            ++Position;

            bool IsNumericMatrix = true;
            std::size_t LastSeparator = std::string::npos;
            for (int Component = 0; Component < 16; ++Component)
            {
                if (!ConsumeXNumber(Source, Position))
                {
                    IsNumericMatrix = false;
                    break;
                }
                SkipXWhitespace(Source, Position);
                if (Position >= Source.size() || (Source[Position] != ',' && Source[Position] != ';'))
                {
                    IsNumericMatrix = false;
                    break;
                }
                LastSeparator = Position++;
            }
            if (!IsNumericMatrix || LastSeparator == std::string::npos || Source[LastSeparator] != ';')
            {
                SearchPosition = TokenEnd;
                continue;
            }

            SkipXWhitespace(Source, Position);
            if (Position < Source.size() && Source[Position] == ';')
            {
                SearchPosition = Position + 1;
                continue;
            }
            if (Position >= Source.size() || Source[Position] != '}')
            {
                SearchPosition = TokenEnd;
                continue;
            }

            Source.insert(LastSeparator + 1, 1, ';');
            Repaired = true;
            SearchPosition = Position + 2;
        }
        return Repaired;
    }

    [[nodiscard]] bool ReadAssimpSource(const std::string& Path, std::string& Source)
    {
        SdlAssimpIOSystem IoSystem;
        std::unique_ptr<Assimp::IOStream> Input(IoSystem.Open(Path.c_str(), "rb"));
        if (!Input)
        {
            return false;
        }
        const std::size_t FileSize = Input->FileSize();
        if (FileSize == 0)
        {
            return false;
        }
        Source.resize(FileSize);
        return Input->Read(Source.data(), 1, FileSize) == FileSize;
    }

    [[nodiscard]] AssimpImportResult ImportSceneWithFlags(const std::string& Path, unsigned int Flags)
    {
        AssimpImportResult Result{};
        Result.ImporterValue = std::make_unique<Assimp::Importer>();
        Result.ImporterValue->SetIOHandler(new SdlAssimpIOSystem());
        Result.Scene = Result.ImporterValue->ReadFile(Path, Flags);
        if (!Result.Scene && FileExtensionLower(Path) == ".x")
        {
            const char* Error = Result.ImporterValue->GetErrorString();
            std::string RepairedSource;
            if (Error && std::string_view(Error).find("Semicolon expected.") != std::string_view::npos &&
                ReadAssimpSource(Path, RepairedSource) && RepairSingleTerminatedFrameMatrices(RepairedSource))
            {
                std::unique_ptr<Assimp::Importer> Retry = std::make_unique<Assimp::Importer>();
                Retry->SetIOHandler(new SdlAssimpIOSystem());
                const aiScene* RetriedScene =
                    Retry->ReadFileFromMemory(RepairedSource.data(), RepairedSource.size(), Flags, "x");
                if (RetriedScene)
                {
                    Result.ImporterValue = std::move(Retry);
                    Result.Scene = RetriedScene;
                }
            }
        }
        if (Result.Scene && Result.Scene->HasMeshes())
        {
            Result.TriangleCount = CountSceneTriangles(Result.Scene);
        }
        return Result;
    }
} // namespace dbc
