#include "engine/runtime_compatibility.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/data_file_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/file_block_system.hpp"
#include "engine/systems/file_map_system.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/memblock_system.hpp"
#include "engine/systems/native_file_dialog.hpp"
#include "interpreter/engine_builtin_context.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"
#include "language/frontend.hpp"
#include "unique_suffix.hpp"
#include "vm/compiler.hpp"
#include "vm/verifier.hpp"

#include <SDL3/SDL_platform.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using dbc::interpreter::detail::BuiltinContext;
    using dbc::interpreter::detail::BuiltinValue;
    using dbc::language::LanguageProfile;
    using dbc::language::SymbolId;
    using dbc::language::SymbolKind;

    // A DBPro program's engine, driven one builtin at a time.
    class Application
    {
      public:
        explicit Application(const char* Title)
        {
            REQUIRE(Kernel_.Initialize(Title, 64, 64));
            Kernel_.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
            Context_.Kernel = &Kernel_;
            Context_.Profile = LanguageProfile::DbPro;
        }

        bool Command(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return dbc::interpreter::detail::InvokeCommand(Context_, Symbol, Arguments, Error);
        }

        void Require(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            REQUIRE_MESSAGE(Command(Symbol, std::move(Arguments)), Error);
        }

        BuiltinValue Function(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            BuiltinValue Result;
            REQUIRE_MESSAGE(dbc::interpreter::detail::InvokeFunction(Context_, Symbol, Arguments, Result, Error),
                            Error);
            return Result;
        }

        double Number(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return Function(Symbol, std::move(Arguments)).AsNumber();
        }

        std::string Text(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return Function(Symbol, std::move(Arguments)).AsString();
        }

        bool CommandFails(SymbolId Symbol, std::vector<BuiltinValue> Arguments)
        {
            Error.clear();
            return !Command(Symbol, std::move(Arguments)) && !Error.empty();
        }

        bool Fails(SymbolId Symbol, std::vector<BuiltinValue> Arguments)
        {
            BuiltinValue Result;
            Error.clear();
            return !dbc::interpreter::detail::InvokeFunction(Context_, Symbol, Arguments, Result, Error) &&
                   !Error.empty();
        }

        dbc::EngineKernel& Kernel()
        {
            return Kernel_;
        }

        BuiltinContext& Context()
        {
            return Context_;
        }

        std::string Error;

      private:
        dbc::EngineKernel Kernel_;
        BuiltinContext Context_;
    };

    // A directory of its own for one test, removed afterwards.
    class ScratchDirectory
    {
      public:
        explicit ScratchDirectory(const char* Name)
            : Path_(std::filesystem::temp_directory_path() /
                    (std::string(Name) + "-" + std::to_string(dbc::testing::UniqueSuffix())))
        {
            std::filesystem::create_directories(Path_);
        }

        ~ScratchDirectory()
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Path_, Ignored);
        }

        std::string Write(const char* Name, const std::string& Contents) const
        {
            const std::filesystem::path File = Path_ / Name;
            std::ofstream(File, std::ios::binary) << Contents;
            return File.generic_string();
        }

        const std::filesystem::path& Path() const
        {
            return Path_;
        }

      private:
        std::filesystem::path Path_;
    };

    BuiltinValue Int(int Value)
    {
        return BuiltinValue::FromNumber(static_cast<double>(Value));
    }

    std::vector<std::string> ChecklistNames(const BuiltinContext& Context)
    {
        std::vector<std::string> Names;
        for (const auto& Item : Context.Checklist)
            Names.push_back(Item.Name);
        return Names;
    }

    bool CompilesInDbPro(std::string Text)
    {
        const auto Source = std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{"test.dba", std::move(Text), LanguageProfile::DbPro});
        const dbc::language::Program Program = dbc::language::Compile(Source);
        for (const auto& Diagnostic : Program.Diagnostics)
            INFO(dbc::language::FormatDiagnostic(*Source, Diagnostic));
        if (Program.HasErrors())
            return false;
        const dbc::vm::CompileResult Result = dbc::vm::Lower(Program);
        return Result.Success() && dbc::vm::Verify(*Result.Bytecode).Success();
    }
} // namespace

TEST_CASE("DBPro EAX forms bind and run as permanent stubs, as on a card without EAX")
{
    using dbc::language::BuiltinValueType;
    using dbc::language::CompatibilityStubPolicy;
    using dbc::language::Type;
    using dbc::language::TypeKind;

    Application App("enhancement-eax-stubs");
    dbc::interpreter::detail::EngineBuiltinHost Host(App.Context());
    std::ostringstream Program;
    std::size_t Stubs = 0;
    std::uint64_t InvocationId = 1;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (dbc::language::StubPolicyFor(Descriptor, LanguageProfile::DbPro) != CompatibilityStubPolicy::Permanent)
            continue;
        CAPTURE(Descriptor.CanonicalName);
        CHECK(Descriptor.CanonicalName.find("EAX") != std::string_view::npos);
        ++Stubs;
        for (const dbc::language::BuiltinOverload* Overload :
             dbc::language::Overloads(Descriptor.Id, LanguageProfile::DbPro))
        {
            std::vector<dbc::vm::Value> Arguments;
            Program << (Descriptor.Kind == SymbolKind::Function ? "print " : "") << Descriptor.CanonicalName
                    << (Descriptor.Kind == SymbolKind::Function ? "(" : " ");
            for (std::size_t Index = 0; Index < Overload->Parameters.size(); ++Index)
            {
                Program << (Index == 0 ? "" : ", ") << "1";
                Arguments.push_back(Overload->Parameters[Index].ValueType == BuiltinValueType::Real
                                        ? dbc::vm::Value::Real(1.0)
                                        : dbc::vm::Value::Integer(1));
            }
            Program << (Descriptor.Kind == SymbolKind::Function ? ")\n" : "\n");

            const Type Result = Overload->ReturnType == BuiltinValueType::Void
                                    ? Type{TypeKind::Void, {}}
                                    : (Overload->ReturnType == BuiltinValueType::Real ? Type{TypeKind::Real, {}}
                                                                                      : Type{TypeKind::Integer, {}});
            const dbc::vm::BuiltinResult Invoked =
                Host.Invoke({InvocationId++, Descriptor.Id, Arguments, Result, false});
            REQUIRE(Invoked.State == dbc::vm::BuiltinState::Completed);
            if (Descriptor.Kind == SymbolKind::Command)
            {
                CHECK_FALSE(Invoked.ReturnValue.has_value());
                continue;
            }
            REQUIRE(Invoked.ReturnValue.has_value());
            CHECK(Invoked.ReturnValue->AsReal() == 0.0);
        }
    }
    CHECK(Stubs == 80);
    CHECK(CompilesInDbPro(Program.str()));
}

TEST_CASE("PERFORM CHECKLIST FOR EAX EFFECTS lists the presets in the Enhancement Pack's order")
{
    Application App("enhancement-eax-checklist");
    App.Context().Checklist = {{"stale"}, {"entries"}};
    App.Require(SymbolId::Command_ExtEnhancementPerformChecklistForEaxEffects);
    const auto& Checklist = App.Context().Checklist;
    REQUIRE(Checklist.size() == 26);
    CHECK(Checklist.front().Name == "Generic");
    CHECK(Checklist[7].Name == "Concert Hall");
    CHECK(Checklist.back().Name == "Psychotic");
}

TEST_CASE("Enhancement Pack processor queries describe the host processor")
{
    Application App("enhancement-processor");
    const double Count = App.Number(SymbolId::Function_ExtEnhancementGetProcessorCount);
    CHECK(Count >= 1.0);
    CHECK(App.Text(SymbolId::Function_ExtEnhancementGetProcessorPlatform, {Int(0)}) == SDL_GetPlatform());
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetProcessorFrequency, {Int(0)}) >= 0.0);
    for (int Sample = 0; Sample < 2; ++Sample)
    {
        const double Usage = App.Number(SymbolId::Function_ExtEnhancementGetProcessorUsage, {Int(0)});
        CHECK(Usage >= 0.0);
        CHECK(Usage <= 100.0);
    }

    App.Context().Checklist = {{"stale"}};
    App.Require(SymbolId::Command_ExtEnhancementGetProcessorSupportedFeatures, {Int(0)});
    const std::vector<std::string> Features = ChecklistNames(App.Context());
    CHECK(std::find(Features.begin(), Features.end(), "stale") == Features.end());
    CHECK((std::find(Features.begin(), Features.end(), "MP") != Features.end()) == (Count > 1.0));

    App.Require(SymbolId::Command_ExtEnhancementGetProcessorOnChipFeatures, {Int(0)});
    for (const std::string& Name : ChecklistNames(App.Context()))
    {
        CAPTURE(Name);
        CHECK(((Name.rfind("L", 0) == 0 && Name.find(" cache = ") == 2) || Name == "APIC present"));
    }
    App.Require(SymbolId::Command_ExtEnhancementGetProcessorPowerManagement, {Int(0)});
    for (const std::string& Name : ChecklistNames(App.Context()))
    {
        CAPTURE(Name);
        CHECK((Name == "Thermal monitor" || Name == "Temperature sensor" || Name == "Frequency ID" ||
               Name == "Voltage ID"));
    }
}

TEST_CASE("Enhancement Pack memory queries round up and reject unknown units with zero")
{
    Application App("enhancement-memory");
    const double Kilobytes = App.Number(SymbolId::Function_ExtEnhancementGetInstalledMemory, {Int(0)});
    const double Megabytes = App.Number(SymbolId::Function_ExtEnhancementGetInstalledMemory, {Int(1)});
    CHECK(Kilobytes > 0.0);
    CHECK(Megabytes >= 1.0);
    CHECK(Megabytes * 1024.0 >= Kilobytes);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetInstalledMemory, {Int(3)}) == 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetMemoryAvailable, {Int(1)}) <= Megabytes);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetMemoryAvailable, {Int(-1)}) == 0.0);
    const double Used = App.Number(SymbolId::Function_ExtEnhancementGetMemoryPercentUsed);
    CHECK(Used >= 0.0);
    CHECK(Used <= 100.0);
    CHECK(Used + App.Number(SymbolId::Function_ExtEnhancementGetMemoryPercentFree) ==
          doctest::Approx(100.0).epsilon(0.02));
}

TEST_CASE("Enhancement Pack drive queries read the system volume and answer absent drives as DBPro did")
{
    Application App("enhancement-drives");
    REQUIRE(App.Number(SymbolId::Function_ExtEnhancementGetDriveCount) >= 1.0);
    const double Total = App.Number(SymbolId::Function_ExtEnhancementGetDriveTotalSize, {Int(0), Int(1)});
    const double Free = App.Number(SymbolId::Function_ExtEnhancementGetDriveFreeSpace, {Int(0), Int(1)});
    const double Used = App.Number(SymbolId::Function_ExtEnhancementGetDriveUsedSpace, {Int(0), Int(1)});
    CHECK(Total > 0.0);
    CHECK(Free >= 0.0);
    CHECK(Free + Used <= Total + 1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveFileLengthSupport, {Int(0)}) > 0.0);
    CHECK_FALSE(App.Text(SymbolId::Function_ExtEnhancementGetDriveFileSystem, {Int(0)}).empty());

    // Drive 24 exists on no machine the tests run on.
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveTotalSize, {Int(24), Int(0)}) == 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveFreeSpace, {Int(24), Int(0)}) == -1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveFileLengthSupport, {Int(24)}) == -1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveCylinderCount, {Int(24)}) == 0.0);
    CHECK(App.Text(SymbolId::Function_ExtEnhancementGetDriveSerial, {Int(24)}).empty());
    CHECK(App.Fails(SymbolId::Function_ExtEnhancementGetDriveSerial, {Int(25)}));
    CHECK(App.Fails(SymbolId::Function_ExtEnhancementGetDriveTotalSize, {Int(-1), Int(0)}));

    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetCdCount) >= 0.0);
    CHECK(App.Text(SymbolId::Function_ExtEnhancementGetCdLetter, {Int(0)}).empty());
    CHECK(App.Fails(SymbolId::Function_ExtEnhancementGetCdLetter, {Int(25)}));
}

TEST_CASE("Enhancement Pack names, address and display queries answer without a display")
{
    Application App("enhancement-names");
    CHECK_FALSE(App.Text(SymbolId::Function_ExtEnhancementGetComputerName).empty());
    CHECK_FALSE(App.Text(SymbolId::Function_ExtEnhancementGetIpAddress).empty());
    App.Text(SymbolId::Function_ExtEnhancementGetUserName);
    App.Text(SymbolId::Function_ExtEnhancementGetWebClient);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayWidth) >= 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayHeight) >= 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayDepth) >= -1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayRefreshRate) >= -1.0);
}

TEST_CASE("DataFileSystem splits rows as the Enhancement Pack did")
{
    using Fields = std::vector<std::string>;
    CHECK(dbc::DataFileSystem::SplitRow("a,b,,c", ",") == Fields{"a", "b", "", "c"});
    CHECK(dbc::DataFileSystem::SplitRow("\"x, y\",\"z\"", ",") == Fields{"x, y", "z"});
    CHECK(dbc::DataFileSystem::SplitRow("a::b", "::") == Fields{"a", "b"});
    CHECK(dbc::DataFileSystem::SplitRow("\"alone\"", ",") == Fields{"alone"});
    CHECK(dbc::DataFileSystem::SplitRow("plain", ",") == Fields{"plain"});
    CHECK(dbc::DataFileSystem::SplitRow("", ",") == Fields{""});
    CHECK(dbc::DataFileSystem::SplitRow("a,b", "") == Fields{"a,b"});
}

TEST_CASE("Enhancement Pack data files read delimited cells by column and row")
{
    ScratchDirectory Scratch("opendbc-data-file");
    const std::string Table = Scratch.Write("scores.csv", "name;score\r\n\"Ada\";\"42\"\r\nBob;7;extra\r\n");
    Application App("enhancement-data-files");
    using S = SymbolId;
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileExists, {Int(3)}) == 0.0);
    App.Require(S::Command_ExtEnhancementOpenDataFile,
                {BuiltinValue::FromString(Table), BuiltinValue::FromString(";"), Int(3)});
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileExists, {Int(3)}) == 1.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileRowCount, {Int(3)}) == 3.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileColumnCount, {Int(3)}) == 3.0);
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(1), Int(1)}) == "name");
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(2), Int(2)}) == "42");
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(3), Int(3)}) == "extra");
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(3), Int(1)}).empty());
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(0), Int(1)}).empty());
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(1), Int(9)}).empty());

    Scratch.Write("scores.csv", "only;row\n");
    App.Require(S::Command_ExtEnhancementUpdateDataFile, {Int(3)});
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileRowCount, {Int(3)}) == 1.0);
    CHECK(App.Text(S::Function_ExtEnhancementGetDataFileCell, {Int(3), Int(2), Int(1)}) == "row");

    CHECK(App.CommandFails(S::Command_ExtEnhancementOpenDataFile,
                           {BuiltinValue::FromString(Table), BuiltinValue::FromString(";"), Int(3)}));
    App.Require(S::Command_ExtEnhancementCloseDataFile, {Int(3)});
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileExists, {Int(3)}) == 0.0);
    CHECK(App.CommandFails(S::Command_ExtEnhancementCloseDataFile, {Int(3)}));
    CHECK(App.Fails(S::Function_ExtEnhancementGetDataFileRowCount, {Int(3)}));
    CHECK(App.Fails(S::Function_ExtEnhancementGetDataFileExists, {Int(256)}));
    CHECK(App.CommandFails(
        S::Command_ExtEnhancementOpenDataFile,
        {BuiltinValue::FromString(Scratch.Write("book.XLS", "")), BuiltinValue::FromString("Sheet1"), Int(4)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementOpenDataFile,
                           {BuiltinValue::FromString((Scratch.Path() / "missing.csv").generic_string()),
                            BuiltinValue::FromString(","), Int(4)}));
    CHECK(App.Number(S::Function_ExtEnhancementGetDataFileExists, {Int(4)}) == 0.0);
}

TEST_CASE("Enhancement Pack file maps share memory between programs")
{
    using S = SymbolId;
    const std::string Name = "opendbc-test-map-" + std::to_string(dbc::testing::UniqueSuffix());
    const BuiltinValue MapName = BuiltinValue::FromString(Name);
    Application Host("enhancement-file-map-host");
    Application Guest("enhancement-file-map-guest");
    CHECK(Guest.CommandFails(S::Command_ExtEnhancementOpenFileMap, {Int(1), MapName}));

    Host.Require(S::Command_ExtEnhancementCreateFileMap, {Int(1), MapName, Int(1024)});
    Host.Require(S::Command_ExtEnhancementSetFileMapDword, {Int(1), Int(0), Int(-2)});
    Host.Require(S::Command_ExtEnhancementSetFileMapFloat, {Int(1), Int(4), BuiltinValue::FromNumber(1.5)});
    Host.Require(S::Command_ExtEnhancementSetFileMapString,
                 {Int(1), Int(8), BuiltinValue::FromString("hello from host")});
    Host.Require(S::Command_ExtEnhancementSetFileMapString, {Int(1), Int(8), BuiltinValue::FromString("hi")});

    // Creating a map another program made joins it, keeping its size.
    Guest.Require(S::Command_ExtEnhancementCreateFileMap, {Int(2), MapName, Int(16)});
    CHECK(Guest.Number(S::Function_ExtEnhancementGetFileMapDword, {Int(2), Int(0)}) == -2.0);
    CHECK(Guest.Number(S::Function_ExtEnhancementGetFileMapFloat, {Int(2), Int(4)}) == 1.5);
    CHECK(Guest.Text(S::Function_ExtEnhancementGetFileMapString, {Int(2), Int(8)}) == "hi");
    Guest.Require(S::Command_ExtEnhancementSetFileMapDword, {Int(2), Int(1020), Int(7)});
    CHECK(Host.Number(S::Function_ExtEnhancementGetFileMapDword, {Int(1), Int(1020)}) == 7.0);

    CHECK(Host.Fails(S::Function_ExtEnhancementGetFileMapDword, {Int(1), Int(1021)}));
    CHECK(Host.Fails(S::Function_ExtEnhancementGetFileMapDword, {Int(1), Int(-1)}));
    CHECK(Host.CommandFails(S::Command_ExtEnhancementSetFileMapString,
                            {Int(1), Int(1020), BuiltinValue::FromString("too long")}));
    CHECK(Host.Fails(S::Function_ExtEnhancementGetFileMapDword, {Int(9), Int(0)}));
    CHECK(Host.CommandFails(S::Command_ExtEnhancementCreateFileMap, {Int(256), MapName, Int(16)}));
    CHECK(Host.CommandFails(S::Command_ExtEnhancementCreateFileMap, {Int(3), MapName, Int(0)}));

    const std::string FullField(dbc::FileMapSystem::StringField, 'a');
    Host.Require(S::Command_ExtEnhancementSetFileMapDword, {Int(1), Int(767), Int(0x12345678)});
    Host.Require(S::Command_ExtEnhancementSetFileMapString, {Int(1), Int(512), BuiltinValue::FromString(FullField)});
    CHECK(Guest.Text(S::Function_ExtEnhancementGetFileMapString, {Int(2), Int(512)}) == FullField);
    CHECK(Host.CommandFails(S::Command_ExtEnhancementSetFileMapString,
                            {Int(1), Int(512), BuiltinValue::FromString(std::string(256, 'b'))}));
    CHECK(Guest.Text(S::Function_ExtEnhancementGetFileMapString, {Int(2), Int(512)}) == FullField);
    CHECK(Guest.Number(S::Function_ExtEnhancementGetFileMapDword, {Int(2), Int(767)}) == 0x12345678);

    // Another program's write ends the wait long before its five-second timeout.
    const auto Started = std::chrono::steady_clock::now();
    std::thread Writer(
        [&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::string Ignored;
            Host.Kernel().FileMaps().WriteDword(1, 0, 3, &Ignored);
        });
    Guest.Require(S::Command_ExtEnhancementWaitForFileMapEvent, {Int(2)});
    Writer.join();
    CHECK(std::chrono::steady_clock::now() - Started < std::chrono::seconds(4));
    CHECK(Guest.Number(S::Function_ExtEnhancementGetFileMapDword, {Int(2), Int(0)}) == 3.0);

    Guest.Require(S::Command_ExtEnhancementCloseFileMap, {Int(2)});
    Guest.Require(S::Command_ExtEnhancementDestroyFileMap, {Int(2)});
    CHECK(Guest.Fails(S::Function_ExtEnhancementGetFileMapDword, {Int(2), Int(0)}));
    Host.Require(S::Command_ExtEnhancementDestroyFileMap, {Int(1)});
    CHECK(Guest.CommandFails(S::Command_ExtEnhancementOpenFileMap, {Int(2), MapName}));
}

TEST_CASE("FileBlockSystem names entries as paths without a root or dot folders")
{
    CHECK(dbc::FileBlockSystem::NormaliseEntryName(".\\media\\a.bmp") == "media/a.bmp");
    CHECK(dbc::FileBlockSystem::NormaliseEntryName("C:\\games//data/./b.bmp") == "games/data/b.bmp");
    CHECK(dbc::FileBlockSystem::NormaliseEntryName("/object.dbo") == "object.dbo");
    CHECK(dbc::FileBlockSystem::NormaliseEntryName("../up.txt") == "../up.txt");
}

TEST_CASE("Enhancement Pack file blocks store, encrypt and extract files")
{
    using S = SymbolId;
    ScratchDirectory Scratch("opendbc-file-block");
    std::filesystem::create_directories(Scratch.Path() / "media");
    Scratch.Write("media/notes.txt", "hello file blocks, hello file blocks, hello file blocks");
    Scratch.Write("media/raw.bin", std::string("\x01\x02\x03\x04", 4));
    Application App("enhancement-file-blocks");
    App.Context().AssetRoot = Scratch.Path();
    const auto Str = [](const char* Text) { return BuiltinValue::FromString(Text); };

    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockExists, {Int(1)}) == 0.0);
    App.Require(S::Command_ExtEnhancementCreateFileBlock, {Int(1), Str("media.fb")});
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockCompression, {Int(1)}) == 9.0);
    App.Require(S::Command_ExtEnhancementSetFileBlockKey, {Int(1), Str("mypassword")});
    App.Require(S::Command_ExtEnhancementAddFileToBlock, {Int(1), Str(".\\media\\notes.txt")});
    App.Require(S::Command_ExtEnhancementSetFileBlockCompression, {Int(1), Int(0)});
    App.Require(S::Command_ExtEnhancementAddFileToBlock, {Int(1), Str("media/raw.bin")});
    App.Require(S::Command_ExtEnhancementAddFileToBlock, {Int(1), Str("media/raw.bin")});
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockCount, {Int(1)}) == 2.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockKey, {Int(1)}) == 1.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockSize, {Int(1)}) > 0.0);
    CHECK(std::filesystem::exists(Scratch.Path() / "media.fb"));
    App.Require(S::Command_ExtEnhancementSaveFileBlock, {Int(1)});
    App.Require(S::Command_ExtEnhancementCloseFileBlock, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockExists, {Int(1)}) == 0.0);

    App.Require(S::Command_ExtEnhancementOpenFileBlock, {Str("media.fb"), Int(2), Str("mypassword")});
    App.Require(S::Command_ExtEnhancementPerformChecklistForFileBlockData, {Int(2)});
    CHECK(ChecklistNames(App.Context()) == std::vector<std::string>{"media/notes.txt", "media/raw.bin"});
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockDataExists, {Int(2), Str("MEDIA\\NOTES.TXT")}) == 1.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockDataExists, {Int(2), Str("missing.txt")}) == 0.0);
    App.Require(S::Command_ExtEnhancementExtractFileFromBlock, {Int(2), Str("media/notes.txt"), Str("out")});
    std::ifstream Extracted(Scratch.Path() / "out/media/notes.txt");
    CHECK(std::string(std::istreambuf_iterator<char>(Extracted), {}) ==
          "hello file blocks, hello file blocks, hello file blocks");
    App.Require(S::Command_ExtEnhancementLoadMemblockFromBlock, {Int(2), Str("media/raw.bin"), Int(4)});
    int Byte = 0;
    std::string Error;
    REQUIRE(App.Kernel().Memblocks().ReadMemblockByte(Byte, 4, 3, &Error));
    CHECK(Byte == 4);
    App.Require(S::Command_ExtEnhancementLoadFileFromBlock, {Int(2), Str("media/notes.txt"), Int(1)});
    CHECK(App.Number(S::Function_FileOpen, {Int(1)}) == 1.0);
    CHECK(App.Number(S::Function_ReadByte, {Int(1)}) == 'h');

    App.Require(S::Command_ExtEnhancementRemoveFileFromBlock, {Int(2), Str("media/raw.bin")});
    CHECK(App.CommandFails(S::Command_ExtEnhancementRemoveFileFromBlock, {Int(2), Str("media/raw.bin")}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementLoadMemblockFromBlock, {Int(2), Str("media/raw.bin"), Int(5)}));
    // Deleting drops the removal instead of saving it.
    App.Require(S::Command_ExtEnhancementDeleteFileBlock, {Int(2)});
    App.Require(S::Command_ExtEnhancementOpenFileBlock, {Str("media.fb"), Int(3)});
    CHECK(App.Number(S::Function_ExtEnhancementGetFileBlockCount, {Int(3)}) == 2.0);
    CHECK(App.CommandFails(S::Command_ExtEnhancementLoadMemblockFromBlock, {Int(3), Str("media/raw.bin"), Int(5)}));
    CHECK(App.Error.find("key") != std::string::npos);

    // An entry naming a parent folder never leaves the extraction folder.
    std::vector<std::uint8_t> Payload = {'x'};
    REQUIRE(App.Kernel().FileBlocks().AddBytes(3, "../escape.txt", Payload, &Error));
    CHECK(App.CommandFails(S::Command_ExtEnhancementExtractFileFromBlock, {Int(3), Str("../escape.txt"), Str("out")}));
    CHECK_FALSE(std::filesystem::exists(Scratch.Path() / "escape.txt"));

    CHECK(App.CommandFails(S::Command_ExtEnhancementOpenFileBlock, {Str("media/notes.txt"), Int(4)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementSetFileBlockCompression, {Int(3), Int(10)}));
    CHECK(App.Fails(S::Function_ExtEnhancementGetFileBlockCount, {Int(9)}));
    CHECK(App.Fails(S::Function_ExtEnhancementGetFileBlockExists, {Int(256)}));
}

TEST_CASE("FileBlockSystem preserves encrypted data descriptors when saving imported ZIP files")
{
    ScratchDirectory Scratch("opendbc-file-block-descriptor");
    const std::filesystem::path Path = Scratch.Path() / "archive.zip";
    std::filesystem::copy_file(
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/file_blocks/encrypted_descriptor.zip", Path);
    dbc::FileBlockSystem Blocks;
    std::string Error;
    std::vector<std::uint8_t> Bytes;
    REQUIRE_MESSAGE(Blocks.Open(1, Path.generic_string(), "opendbc-test", &Error), Error);
    REQUIRE_MESSAGE(Blocks.Read(1, "payload.txt", Bytes, &Error), Error);
    CHECK(std::string(Bytes.begin(), Bytes.end()) == "descriptor payload\n");
    REQUIRE_MESSAGE(Blocks.AddBytes(1, "added.txt", {'a'}, &Error), Error);

    SUBCASE("SAVE FILE BLOCK")
    {
        REQUIRE_MESSAGE(Blocks.Save(1, &Error), Error);
    }
    SUBCASE("GET FILE BLOCK SIZE")
    {
        int Size = 0;
        REQUIRE_MESSAGE(Blocks.Size(1, Size, &Error), Error);
        CHECK(Size > 0);
    }
    REQUIRE_MESSAGE(Blocks.Close(1, &Error), Error);
    REQUIRE_MESSAGE(Blocks.Open(1, Path.generic_string(), "opendbc-test", &Error), Error);
    REQUIRE_MESSAGE(Blocks.Read(1, "payload.txt", Bytes, &Error), Error);
    CHECK(std::string(Bytes.begin(), Bytes.end()) == "descriptor payload\n");
    REQUIRE_MESSAGE(Blocks.Read(1, "added.txt", Bytes, &Error), Error);
    CHECK(Bytes == std::vector<std::uint8_t>{'a'});
}

TEST_CASE("Enhancement Pack file blocks carry images, bitmaps, objects, sounds and animations")
{
    using S = SymbolId;
    ScratchDirectory Scratch("opendbc-file-block-media");
    Application App("enhancement-file-block-media");
    App.Context().AssetRoot = Scratch.Path();
    const auto Str = [](const char* Text) { return BuiltinValue::FromString(Text); };
    dbc::EngineKernel& Kernel = App.Kernel();
    std::string Error;
    REQUIRE(Kernel.Memblocks().MakeMemblock(1, 20, &Error));
    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 0, 2, &Error));
    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 4, 1, &Error));
    REQUIRE(Kernel.Memblocks().WriteMemblockDword(1, 8, 32, &Error));
    REQUIRE(Kernel.Memblocks().MakeImageFromMemblock(1, 1, &Error));
    REQUIRE(Kernel.ImageStoreRef().CreateBitmap(1, 4, 3, &Error));
    App.Require(SymbolId::Command_MakeObjectCube, {Int(1), BuiltinValue::FromNumber(10.0)});
    const std::filesystem::path Source(TEST_SOURCE_DIR);
    std::filesystem::copy_file(Source / "examples/dbpro/tutorials/memblocks/behold.wav", Scratch.Path() / "behold.wav");
    std::filesystem::copy_file(Source / "tests/fixtures/video/red_blue.mp4", Scratch.Path() / "red_blue.mp4");

    App.Require(S::Command_ExtEnhancementCreateFileBlock, {Int(1), Str("media.zip")});
    App.Require(S::Command_ExtEnhancementAddImageToBlock, {Int(1), Int(1), Str("image.bmp")});
    App.Require(S::Command_ExtEnhancementAddBitmapToBlock, {Int(1), Int(1), Str("bitmap.bmp")});
    App.Require(S::Command_ExtEnhancementAddObjectToBlock, {Int(1), Int(1), Str("object.dbo")});
    App.Require(S::Command_ExtEnhancementAddFileToBlock, {Int(1), Str("behold.wav")});
    App.Require(S::Command_ExtEnhancementAddFileToBlock, {Int(1), Str("red_blue.mp4")});
    CHECK(App.CommandFails(S::Command_ExtEnhancementAddImageToBlock, {Int(2), Int(1), Str("image.bmp")}));
    App.Require(S::Command_ExtEnhancementSaveFileBlock, {Int(1)});

    App.Require(S::Command_ExtEnhancementLoadImageFromBlock, {Int(1), Str("image.bmp"), Int(2)});
    CHECK(App.Number(S::Function_ImageExist, {Int(2)}) == 1.0);
    App.Require(S::Command_ExtEnhancementLoadBitmapFromBlock, {Int(1), Str("bitmap.bmp"), Int(2)});
    CHECK(App.Number(S::Function_BitmapExist, {Int(2)}) == 1.0);
    App.Require(S::Command_ExtEnhancementLoadObjectFromBlock, {Int(1), Str("object.dbo"), Int(2)});
    CHECK(App.Number(S::Function_ObjectExist, {Int(2)}) == 1.0);
    App.Require(S::Command_ExtEnhancementLoadSoundFromBlock, {Int(1), Str("behold.wav"), Int(2)});
    CHECK(App.Number(S::Function_SoundExist, {Int(2)}) == 1.0);
#if defined(__APPLE__) || defined(_WIN32)
    App.Require(S::Command_ExtEnhancementLoadAnimationFromBlock, {Int(1), Str("red_blue.mp4"), Int(1)});
    CHECK(App.Number(S::Function_AnimationHeight, {Int(1)}) == 48.0);
#endif
}

TEST_CASE("Enhancement Pack Ogg Vorbis encodes WAV files and plays them apart from sounds")
{
    using S = SymbolId;
    ScratchDirectory Scratch("opendbc-ogg-vorbis");
    std::filesystem::copy_file(std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/memblocks/behold.wav",
                               Scratch.Path() / "behold.wav");
    Application App("enhancement-ogg-vorbis");
    App.Context().AssetRoot = Scratch.Path();
    const auto Str = [](const char* Text) { return BuiltinValue::FromString(Text); };

    App.Require(S::Command_ExtEnhancementEncodeToOggVorbis, {Str("behold.wav"), Str("behold.ogg")});
    App.Require(S::Command_ExtEnhancementEncodeToOggVorbis,
                {Str("behold.wav"), Str("low.ogg"), BuiltinValue::FromNumber(0.0)});
    std::ifstream Encoded(Scratch.Path() / "behold.ogg", std::ios::binary);
    const std::string Contents(std::istreambuf_iterator<char>(Encoded), {});
    CHECK(Contents.rfind("OggS", 0) == 0);
    CHECK(Contents.find("vorbis") != std::string::npos);
    CHECK(std::filesystem::file_size(Scratch.Path() / "low.ogg") < Contents.size());
    CHECK(App.CommandFails(S::Command_ExtEnhancementEncodeToOggVorbis,
                           {Str("behold.wav"), Str("bad.ogg"), BuiltinValue::FromNumber(2.0)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementEncodeToOggVorbis, {Str("missing.wav"), Str("bad.ogg")}));

    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisExists, {Int(1)}) == 0.0);
    App.Require(S::Command_ExtEnhancementLoadOggVorbis, {Str("behold.ogg"), Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisExists, {Int(1)}) == 1.0);
    CHECK(App.Number(S::Function_SoundExist, {Int(1)}) == 0.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisState, {Int(1)}) == 0.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisVolume, {Int(1)}) == 0.0);
    App.Require(S::Command_ExtEnhancementLoopOggVorbis, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisState, {Int(1)}) == 1.0);
    App.Require(S::Command_ExtEnhancementPauseOggVorbis, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisState, {Int(1)}) == 2.0);
    App.Require(S::Command_ExtEnhancementResumeOggVorbis, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisState, {Int(1)}) == 1.0);
    App.Require(S::Command_ExtEnhancementSetOggVorbisVolume, {Int(1), Int(50)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisVolume, {Int(1)}) == -4800.0);
    CHECK(App.CommandFails(S::Command_ExtEnhancementSetOggVorbisVolume, {Int(1), Int(101)}));
    App.Require(S::Command_ExtEnhancementStopOggVorbis, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisState, {Int(1)}) == 0.0);
    App.Require(S::Command_ExtEnhancementPlayOggVorbis, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisState, {Int(1)}) == 1.0);
    CHECK(App.CommandFails(S::Command_ExtEnhancementLoadOggVorbis, {Str("low.ogg"), Int(1)}));
    App.Require(S::Command_ExtEnhancementDeleteOggVorbis, {Int(1)});
    CHECK(App.Number(S::Function_ExtEnhancementGetOggVorbisExists, {Int(1)}) == 0.0);

    CHECK(App.CommandFails(S::Command_ExtEnhancementLoadOggVorbis, {Str("behold.wav"), Int(2)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementLoadOggVorbis, {Str("behold.ogg"), Int(255)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementPlayOggVorbis, {Int(3)}));
    CHECK(App.Fails(S::Function_ExtEnhancementGetOggVorbisExists, {Int(0)}));
}

TEST_CASE("Enhancement Pack speech uses the platform synthesiser and stays silent headless")
{
    using S = SymbolId;
    Application App("enhancement-speech");
    REQUIRE(App.Kernel().Core().EnsureInitialized());
    REQUIRE(App.Kernel().Core().Headless());
    const auto Str = [](const char* Text) { return BuiltinValue::FromString(Text); };

    CHECK(App.CommandFails(S::Command_ExtEnhancementSetVoiceSpeed, {Int(11)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementSetVoiceVolume, {Int(-1)}));
    App.Require(S::Command_ExtEnhancementSetVoiceSpeed, {Int(-4)});
    App.Require(S::Command_ExtEnhancementSetVoiceVolume, {Int(70)});
    App.Require(S::Command_ExtEnhancementSendVoiceMessage, {Str("Hello")});
    App.Require(S::Command_ExtEnhancementSendVoiceMessageXml, {Str("The words <emph>dark basic</emph>")});

#if defined(SDL_PLATFORM_APPLE) || defined(SDL_PLATFORM_WINDOWS)
    CHECK(App.Number(S::Function_ExtEnhancementGetVoiceApi) == 1.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetVoiceSpeed) == -4.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetVoiceVolume) == 70.0);
    App.Require(S::Command_ExtEnhancementGetVoiceList);
    const std::vector<std::string> Voices = ChecklistNames(App.Context());
    CHECK(std::none_of(Voices.begin(), Voices.end(), [](const std::string& Name) { return Name.empty(); }));
    if (!Voices.empty())
        App.Require(S::Command_ExtEnhancementSetVoice, {Int(static_cast<int>(Voices.size()))});
    CHECK(App.CommandFails(S::Command_ExtEnhancementSetVoice, {Int(static_cast<int>(Voices.size()) + 1)}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementSetVoice, {Int(0)}));
#else
    CHECK(App.Number(S::Function_ExtEnhancementGetVoiceApi) == 0.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetVoiceSpeed) == -1.0);
    CHECK(App.Number(S::Function_ExtEnhancementGetVoiceVolume) == -1.0);
    CHECK(App.CommandFails(S::Command_ExtEnhancementGetVoiceList, {}));
    CHECK(App.CommandFails(S::Command_ExtEnhancementSetVoice, {Int(1)}));
#endif
}

TEST_CASE("Enhancement Pack file dialogs read Windows filters and answer headless runs as cancelled")
{
    using S = SymbolId;
    const std::vector<dbc::FileDialogFilter> Filters =
        dbc::ParseFileDialogFilters("Text files|*.txt;*.dat|All files|*.*|Broken|");
    REQUIRE(Filters.size() == 2);
    CHECK(Filters[0].Name == "Text files");
    CHECK(Filters[0].Pattern == "txt;dat");
    CHECK(Filters[1].Name == "All files");
    CHECK(Filters[1].Pattern == "*");
    const std::vector<dbc::FileDialogFilter> Bare = dbc::ParseFileDialogFilters("*.png; .jpg");
    REQUIRE(Bare.size() == 1);
    CHECK(Bare[0].Pattern == "png;jpg");
    CHECK(dbc::ParseFileDialogFilters("").empty());

    Application App("enhancement-file-dialogs");
    REQUIRE(App.Kernel().Core().EnsureInitialized());
    REQUIRE(App.Kernel().Core().Headless());
    const std::vector<BuiltinValue> Arguments = {BuiltinValue::FromString("media"),
                                                 BuiltinValue::FromString("Images|*.png"),
                                                 BuiltinValue::FromString("Choose an image")};
    CHECK(App.Text(S::Function_ExtEnhancementOpenFileDialog, Arguments).empty());
    CHECK(App.Text(S::Function_ExtEnhancementSaveFileDialog, Arguments).empty());
}
