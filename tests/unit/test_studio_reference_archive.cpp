#include "core/reference_archive.hpp"
#include "path_utf8.hpp"
#include "unique_suffix.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    namespace fs = std::filesystem;
    using dbc::studio::core::ReferenceArchive;

    std::string ReadFile(const fs::path& File)
    {
        std::ifstream Input(File, std::ios::binary);
        REQUIRE(Input);
        return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
    }

    std::string ArchiveBytes()
    {
        return ReadFile(dbc::PathFromUtf8(OPENDBC_REFERENCE_ARCHIVE_PATH));
    }

    struct TemporaryFolder
    {
        fs::path Root =
            fs::temp_directory_path() / ("opendbc-reference-" + std::to_string(dbc::testing::UniqueSuffix()));

        ~TemporaryFolder()
        {
            std::error_code Error;
            fs::remove_all(Root, Error);
        }
    };
} // namespace

TEST_CASE("Studio reference archive loads the generated category pages and search index")
{
    const std::string Bytes = ArchiveBytes();
    ReferenceArchive Archive;
    REQUIRE(Archive.Open(Bytes));
    CHECK(Archive.IsOpen());
    const auto Basics = Archive.Read("basics.html");
    REQUIRE(Basics);
    CHECK(Basics->find("id=\"for\"") != std::string::npos);
    const auto Search = Archive.Read("search.json");
    const auto Index = Archive.Read("index.html");
    REQUIRE(Search);
    REQUIRE(Index);
    CHECK(Search->find("basics.html#for") != std::string::npos);
    CHECK(Index->find("const INDEX = " + *Search + ';') != std::string::npos);
    CHECK_FALSE(Archive.Read("../basics.html"));
    CHECK_FALSE(Archive.Read("missing.html"));
}

TEST_CASE("Studio reference archive rejects malformed entries and corrupt compressed data")
{
    const std::string Bytes = ArchiveBytes();
    std::string Corrupt = Bytes;
    Corrupt.back() ^= 1;
    std::string Unsafe = Bytes;
    const std::size_t Position = Unsafe.find("basics.html");
    REQUIRE(Position != std::string::npos);
    Unsafe.replace(Position, std::string_view("basics.html").size(), "../foo.html");

    ReferenceArchive Archive;
    REQUIRE(Archive.Open(Bytes));
    CHECK_FALSE(Archive.Open(std::string_view(Bytes).substr(0, Bytes.size() - 1)));
    CHECK_FALSE(Archive.IsOpen());

    REQUIRE(Archive.Open(Corrupt));
    CHECK_FALSE(Archive.Read("search.json"));
    CHECK(Archive.Read("basics.html"));

    CHECK_FALSE(Archive.Open(Unsafe));
    CHECK_FALSE(Archive.IsOpen());
}

TEST_CASE("Studio reference archive materializes local browser files and repairs an incomplete cache")
{
    const std::string Bytes = ArchiveBytes();
    ReferenceArchive Archive;
    REQUIRE(Archive.Open(Bytes));
    const TemporaryFolder Temporary;
    const fs::path Cache = Temporary.Root / "browser help";
    CHECK_FALSE(Archive.Materialize("relative"));
    const auto Index = Archive.Materialize(Cache);
    REQUIRE(Index);
    CHECK(Index->filename() == "index.html");
    const fs::path Directory = Index->parent_path();
    CHECK(Directory.parent_path() == Cache);
    CHECK(ReadFile(*Index) == Archive.Read("index.html"));
    CHECK(ReadFile(Directory / "styles.css") == Archive.Read("styles.css"));
    CHECK_FALSE(fs::exists(Directory / "search.json"));

    const fs::path Basics = Directory / "basics.html";
    REQUIRE(fs::remove(Basics));
    REQUIRE(fs::create_directory(Basics));
    CHECK_FALSE(Archive.Materialize(Cache));
    REQUIRE(fs::remove(Basics));
    CHECK(Archive.Materialize(Cache) == Index);
    CHECK(ReadFile(Basics) == Archive.Read("basics.html"));
}
