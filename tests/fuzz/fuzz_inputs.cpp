// Coverage-guided fuzzing of what OpenDBC reads from files, under the sanitizers: mutate seeds, keep
// each input that reaches new code and leave the crash to AddressSanitizer. Apple's clang has the
// instrumentation but not libFuzzer, so the loop lives here.
//
//   fuzz_inputs <target> <seconds> <work directory> [seed file or directory]...
//
// <work directory>/current.* holds the input being run, so it outlives a crash. With 0 seconds the
// seeds are only replayed, which reproduces a crash from its saved input.
// scripts/fuzz_inputs.py restarts the run after each crash and files the distinct ones.

#include "engine/systems/audio_system.hpp"
#include "engine/systems/camera_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/memblock_system.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/renderer_system.hpp"
#include "interpreter/program_image.hpp"
#include "language/frontend.hpp"
#include "vm/compiler.hpp"
#include "vm/verifier.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

namespace
{
    // Clang's edge counters: the instrumented code numbers its edges through the guards below.
    constexpr std::size_t MaximumEdges = std::size_t{1} << 22;
    std::uint8_t EdgeHits[MaximumEdges];
    std::uint8_t SeenEdges[MaximumEdges];
    std::uint32_t EdgeCount = 0;
} // namespace

extern "C" void __sanitizer_cov_trace_pc_guard_init(std::uint32_t* Start, std::uint32_t* Stop)
{
    if (Start == Stop || *Start != 0)
        return;
    for (std::uint32_t* Guard = Start; Guard < Stop; ++Guard)
        *Guard = ++EdgeCount % MaximumEdges;
}

extern "C" void __sanitizer_cov_trace_pc_guard(std::uint32_t* Guard)
{
    ++EdgeHits[*Guard];
}

namespace
{
    constexpr std::size_t MaximumSeedSize = 256 * 1024;
    constexpr std::size_t MaximumInputSize = 1024 * 1024;
    constexpr unsigned WatchdogSeconds = 30;

    dbc::EngineKernel* Kernel = nullptr;
    std::filesystem::path WorkDirectory;
    std::string CurrentPath;
    std::string OutputPath;
    std::uint64_t RenderCount = 0;

    // Whether the last run reached an edge, or passed one as many times over, that no earlier run did.
    bool TakeNewEdges()
    {
        bool Found = false;
        const std::size_t Count = std::min<std::size_t>(std::size_t{EdgeCount} + 1, MaximumEdges);
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            if (EdgeHits[Index] == 0)
                continue;
            const auto Bucket = static_cast<std::uint8_t>(1u << (std::bit_width(EdgeHits[Index]) - 1));
            Found = Found || (SeenEdges[Index] & Bucket) == 0;
            SeenEdges[Index] |= Bucket;
        }
        std::memset(EdgeHits, 0, Count);
        return Found;
    }

    class Mutator
    {
      public:
        Mutator(std::uint64_t Seed, bool Text) : Random_(Seed), Text_(Text) {}

        void Mutate(Bytes& Data, const Bytes& Other)
        {
            for (std::size_t Count = 1 + Below(4); Count > 0; --Count)
                Apply(Data, Other);
            if (Data.size() > MaximumInputSize)
                Data.resize(MaximumInputSize);
        }

        std::size_t Below(std::size_t Limit)
        {
            return Limit == 0 ? 0 : Random_() % Limit;
        }

      private:
        // Half the mutations land in the first bytes, where formats keep their counts and offsets.
        std::size_t Position(std::size_t Size)
        {
            return Below(2) == 0 ? Below(std::min<std::size_t>(Size, 256)) : Below(Size);
        }

        void Write32(Bytes& Data, std::uint32_t Value)
        {
            if (Data.size() < 4)
                return;
            std::size_t Offset = std::min(Position(Data.size()), Data.size() - 4);
            if (Below(2) == 0)
                Offset &= ~std::size_t{3};
            std::memcpy(Data.data() + Offset, &Value, sizeof(Value));
        }

        void Apply(Bytes& Data, const Bytes& Other)
        {
            static constexpr std::array<std::uint8_t, 6> Interesting8{0, 1, 0x7F, 0x80, 0xFF, 0x20};
            static constexpr std::array<std::uint32_t, 12> Interesting32{
                0, 1, 2, 3, 255, 65535, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x40000000, 0x20000000};
            // NaN, infinities, the smallest denormal and a magnitude that overflows when squared.
            static constexpr std::array<std::uint32_t, 5> InterestingFloats{0x7FC00000, 0x7F800000, 0xFF800000,
                                                                            0x00000001, 0x7E967699};
            static constexpr std::array<std::string_view, 21> Tokens{"\"",         "(",         ")",
                                                                     "[",          "]",         ",",
                                                                     ":",          "\n",        "#include \"",
                                                                     "#constant ", "remstart",  "type ",
                                                                     "endtype\n",  "function ", "endfunction\n",
                                                                     "dim a(",     "1e999",     "0xFFFFFFFFFFFFFFFF",
                                                                     "%",          "$",         "#"};

            switch (Below(Text_ ? 10 : 9))
            {
                case 0:
                    if (!Data.empty())
                        Data[Position(Data.size())] ^= static_cast<std::uint8_t>(1u << Below(8));
                    break;
                case 1:
                    if (!Data.empty())
                        Data[Position(Data.size())] = Interesting8[Below(Interesting8.size())];
                    break;
                case 2:
                    Write32(Data, Below(4) == 0 ? static_cast<std::uint32_t>(Data.size() + Below(16))
                                                : Interesting32[Below(Interesting32.size())]);
                    break;
                case 3:
                    Write32(Data, InterestingFloats[Below(InterestingFloats.size())]);
                    break;
                case 4:
                    if (Data.size() >= 4)
                    {
                        const std::size_t Offset = std::min(Position(Data.size()), Data.size() - 4);
                        std::uint32_t Value = 0;
                        std::memcpy(&Value, Data.data() + Offset, sizeof(Value));
                        Value += static_cast<std::uint32_t>(Below(17)) - 8u;
                        std::memcpy(Data.data() + Offset, &Value, sizeof(Value));
                    }
                    break;
                case 5:
                    if (!Data.empty())
                    {
                        const std::size_t Start = Position(Data.size());
                        const std::size_t Length = std::min(Below(std::size_t{1} << Below(14)), Data.size() - Start);
                        Data.erase(Data.begin() + static_cast<std::ptrdiff_t>(Start),
                                   Data.begin() + static_cast<std::ptrdiff_t>(Start + Length));
                    }
                    break;
                case 6:
                    if (!Other.empty())
                    {
                        const std::size_t Start = Below(Other.size());
                        const std::size_t Length = std::min(Below(std::size_t{1} << Below(14)), Other.size() - Start);
                        Data.insert(Data.begin() + static_cast<std::ptrdiff_t>(Position(Data.size())),
                                    Other.begin() + static_cast<std::ptrdiff_t>(Start),
                                    Other.begin() + static_cast<std::ptrdiff_t>(Start + Length));
                    }
                    break;
                case 7:
                    Data.resize(Below(Data.size() + 1));
                    break;
                case 8:
                    // A short run repeated many times: deep nesting, long tokens, big element counts.
                    if (!Data.empty())
                    {
                        const std::size_t Start = Below(Data.size());
                        const Bytes Chunk(
                            Data.begin() + static_cast<std::ptrdiff_t>(Start),
                            Data.begin() + static_cast<std::ptrdiff_t>(std::min(Start + 1 + Below(16), Data.size())));
                        Bytes Repeated;
                        for (std::size_t Count = std::size_t{1} << Below(15); Count > 0; --Count)
                            Repeated.insert(Repeated.end(), Chunk.begin(), Chunk.end());
                        Data.insert(Data.begin() + static_cast<std::ptrdiff_t>(Start), Repeated.begin(),
                                    Repeated.end());
                    }
                    break;
                default:
                {
                    const std::string_view Token = Tokens[Below(Tokens.size())];
                    Data.insert(Data.begin() + static_cast<std::ptrdiff_t>(Position(Data.size())), Token.begin(),
                                Token.end());
                    break;
                }
            }
        }

        std::mt19937_64 Random_;
        bool Text_;
    };

    Bytes ReadFile(const std::filesystem::path& Path)
    {
        std::ifstream Input(Path, std::ios::binary);
        return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
    }

    void WriteFile(const std::filesystem::path& Path, std::span<const std::uint8_t> Data)
    {
        std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
        Output.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
    }

    std::string Hexadecimal(std::span<const std::uint8_t> Data)
    {
        std::uint64_t Hash = UINT64_C(14695981039346656037);
        for (const std::uint8_t Byte : Data)
            Hash = (Hash ^ Byte) * UINT64_C(1099511628211);
        std::array<char, 17> Text{};
        std::snprintf(Text.data(), Text.size(), "%016llx", static_cast<unsigned long long>(Hash));
        return Text.data();
    }

    // The CPU and the GPU mesh paths read geometry differently, so they take turns.
    void Render()
    {
        Kernel->Renderer().SetGpuMeshesEnabled(++RenderCount % 2 == 0);
        if (SDL_Surface* Frame = Kernel->Renderer().CaptureScreen())
            SDL_DestroySurface(Frame);
    }

    std::shared_ptr<const dbc::language::SourceDocument> SourceDocument(std::string Text,
                                                                        dbc::language::LanguageProfile Profile)
    {
        return std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{"fuzz.dba", std::move(Text), Profile});
    }

    bool RunFrontend(std::span<const std::uint8_t> Input)
    {
        // Two profiles, picked by a bit a mutation can flip.
        const auto Profile =
            Input.size() % 2 == 0 ? dbc::language::LanguageProfile::Dbc : dbc::language::LanguageProfile::DbPro;
        const dbc::language::Program Program = dbc::language::Compile(
            SourceDocument(std::string(Input.begin(), Input.end()), Profile), {.RejectUnsupported = true});
        if (Program.HasErrors())
            return false;
        const dbc::vm::CompileResult Compiled = dbc::vm::Lower(Program);
        if (!Compiled.Success())
            return false;
        return dbc::vm::Verify(*Compiled.Bytecode).Success();
    }

    bool RunProgramImage(std::span<const std::uint8_t> Input)
    {
        const dbc::interpreter::ProgramImageReadResult Read = dbc::interpreter::ReadProgramImage(Input);
        return Read.Image && Read.Image->Bytecode && dbc::vm::Verify(*Read.Image->Bytecode).Success();
    }

    // Goes through the image store, so that sizes a decoder accepts also reach the GPU upload.
    bool RunPicture(std::span<const std::uint8_t> Input)
    {
        dbc::ImageStore& Images = Kernel->ImageStoreRef();
        if (Input.size() % 2 == 0)
        {
            if (!Images.LoadImageFile(CurrentPath, 1))
                return false;
            Images.DeleteImage(1);
            return true;
        }
        if (!Images.LoadBitmapFile(CurrentPath, 1))
            return false;
        Images.DeleteBitmap(1);
        return true;
    }

    bool RunSound(std::span<const std::uint8_t>)
    {
        if (!Kernel->Audio().LoadSound(CurrentPath, 1, true))
            return false;
        Kernel->Memblocks().MakeMemblockFromSound(2, 1);
        Kernel->Audio().DeleteSound(1);
        return true;
    }

    bool RunObject(std::span<const std::uint8_t>)
    {
        dbc::ObjectManager& Objects = Kernel->Objects();
        if (!Objects.LoadObject(CurrentPath, 1))
            return false;
        Objects.SetObjectFrame(1, 1.5f);
        Render();
        Objects.SaveObject(OutputPath, 1);
        Objects.DeleteObject(1);
        return true;
    }

    // The first byte picks the decoder; the rest is the memblock. Each decoded result is used and encoded
    // back, since a decoder that lets bad data through only crashes what comes after it.
    bool RunMemblock(std::span<const std::uint8_t> Input)
    {
        if (Input.size() < 2)
            return false;
        dbc::MemblockSystem& Memblocks = Kernel->Memblocks();
        if (!Memblocks.ReplaceMemblock(1, Bytes(Input.begin() + 1, Input.end())))
            return false;
        switch (Input[0] % 4)
        {
            case 0:
                if (!Memblocks.MakeMeshFromMemblock(1, 1))
                    return false;
                Kernel->Objects().MakeObject(1, 1, 0);
                Render();
                Memblocks.MakeMemblockFromMesh(2, 1);
                Kernel->Objects().DeleteObject(1);
                Kernel->Objects().DeleteMesh(1);
                return true;
            case 1:
                if (!Memblocks.MakeImageFromMemblock(1, 1))
                    return false;
                Memblocks.MakeMemblockFromImage(2, 1);
                Kernel->ImageStoreRef().DeleteImage(1);
                return true;
            case 2:
                if (!Memblocks.MakeBitmapFromMemblock(1, 1))
                    return false;
                Memblocks.MakeMemblockFromBitmap(2, 1);
                Kernel->ImageStoreRef().DeleteBitmap(1);
                return true;
            default:
                if (!Memblocks.MakeSoundFromMemblock(1, 1))
                    return false;
                Memblocks.MakeMemblockFromSound(2, 1);
                Kernel->Audio().DeleteSound(1);
                return true;
        }
    }

    Bytes ImageSeedFromSource(const std::filesystem::path& Path)
    {
        const Bytes Source = ReadFile(Path);
        const dbc::language::Program Program = dbc::language::Compile(
            SourceDocument(std::string(Source.begin(), Source.end()), dbc::language::LanguageProfile::DbPro),
            {.RejectUnsupported = true});
        if (Program.HasErrors())
            return {};
        const dbc::vm::CompileResult Compiled = dbc::vm::Lower(Program);
        return Compiled.Success() ? dbc::interpreter::WriteProgramImage(*Compiled.Bytecode, {}, Path.parent_path())
                                  : Bytes{};
    }

    Bytes ObjectSeedFromModel(const std::filesystem::path& Path)
    {
        if (!Kernel->Objects().LoadObject(Path.string(), 1))
            return {};
        Kernel->Objects().SaveObject(OutputPath, 1);
        Kernel->Objects().DeleteObject(1);
        return ReadFile(OutputPath);
    }

    Bytes SmallWave()
    {
        Bytes Wave;
        const auto Text = [&](std::string_view Value) { Wave.insert(Wave.end(), Value.begin(), Value.end()); };
        const auto Number = [&](std::uint32_t Value, int Size)
        {
            for (int Index = 0; Index < Size; ++Index)
                Wave.push_back(static_cast<std::uint8_t>(Value >> (Index * 8)));
        };
        Text("RIFF");
        Number(36 + 16, 4);
        Text("WAVEfmt ");
        Number(16, 4);
        Number(1, 2);
        Number(1, 2);
        Number(22050, 4);
        Number(44100, 4);
        Number(2, 2);
        Number(16, 2);
        Text("data");
        Number(16, 4);
        Wave.resize(Wave.size() + 16, 0x40);
        return Wave;
    }

    void AddWord(Bytes& Data, std::uint32_t Value)
    {
        for (int Index = 0; Index < 4; ++Index)
            Data.push_back(static_cast<std::uint8_t>(Value >> (Index * 8)));
    }

    // Small pictures of each block format: the example files spend most of their bytes on pixels.
    std::vector<Bytes> DdsSeeds()
    {
        std::vector<Bytes> Seeds;
        for (const std::uint32_t FourCc :
             {UINT32_C(0x31545844), UINT32_C(0x33545844), UINT32_C(0x35545844), UINT32_C(0)})
        {
            Bytes Data;
            AddWord(Data, 0x20534444); // "DDS "
            AddWord(Data, 124);
            AddWord(Data, 0x1007); // Caps, height, width, pixel format.
            AddWord(Data, 4);
            AddWord(Data, 4);
            AddWord(Data, FourCc == 0 ? 16 : 16);
            Data.resize(76, 0);
            AddWord(Data, 32);
            AddWord(Data, FourCc == 0 ? 0x41 : 0x4); // RGB with alpha, or a FourCC.
            AddWord(Data, FourCc);
            AddWord(Data, FourCc == 0 ? 32 : 0);
            for (const std::uint32_t Mask : {0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u})
                AddWord(Data, FourCc == 0 ? Mask : 0);
            Data.resize(128, 0);
            Data.resize(128 + 64, 0x5A);
            Seeds.push_back(std::move(Data));
        }
        return Seeds;
    }

    std::vector<Bytes> MemblockSeeds()
    {
        std::vector<Bytes> Seeds;
        const auto AddSeed = [&](std::uint8_t Decoder, int Block)
        {
            const Bytes* Data = Kernel->Memblocks().MemblockData(Block);
            if (!Data)
                return;
            Bytes Seed{Decoder};
            Seed.insert(Seed.end(), Data->begin(), Data->end());
            Seeds.push_back(std::move(Seed));
        };

        dbc::ObjectManager& Objects = Kernel->Objects();
        Objects.MakeObjectSphere(1, 2.0f, 6, 8);
        if (Objects.MakeMeshFromObject(1, 1) && Kernel->Memblocks().MakeMemblockFromMesh(1, 1))
            AddSeed(0, 1);
        Objects.DeleteObject(1);
        Objects.DeleteMesh(1);

        for (const std::uint32_t Depth : {16u, 24u, 32u})
        {
            Bytes Picture;
            AddWord(Picture, 2);
            AddWord(Picture, 2);
            AddWord(Picture, Depth);
            Picture.resize(Picture.size() + 4 * (Depth / 8), 0x7F);
            for (const std::uint8_t Decoder : {std::uint8_t{1}, std::uint8_t{2}})
            {
                Bytes Seed{Decoder};
                Seed.insert(Seed.end(), Picture.begin(), Picture.end());
                Seeds.push_back(std::move(Seed));
            }
        }

        const std::filesystem::path Wave = WorkDirectory / "seed.wav";
        WriteFile(Wave, SmallWave());
        if (Kernel->Audio().LoadSound(Wave.string(), 1, true) && Kernel->Memblocks().MakeMemblockFromSound(1, 1))
            AddSeed(3, 1);
        Kernel->Audio().DeleteSound(1);
        return Seeds;
    }

    struct Target
    {
        std::string_view Name;
        std::string_view Extension;
        std::vector<std::string_view> SeedExtensions;
        bool Text;
        bool NeedsKernel;
        bool Classic;
        bool (*Run)(std::span<const std::uint8_t>);
        Bytes (*ConvertSeed)(const std::filesystem::path&);
        std::vector<Bytes> (*BuiltInSeeds)();
    };

    const std::vector<Target>& Targets()
    {
        static const std::vector<Target> Values{
            {"frontend", ".dba", {".dba"}, true, false, false, RunFrontend, nullptr, nullptr},
            {"image", ".obc", {".dba"}, false, false, false, RunProgramImage, ImageSeedFromSource, nullptr},
            {"dds", ".dds", {".dds"}, false, true, false, RunPicture, nullptr, DdsSeeds},
            {"wav", ".wav", {".wav"}, false, true, false, RunSound, nullptr, nullptr},
            {"dbo", ".dbo", {".x", ".dbo"}, false, true, false, RunObject, ObjectSeedFromModel, nullptr},
            {"memblock-classic", ".bin", {}, false, true, true, RunMemblock, nullptr, MemblockSeeds},
            {"memblock-pro", ".bin", {}, false, true, false, RunMemblock, nullptr, MemblockSeeds},
        };
        return Values;
    }

    std::string LowerExtension(const std::filesystem::path& Path)
    {
        std::string Extension = Path.extension().string();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](unsigned char Value) { return static_cast<char>(std::tolower(Value)); });
        return Extension;
    }

    // A file named directly is taken as is unless its extension asks for conversion, so that a saved crash
    // replays; a directory contributes the files of the target's seed extensions.
    std::vector<Bytes> LoadSeeds(const Target& Value, std::span<char*> Roots)
    {
        std::vector<Bytes> Seeds;
        const auto Add = [&](const std::filesystem::path& Path)
        {
            const std::string Extension = LowerExtension(Path);
            const bool Convert = Value.ConvertSeed && Extension != Value.Extension &&
                                 std::find(Value.SeedExtensions.begin(), Value.SeedExtensions.end(), Extension) !=
                                     Value.SeedExtensions.end();
            Bytes Data = Convert ? Value.ConvertSeed(Path) : ReadFile(Path);
            if (!Data.empty())
                Seeds.push_back(std::move(Data));
        };
        for (const char* Root : Roots)
        {
            if (std::filesystem::is_regular_file(Root))
            {
                Add(Root);
                continue;
            }
            std::error_code Error;
            for (const auto& Entry : std::filesystem::recursive_directory_iterator(
                     Root, std::filesystem::directory_options::skip_permission_denied, Error))
            {
                const std::string Extension = LowerExtension(Entry.path());
                if (Entry.is_regular_file() && Entry.file_size() <= MaximumSeedSize &&
                    std::find(Value.SeedExtensions.begin(), Value.SeedExtensions.end(), Extension) !=
                        Value.SeedExtensions.end())
                    Add(Entry.path());
            }
        }
        return Seeds;
    }

    void StopHangingInput(int)
    {
        // AddressSanitizer turns the abort into a report with this thread's stack.
        const char Message[] = "fuzz_inputs: input ran past the watchdog\n";
        [[maybe_unused]] const ssize_t Written = write(STDERR_FILENO, Message, sizeof(Message) - 1);
        std::abort();
    }

    // A slow input points at superlinear work before one grows long enough to trip the watchdog.
    std::chrono::steady_clock::duration SlowestRun{};
    std::size_t SlowestRunSize = 0;

    bool RunInput(const Target& Value, std::span<const std::uint8_t> Input)
    {
        WriteFile(CurrentPath, Input);
        alarm(WatchdogSeconds);
        const auto Started = std::chrono::steady_clock::now();
        const bool Accepted = Value.Run(Input);
        const auto Elapsed = std::chrono::steady_clock::now() - Started;
        alarm(0);
        if (Elapsed > SlowestRun)
        {
            SlowestRun = Elapsed;
            SlowestRunSize = Input.size();
        }
        return Accepted;
    }

    long long SlowestRunMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(SlowestRun).count();
    }
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 4)
    {
        std::fprintf(stderr, "usage: fuzz_inputs <target> <seconds> <work directory> [seed file or directory]...\n");
        return 2;
    }
    const auto Selected = std::find_if(Targets().begin(), Targets().end(),
                                       [&](const Target& Value) { return Value.Name == Arguments[1]; });
    if (Selected == Targets().end())
    {
        std::fprintf(stderr, "fuzz_inputs: unknown target '%s'\n", Arguments[1]);
        return 2;
    }
    const Target& Value = *Selected;
    const int Seconds = std::atoi(Arguments[2]);
    WorkDirectory = Arguments[3];
    const std::filesystem::path Corpus = WorkDirectory / "corpus";
    std::filesystem::create_directories(Corpus);
    CurrentPath = (WorkDirectory / ("current" + std::string(Value.Extension))).string();
    OutputPath = (WorkDirectory / ("output" + std::string(Value.Extension))).string();

    std::signal(SIGALRM, StopHangingInput);
    setenv(dbc::KHeadlessHint, "1", 1);
    dbc::log::SetLevel("off");
    // Owned here: the engine must stop before the statics it uses are destroyed.
    std::unique_ptr<dbc::EngineKernel> OwnedKernel;
    if (Value.NeedsKernel)
    {
        OwnedKernel = std::make_unique<dbc::EngineKernel>();
        Kernel = OwnedKernel.get();
        if (!Kernel->Initialize("fuzz", 16, 16) || !Kernel->Core().EnsureInitialized())
        {
            std::fprintf(stderr, "fuzz_inputs: the engine did not start\n");
            return 1;
        }
        Kernel->ConfigureRuntimeCompatibility(Value.Classic ? dbc::RuntimeCompatibilityPolicy::Classic()
                                                            : dbc::RuntimeCompatibilityPolicy::Professional());
        Kernel->Camera().PositionCamera(0.0f, 0.0f, -12.0f);
    }

    std::vector<Bytes> Pool = Value.BuiltInSeeds ? Value.BuiltInSeeds() : std::vector<Bytes>{};
    std::vector<Bytes> Files = LoadSeeds(Value, std::span(Arguments + 4, Arguments + ArgumentCount));
    Pool.insert(Pool.end(), std::make_move_iterator(Files.begin()), std::make_move_iterator(Files.end()));
    for (const auto& Entry : std::filesystem::directory_iterator(Corpus))
        Pool.push_back(ReadFile(Entry.path()));

    std::vector<Bytes> Kept;
    std::size_t AcceptedSeeds = 0;
    for (Bytes& Input : Pool)
    {
        AcceptedSeeds += RunInput(Value, Input) ? 1 : 0;
        if (TakeNewEdges())
            Kept.push_back(std::move(Input));
    }
    std::printf("%s: %zu of %zu seeds accepted, %zu kept, %u edges instrumented, slowest %lld ms on %zu bytes\n",
                Arguments[1], AcceptedSeeds, Pool.size(), Kept.size(), EdgeCount, SlowestRunMilliseconds(),
                SlowestRunSize);
    std::fflush(stdout);
    if (Seconds == 0)
        return 0;
    // Seeds no decoder accepts would leave only its first checks to fuzz.
    if (AcceptedSeeds == 0 || Kept.empty())
    {
        std::fprintf(stderr, "fuzz_inputs: no usable seed for '%s'\n", Arguments[1]);
        return 1;
    }

    Mutator Mutation(std::random_device{}(), Value.Text);
    const auto Started = std::chrono::steady_clock::now();
    const auto Deadline = Started + std::chrono::seconds(Seconds);
    auto NextReport = Started + std::chrono::seconds(30);
    std::uint64_t Runs = 0;
    for (auto Now = Started; Now < Deadline; Now = std::chrono::steady_clock::now())
    {
        Bytes Input = Kept[Mutation.Below(Kept.size())];
        Mutation.Mutate(Input, Kept[Mutation.Below(Kept.size())]);
        RunInput(Value, Input);
        ++Runs;
        if (TakeNewEdges())
        {
            WriteFile(Corpus / Hexadecimal(Input), Input);
            Kept.push_back(std::move(Input));
        }
        if (Now >= NextReport)
        {
            std::printf("%s: %llu runs, %zu inputs kept\n", Arguments[1], static_cast<unsigned long long>(Runs),
                        Kept.size());
            std::fflush(stdout);
            NextReport = Now + std::chrono::seconds(30);
        }
    }
    std::printf("%s: finished, %llu runs, %zu inputs kept, slowest %lld ms on %zu bytes\n", Arguments[1],
                static_cast<unsigned long long>(Runs), Kept.size(), SlowestRunMilliseconds(), SlowestRunSize);
    return 0;
}
