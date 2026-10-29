#include "program_image.hpp"
#include "path_utf8.hpp"

#include <bit>
#include <cctype>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace dbc::interpreter
{
    namespace
    {
        // The first byte is not ASCII and the CR/LF/EOF bytes reveal text-mode transfers, as in PNG.
        constexpr std::uint8_t KMagic[8] = {0x89, 'O', 'B', 'C', '\r', '\n', 0x1a, '\n'};
        constexpr std::uint32_t KFormatVersion = 1;
        constexpr std::uint32_t KNoPath = std::numeric_limits<std::uint32_t>::max();

        std::string PathText(const std::filesystem::path& Path)
        {
            return dbc::GenericPathToUtf8(Path);
        }

        std::filesystem::path TextPath(const std::string& Text)
        {
            return dbc::PathFromUtf8(Text);
        }

        class Writer
        {
          public:
            explicit Writer(const std::filesystem::path& SourceRoot) : SourceRoot_(SourceRoot.lexically_normal()) {}

            void U8(std::uint8_t Value)
            {
                Bytes_.push_back(Value);
            }

            void U32(std::uint32_t Value)
            {
                for (int Shift = 0; Shift < 32; Shift += 8)
                    Bytes_.push_back(static_cast<std::uint8_t>(Value >> Shift));
            }

            void U64(std::uint64_t Value)
            {
                for (int Shift = 0; Shift < 64; Shift += 8)
                    Bytes_.push_back(static_cast<std::uint8_t>(Value >> Shift));
            }

            void I32(std::int32_t Value)
            {
                U32(static_cast<std::uint32_t>(Value));
            }

            void String(std::string_view Value)
            {
                U32(static_cast<std::uint32_t>(Value.size()));
                Bytes_.insert(Bytes_.end(), Value.begin(), Value.end());
            }

            void Type(const language::Type& Value)
            {
                U8(static_cast<std::uint8_t>(Value.Kind));
                String(Value.Name);
            }

            void Slots(const std::vector<vm::Slot>& Values)
            {
                U32(static_cast<std::uint32_t>(Values.size()));
                for (const vm::Slot& Value : Values)
                {
                    String(Value.Name);
                    Type(Value.ValueType);
                    U64(Value.ArrayRank);
                }
            }

            // Paths go to a table written ahead of the module, so each span costs one index.
            std::uint32_t PathIndex(const std::filesystem::path& Path)
            {
                if (Path.empty())
                    return KNoPath;
                const std::filesystem::path Normal = Path.lexically_normal();
                std::filesystem::path Relative = Normal.lexically_relative(SourceRoot_);
                if (Relative.empty() || *Relative.begin() == "..")
                    Relative = Normal.filename();
                const std::string Text = PathText(Relative);
                const auto [It, Inserted] = PathIndices_.try_emplace(Text, static_cast<std::uint32_t>(Paths_.size()));
                if (Inserted)
                    Paths_.push_back(Text);
                return It->second;
            }

            void Position(const language::SourcePosition& Value)
            {
                U64(Value.Offset);
                U32(Value.Line);
                U32(Value.Column);
            }

            std::vector<std::uint8_t>& Bytes()
            {
                return Bytes_;
            }

            const std::vector<std::string>& Paths() const
            {
                return Paths_;
            }

          private:
            std::filesystem::path SourceRoot_;
            std::vector<std::uint8_t> Bytes_{};
            std::vector<std::string> Paths_{};
            std::unordered_map<std::string, std::uint32_t> PathIndices_{};
        };

        class Reader
        {
          public:
            explicit Reader(std::span<const std::uint8_t> Bytes) : Bytes_(Bytes) {}

            bool U8(std::uint8_t& Value)
            {
                if (Remaining() < 1)
                    return false;
                Value = Bytes_[Offset_++];
                return true;
            }

            bool U32(std::uint32_t& Value)
            {
                if (Remaining() < 4)
                    return false;
                Value = 0;
                for (int Shift = 0; Shift < 32; Shift += 8)
                    Value |= static_cast<std::uint32_t>(Bytes_[Offset_++]) << Shift;
                return true;
            }

            bool U64(std::uint64_t& Value)
            {
                if (Remaining() < 8)
                    return false;
                Value = 0;
                for (int Shift = 0; Shift < 64; Shift += 8)
                    Value |= static_cast<std::uint64_t>(Bytes_[Offset_++]) << Shift;
                return true;
            }

            bool I32(std::int32_t& Value)
            {
                std::uint32_t Raw = 0;
                if (!U32(Raw))
                    return false;
                Value = static_cast<std::int32_t>(Raw);
                return true;
            }

            bool Size(std::size_t& Value)
            {
                std::uint64_t Raw = 0;
                if (!U64(Raw) || Raw > std::numeric_limits<std::size_t>::max())
                    return false;
                Value = static_cast<std::size_t>(Raw);
                return true;
            }

            // Every element takes at least one byte, so a count larger than the rest of the file is corrupt.
            bool Count(std::uint32_t& Value)
            {
                return U32(Value) && Value <= Remaining();
            }

            bool String(std::string& Value)
            {
                std::uint32_t Length = 0;
                if (!U32(Length) || Length > Remaining())
                    return false;
                Value.assign(reinterpret_cast<const char*>(Bytes_.data() + Offset_), Length);
                Offset_ += Length;
                return true;
            }

            bool Type(language::Type& Value)
            {
                std::uint8_t Kind = 0;
                if (!U8(Kind) || Kind > static_cast<std::uint8_t>(language::TypeKind::UserDefined))
                    return false;
                Value.Kind = static_cast<language::TypeKind>(Kind);
                return String(Value.Name);
            }

            bool Slots(std::vector<vm::Slot>& Values)
            {
                std::uint32_t Count = 0;
                if (!this->Count(Count))
                    return false;
                Values.resize(Count);
                for (vm::Slot& Value : Values)
                    if (!String(Value.Name) || !Type(Value.ValueType) || !Size(Value.ArrayRank))
                        return false;
                return true;
            }

            bool Path(const std::vector<std::filesystem::path>& Paths, std::filesystem::path& Value)
            {
                std::uint32_t Index = 0;
                if (!U32(Index))
                    return false;
                if (Index == KNoPath)
                {
                    Value.clear();
                    return true;
                }
                if (Index >= Paths.size())
                    return false;
                Value = Paths[Index];
                return true;
            }

            bool Position(language::SourcePosition& Value)
            {
                return Size(Value.Offset) && U32(Value.Line) && U32(Value.Column);
            }

            std::size_t Remaining() const
            {
                return Bytes_.size() - Offset_;
            }

          private:
            std::span<const std::uint8_t> Bytes_;
            std::size_t Offset_ = 0;
        };

        void WriteInstruction(Writer& Out, const vm::Instruction& Value)
        {
            Out.U8(static_cast<std::uint8_t>(Value.Op));
            Out.U32(Out.PathIndex(Value.Span.LogicalPath));
            Out.Position(Value.Span.Begin);
            Out.Position(Value.Span.End);
            Out.Type(Value.ResultType);
            Out.U32(Value.A);
            Out.U32(Value.B);
            Out.U32(Value.C);
            Out.U32(Value.D);
            Out.U32(Value.E);
            Out.U64(static_cast<std::uint64_t>(Value.Integer));
            Out.U64(std::bit_cast<std::uint64_t>(Value.Real));
        }

        bool ReadInstruction(Reader& In, const std::vector<std::filesystem::path>& Paths, vm::Instruction& Value)
        {
            std::uint8_t Op = 0;
            std::uint64_t Integer = 0;
            std::uint64_t Real = 0;
            if (!In.U8(Op) || Op > static_cast<std::uint8_t>(vm::OpCode::Halt) ||
                !In.Path(Paths, Value.Span.LogicalPath) || !In.Position(Value.Span.Begin) ||
                !In.Position(Value.Span.End) || !In.Type(Value.ResultType) || !In.U32(Value.A) || !In.U32(Value.B) ||
                !In.U32(Value.C) || !In.U32(Value.D) || !In.U32(Value.E) || !In.U64(Integer) || !In.U64(Real))
                return false;
            Value.Op = static_cast<vm::OpCode>(Op);
            Value.Integer = static_cast<std::int64_t>(Integer);
            Value.Real = std::bit_cast<double>(Real);
            return true;
        }

        bool ReadModule(Reader& In, const std::vector<std::filesystem::path>& Paths, vm::Module& Value)
        {
            std::uint32_t Count = 0;
            if (!In.Path(Paths, Value.LogicalPath) || !In.Slots(Value.Globals) || !In.Count(Count))
                return false;
            Value.Types.resize(Count);
            for (vm::TypeLayout& Layout : Value.Types)
            {
                std::uint32_t Fields = 0;
                if (!In.String(Layout.Name) || !In.Count(Fields))
                    return false;
                Layout.Fields.resize(Fields);
                for (vm::TypeField& Field : Layout.Fields)
                    if (!In.String(Field.Name) || !In.Type(Field.ValueType))
                        return false;
            }

            if (!In.Count(Count))
                return false;
            Value.Data.resize(Count);
            for (vm::DataValue& Data : Value.Data)
            {
                std::uint64_t Integer = 0;
                std::uint64_t Real = 0;
                if (!In.Type(Data.ValueType) || !In.U64(Integer) || !In.U64(Real) || !In.U32(Data.StringIndex))
                    return false;
                Data.Integer = static_cast<std::int64_t>(Integer);
                Data.Real = std::bit_cast<double>(Real);
            }

            if (!In.Count(Count))
                return false;
            Value.DataLabels.resize(Count);
            for (auto& [Name, Index] : Value.DataLabels)
                if (!In.String(Name) || !In.Size(Index))
                    return false;

            if (!In.Count(Count))
                return false;
            Value.Functions.resize(Count);
            for (vm::Function& Function : Value.Functions)
            {
                std::uint32_t Instructions = 0;
                if (!In.String(Function.Name) || !In.Type(Function.ReturnType) || !In.Size(Function.ParameterCount) ||
                    !In.Slots(Function.Locals) || !In.Count(Instructions))
                    return false;
                Function.Code.resize(Instructions);
                for (vm::Instruction& Instruction : Function.Code)
                    if (!ReadInstruction(In, Paths, Instruction))
                        return false;
            }

            if (!In.Count(Count))
                return false;
            Value.Strings.resize(Count);
            for (std::string& Text : Value.Strings)
                if (!In.String(Text))
                    return false;
            return In.Size(Value.EntryFunction);
        }
    } // namespace

    std::string_view RuntimeFingerprint()
    {
        return OPENDBC_RUNTIME_FINGERPRINT;
    }

    bool IsProgramImagePath(const std::filesystem::path& Path)
    {
        std::string Extension = dbc::PathToUtf8(Path.extension());
        for (char& Character : Extension)
            Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
        return Extension == ProgramImageExtension;
    }

    std::vector<std::uint8_t> WriteProgramImage(const vm::Module& Bytecode, const ProjectStartup& Startup,
                                                const std::filesystem::path& SourceRoot, std::string_view Fingerprint)
    {
        // The module goes to its own buffer first because it fills the path table written before it.
        Writer Module(SourceRoot);
        Module.U32(Module.PathIndex(Bytecode.LogicalPath));
        Module.Slots(Bytecode.Globals);
        Module.U32(static_cast<std::uint32_t>(Bytecode.Types.size()));
        for (const vm::TypeLayout& Layout : Bytecode.Types)
        {
            Module.String(Layout.Name);
            Module.U32(static_cast<std::uint32_t>(Layout.Fields.size()));
            for (const vm::TypeField& Field : Layout.Fields)
            {
                Module.String(Field.Name);
                Module.Type(Field.ValueType);
            }
        }
        Module.U32(static_cast<std::uint32_t>(Bytecode.Data.size()));
        for (const vm::DataValue& Data : Bytecode.Data)
        {
            Module.Type(Data.ValueType);
            Module.U64(static_cast<std::uint64_t>(Data.Integer));
            Module.U64(std::bit_cast<std::uint64_t>(Data.Real));
            Module.U32(Data.StringIndex);
        }
        Module.U32(static_cast<std::uint32_t>(Bytecode.DataLabels.size()));
        for (const auto& [Name, Index] : Bytecode.DataLabels)
        {
            Module.String(Name);
            Module.U64(Index);
        }
        Module.U32(static_cast<std::uint32_t>(Bytecode.Functions.size()));
        for (const vm::Function& Function : Bytecode.Functions)
        {
            Module.String(Function.Name);
            Module.Type(Function.ReturnType);
            Module.U64(Function.ParameterCount);
            Module.Slots(Function.Locals);
            Module.U32(static_cast<std::uint32_t>(Function.Code.size()));
            for (const vm::Instruction& Instruction : Function.Code)
                WriteInstruction(Module, Instruction);
        }
        Module.U32(static_cast<std::uint32_t>(Bytecode.Strings.size()));
        for (const std::string& Text : Bytecode.Strings)
            Module.String(Text);
        Module.U64(Bytecode.EntryFunction);

        Writer Out(SourceRoot);
        Out.Bytes().assign(std::begin(KMagic), std::end(KMagic));
        Out.U32(KFormatVersion);
        Out.String(Fingerprint);
        Out.U8(static_cast<std::uint8_t>(Bytecode.Profile));
        Out.String(Startup.Title);
        Out.U8(static_cast<std::uint8_t>(Startup.WindowMode));
        Out.I32(Startup.Width);
        Out.I32(Startup.Height);
        Out.I32(Startup.Depth);
        Out.String(Startup.CommandLine);
        Out.U32(static_cast<std::uint32_t>(Module.Paths().size()));
        for (const std::string& Path : Module.Paths())
            Out.String(Path);
        Out.Bytes().insert(Out.Bytes().end(), Module.Bytes().begin(), Module.Bytes().end());
        return std::move(Out.Bytes());
    }

    ProgramImageReadResult ReadProgramImage(std::span<const std::uint8_t> Bytes, std::string_view Fingerprint)
    {
        ProgramImageReadResult Result;
        if (Bytes.size() < sizeof(KMagic) || std::memcmp(Bytes.data(), KMagic, sizeof(KMagic)) != 0)
        {
            Result.Error = "not an OpenDBC program image";
            return Result;
        }
        Reader In(Bytes.subspan(sizeof(KMagic)));
        std::uint32_t Version = 0;
        if (!In.U32(Version) || Version != KFormatVersion)
        {
            Result.Error = "unsupported program image format " + std::to_string(Version);
            return Result;
        }
        std::string ImageFingerprint;
        if (!In.String(ImageFingerprint) || ImageFingerprint != Fingerprint)
        {
            Result.Error = "program image was built by another OpenDBC runtime; rebuild it with this version";
            return Result;
        }

        ProgramImage Image;
        Image.Bytecode = std::make_shared<vm::Module>();
        std::uint8_t Profile = 0;
        std::uint8_t WindowMode = 0;
        std::uint32_t PathCount = 0;
        std::vector<std::filesystem::path> Paths;
        bool Valid = In.U8(Profile) && Profile <= static_cast<std::uint8_t>(language::LanguageProfile::DbPro) &&
                     In.String(Image.Startup.Title) && In.U8(WindowMode) &&
                     WindowMode <= static_cast<std::uint8_t>(ProjectWindowMode::Hidden) &&
                     In.I32(Image.Startup.Width) && In.I32(Image.Startup.Height) && In.I32(Image.Startup.Depth) &&
                     In.String(Image.Startup.CommandLine) && In.Count(PathCount);
        for (std::uint32_t Index = 0; Valid && Index < PathCount; ++Index)
        {
            std::string Text;
            Valid = In.String(Text);
            const std::filesystem::path Path = TextPath(Text);
            Valid = Valid && !Path.empty() && Path.is_relative() && *Path.begin() != "..";
            Paths.push_back(Path);
        }
        Valid = Valid && ReadModule(In, Paths, *Image.Bytecode) && In.Remaining() == 0;
        if (!Valid)
        {
            Result.Error = "program image is truncated or corrupt";
            return Result;
        }
        Image.Bytecode->Profile = static_cast<language::LanguageProfile>(Profile);
        Image.Startup.WindowMode = static_cast<ProjectWindowMode>(WindowMode);
        Result.Image = std::move(Image);
        return Result;
    }
} // namespace dbc::interpreter
