#include <doctest/doctest.h>

#include "core/asset_library.hpp"

#include <fstream>

using namespace dbc::studio;

namespace
{
    namespace fs = std::filesystem;

    fs::path FreshFolder(const char* Name)
    {
        const fs::path Folder = fs::temp_directory_path() / Name;
        std::error_code Error;
        fs::remove_all(Folder, Error);
        fs::create_directories(Folder);
        return Folder;
    }
} // namespace

TEST_CASE("the texture library lists textures by name and the six faces of a cube map as one")
{
    const fs::path Library = FreshFolder("opendbc-library-scan");
    for (const char* Name : {"grass_2.jpg", "grass_1.jpg", "rock_1.PNG", "notes.txt", "day_cube_px.jpg",
                             "day_cube_nx.jpg", "day_cube_py.jpg", "day_cube_ny.jpg", "day_cube_pz.jpg",
                             "day_cube_nz.jpg", "ice_cube_tray.jpg", "grass_1_normal.jpg", "lone_normal.jpg"})
        std::ofstream(Library / Name) << Name;

    const std::vector<core::LibraryTexture> Textures = core::ScanTextureLibrary(Library);
    REQUIRE(Textures.size() == 6);
    CHECK(Textures[0].Name == "day_cube");
    CHECK(Textures[0].CubeMap);
    CHECK(Textures[0].Files.size() == 6);
    CHECK(Textures[0].Preview.filename() == "day_cube_pz.jpg");
    CHECK(Textures[1].Name == "grass_1");
    CHECK(Textures[1].Category == "grass");
    CHECK_FALSE(Textures[1].CubeMap);
    // A texture comes with its normal map, and shows itself.
    CHECK(Textures[1].NormalMap);
    CHECK(Textures[1].Files.size() == 2);
    CHECK(Textures[1].Preview.filename() == "grass_1.jpg");
    CHECK(Textures[2].Name == "grass_2");
    CHECK_FALSE(Textures[2].NormalMap);
    // A name that only looks like a face is a texture of its own.
    CHECK(Textures[3].Name == "ice_cube_tray");
    CHECK(Textures[3].Category == "ice_cube_tray");
    // So is a normal map without its texture.
    CHECK(Textures[4].Name == "lone_normal");
    CHECK(Textures[5].Name == "rock_1");

    CHECK(core::ScanTextureLibrary(Library / "missing").empty());
    std::error_code Error;
    fs::remove_all(Library, Error);
}

TEST_CASE("adding a library texture copies its files to the project's textures and keeps the user's own")
{
    const fs::path Library = FreshFolder("opendbc-library-add");
    const fs::path Project = FreshFolder("opendbc-library-project");
    std::ofstream(Library / "grass_1.jpg") << "library";
    for (const char* Face : {"px", "nx", "py", "ny", "pz", "nz"})
        std::ofstream(Library / (std::string("day_cube_") + Face + ".jpg")) << Face;
    const std::vector<core::LibraryTexture> Textures = core::ScanTextureLibrary(Library);
    REQUIRE(Textures.size() == 2);

    std::string Error;
    CHECK_FALSE(core::HasLibraryTexture(Textures[1], Project));
    REQUIRE(core::AddLibraryTexture(Textures[1], Project, Error));
    CHECK(core::HasLibraryTexture(Textures[1], Project));
    CHECK(fs::exists(Project / "textures" / "grass_1.jpg"));

    REQUIRE(core::AddLibraryTexture(Textures[0], Project, Error));
    CHECK(fs::exists(Project / "textures" / "day_cube_nz.jpg"));

    // A file of that name the user changed is not overwritten.
    std::ofstream(Project / "textures" / "grass_1.jpg") << "mine";
    REQUIRE(core::AddLibraryTexture(Textures[1], Project, Error));
    std::string Kept;
    std::ifstream(Project / "textures" / "grass_1.jpg") >> Kept;
    CHECK(Kept == "mine");

    std::error_code Cleanup;
    fs::remove_all(Library, Cleanup);
    fs::remove_all(Project, Cleanup);
}
