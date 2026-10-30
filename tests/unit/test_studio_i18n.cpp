#include <doctest/doctest.h>

#include "core/i18n.hpp"
#include "core/locales.hpp"
#include "theme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace dbc::studio;

namespace
{
    // The active translations are global: every case leaves English behind.
    struct ActiveFrench
    {
        ActiveFrench()
        {
            SetActiveTranslations(&Table);
        }

        ~ActiveFrench()
        {
            SetActiveTranslations(nullptr);
        }

        TextTable Table{{"Output", "Sortie"},
                        {"Close", "Fermer"},
                        {"Exit", "Fermer"},
                        {"scene node|Light", "Lumière"},
                        {"Copied {0} to {1}", "{1} reçoit {0}"}};
    };

    // The {0} and %s of a text, sorted: a translation may reorder them, not lose or add one.
    std::vector<std::string> Placeholders(std::string_view Text)
    {
        std::vector<std::string> Found;
        for (std::size_t At = 0; At + 1 < Text.size(); ++At)
        {
            if (Text[At] == '%' && Text[At + 1] == 's')
                Found.emplace_back("%s");
            else if (Text[At] == '{' && At + 2 < Text.size() &&
                     std::isdigit(static_cast<unsigned char>(Text[At + 1])) && Text[At + 2] == '}')
                Found.emplace_back(Text.substr(At, 3));
        }
        std::sort(Found.begin(), Found.end());
        return Found;
    }

    std::string_view VisibleText(const char* Label)
    {
        return std::string_view(Label, static_cast<std::size_t>(ImGui::FindRenderedTextEnd(Label) - Label));
    }
} // namespace

TEST_CASE("Without translations the source text shows")
{
    const char* Source = "Output";
    CHECK(Tr(Source) == Source);
    CHECK(std::string_view(TrC("scene node|Light")) == "Light");
    CHECK(std::string_view(TrC("Light")) == "Light");
    CHECK(TrFmt("Copied {0} to {1}", {"a", "b"}) == "Copied a to b");
}

TEST_CASE("Translations replace the source text, and missing ones fall back to it")
{
    const ActiveFrench French;
    CHECK(std::string_view(Tr("Output")) == "Sortie");
    CHECK(std::string_view(TrC("scene node|Light")) == "Lumière");
    const char* Missing = "Not translated";
    CHECK(Tr(Missing) == Missing);
    CHECK(std::string_view(TrC("other|Not translated")) == "Not translated");
}

TEST_CASE("A label keeps its ImGui ID whatever the language")
{
    const ImGuiID English = ImHashStr(TrId("Output"));
    CHECK(VisibleText(TrId("Output")) == "Output");

    const ActiveFrench French;
    CHECK(VisibleText(TrId("Output")) == "Sortie");
    CHECK(ImHashStr(TrId("Output")) == English);
    CHECK(TrId("Output") == TrId("Output"));
    // Layouts saved before windows had translated titles keep working.
    CHECK(English == ImHashStr("Output"));
}

TEST_CASE("Labels translated alike do not collide")
{
    const ActiveFrench French;
    CHECK(VisibleText(TrId("Close")) == VisibleText(TrId("Exit")));
    CHECK(ImHashStr(TrId("Close")) != ImHashStr(TrId("Exit")));
}

TEST_CASE("A ## suffix stays out of the translation lookup and in the ID")
{
    const ActiveFrench French;
    CHECK(VisibleText(TrId("Output##Scene")) == "Sortie");
    CHECK(ImHashStr(TrId("Output##Scene")) != ImHashStr(TrId("Output")));
    CHECK(ImHashStr(TrId("Output##Scene")) == ImHashStr("Output##Scene"));
}

TEST_CASE("A label that names its own ID keeps it")
{
    const ActiveFrench French;
    CHECK(VisibleText(TrId("Size column to fit###SizeOne")) == "Size column to fit");
    CHECK(ImHashStr(TrId("Size column to fit###SizeOne")) == ImHashStr("###SizeOne"));
}

TEST_CASE("Formatted strings let the translation reorder the arguments")
{
    const ActiveFrench French;
    CHECK(TrFmt("Copied {0} to {1}", {"a", "b"}) == "b reçoit a");
    CHECK(TrFmt("{0} and {0}", {"x"}) == "x and x");
    CHECK(TrFmt("Line {0}", {std::to_string(12)}) == "Line 12");
}

TEST_CASE("Formatted strings leave anything but a valid placeholder alone")
{
    CHECK(TrFmt("{1} {x} {} { {0", {"a"}) == "{1} {x} {} { {0");
    CHECK(TrFmt("trailing {", {"a"}) == "trailing {");
    CHECK(TrFmt("{99999999999999999999}", {"a"}) == "{99999999999999999999}");
    CHECK(TrFmt("", {"a"}).empty());
}

TEST_CASE("A catalogue is a flat object with its own name")
{
    std::string Error;
    const auto Catalogue = core::ParseTranslationCatalogue(R"({"@name":"Français","Save":"Enregistrer"})", Error);
    CHECK(Error.empty());
    REQUIRE(Catalogue.has_value());
    CHECK(Catalogue->Name == "Français");
    CHECK(Catalogue->Entries.size() == 1);
    CHECK(Catalogue->Entries.at("Save") == "Enregistrer");
}

TEST_CASE("Invalid catalogues are refused without throwing")
{
    std::string Error;
    CHECK_FALSE(core::ParseTranslationCatalogue("{", Error).has_value());
    CHECK_FALSE(Error.empty());
    CHECK_FALSE(core::ParseTranslationCatalogue("[]", Error).has_value());
    CHECK_FALSE(Error.empty());
    CHECK_FALSE(core::ParseTranslationCatalogue(R"({"Save":"Enregistrer"})", Error).has_value());
    CHECK_FALSE(Error.empty());
}

TEST_CASE("A catalogue entry that is not a string is reported and ignored")
{
    std::string Error;
    const auto Catalogue = core::ParseTranslationCatalogue(R"({"@name":"Français","Save":1,"Open":"Ouvrir"})", Error);
    CHECK_FALSE(Error.empty());
    REQUIRE(Catalogue.has_value());
    CHECK(Catalogue->Entries.size() == 1);
    CHECK(Catalogue->Entries.contains("Open"));
}

TEST_CASE("The system's locales choose the language unless the setting names one")
{
    const auto Resolve = [](std::string_view Setting, const std::vector<std::string>& Locales)
    { return core::ResolveInterfaceLanguage(Setting, Locales); };

    CHECK(Resolve("auto", {"fr"}) == "fr");
    CHECK(Resolve("auto", {"fr-CA"}) == "fr");
    CHECK(Resolve("auto", {"fr_FR"}) == "fr");
    CHECK(Resolve("auto", {"FR"}) == "fr");
    CHECK(Resolve("auto", {"xx", "fr", "en"}) == "fr");
    CHECK(Resolve("auto", {"en-GB", "fr"}) == "en");
    CHECK(Resolve("auto", {"xx"}) == "en");
    CHECK(Resolve("auto", {}) == "en");
    CHECK(Resolve("fr", {"en"}) == "fr");
    CHECK(Resolve("en", {"fr"}) == "en");
    CHECK(Resolve("xx", {"fr"}) == "fr");
    CHECK(Resolve("", {}) == "en");
}

TEST_CASE("Every embedded catalogue is listed after English and can be activated")
{
    const std::vector<core::InterfaceLanguage>& Languages = core::InterfaceLanguages();
    REQUIRE(Languages.size() >= 2);
    CHECK(Languages.front().Code == "en");
    CHECK(Languages.front().Name == "English");
    const auto French = std::find_if(Languages.begin(), Languages.end(),
                                     [](const core::InterfaceLanguage& Language) { return Language.Code == "fr"; });
    REQUIRE(French != Languages.end());
    CHECK(French->Name == "Français");

    core::ActivateInterfaceLanguage("fr");
    CHECK(std::string_view(Tr("Language")) == "Langue");
    core::ActivateInterfaceLanguage("en");
    CHECK(std::string_view(Tr("Language")) == "Language");
    core::ActivateInterfaceLanguage("fr");
    core::ActivateInterfaceLanguage("xx");
    CHECK(std::string_view(Tr("Language")) == "Language");
}

TEST_CASE("The interface font keeps ImGui's default and covers French typography")
{
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    Io.DisplaySize = ImVec2(800.0f, 600.0f);
    Theme::AddFonts();
    ImGui::NewFrame();

    ImFont* Font = ImGui::GetFont();
    REQUIRE(Font->Sources.Size == 2);
    CHECK(std::string_view(Font->Sources[0]->Name).starts_with("Proggy"));
    for (const unsigned int Glyph : {0xE9u, 0xE7u, 0xC9u, 0x153u, 0x2019u, 0x2026u, 0x2014u, 0xA0u, 0x202Fu})
        CHECK(Font->IsGlyphInFont(static_cast<ImWchar>(Glyph)));

    ImGui::Render();
    ImGui::DestroyContext();
}

TEST_CASE("ImGui's own strings follow the language and keep their English wording")
{
    ImGui::CreateContext();
    std::vector<std::string> Defaults;
    for (int Key = 0; Key < ImGuiLocKey_COUNT; ++Key)
        Defaults.emplace_back(VisibleText(ImGui::LocalizeGetMsg(static_cast<ImGuiLocKey>(Key))));

    core::ActivateInterfaceLanguage("fr");
    core::TranslateImGuiStrings();
    const char* HideTabBar = ImGui::LocalizeGetMsg(ImGuiLocKey_DockingHideTabBar);
    CHECK(VisibleText(HideTabBar) == "Masquer la barre d’onglets");
    CHECK(ImHashStr(HideTabBar) == ImHashStr("###HideTabBar"));
    CHECK(std::string_view(ImGui::LocalizeGetMsg(ImGuiLocKey_WindowingUntitled)) == "(Sans titre)");
    CHECK(std::string_view(ImGui::LocalizeGetMsg(ImGuiLocKey_OpenLink_s)).find("%s") != std::string_view::npos);

    core::ActivateInterfaceLanguage("en");
    core::TranslateImGuiStrings();
    for (int Key = 0; Key < ImGuiLocKey_COUNT; ++Key)
        CHECK(VisibleText(ImGui::LocalizeGetMsg(static_cast<ImGuiLocKey>(Key))) ==
              Defaults[static_cast<std::size_t>(Key)]);
    ImGui::DestroyContext();
}

TEST_CASE("The French catalogue names every Studio window")
{
    core::ActivateInterfaceLanguage("fr");
    for (const char* Window :
         {"Project", "Editor", "Output", "Problems", "Preview", "Media", "Hierarchy", "Viewport", "Inspector",
          "Level Settings", "Assets", "Launcher", "Build", "Project Settings", "About OpenDBC"})
        CHECK(std::string_view(Tr(Window)) != Window);
    CHECK(std::string_view(Tr("Code")) == "Code");
    core::ActivateInterfaceLanguage("en");
}

TEST_CASE("The French catalogue covers the File menu and the Scene mode")
{
    core::ActivateInterfaceLanguage("fr");
    CHECK(std::string_view(Tr("New Level...")) == "Nouveau niveau…");
    CHECK(std::string_view(Tr("Scene (Ctrl+2)")) == "Scène (Ctrl+2)");
    CHECK(std::string_view(Tr("World Settings")) == "Réglages du monde");
    CHECK(TrFmt("Scene of {0}", {"main.dba"}) == "Scène de main.dba");
    // These reach Tr through a variable, so that a search of the sources for literals misses them.
    for (const char* Label :
         {"Sphere", "3D Sound", "Spot Light", "First matrix", "Move (W)", "Several", "Heights", "Fog colour",
          "The project folder is read-only.", "No model under the asset folder", " (hidden)"})
        CHECK(std::string_view(Tr(Label)) != Label);
    core::ActivateInterfaceLanguage("en");
}

TEST_CASE("The French catalogue covers the menus and the application's messages")
{
    core::ActivateInterfaceLanguage("fr");
    for (const char* Label : {"Edit", "Build", "View", "Help", "Find and Replace", "Reset Layout", "Stopped"})
        CHECK(std::string_view(Tr(Label)) != Label);
    CHECK(std::string_view(TrC("dialect origin|detected")) == "détecté");
    CHECK(TrFmt("Run failed (exit {0})", {"3"}) == "Échec de l’exécution (code 3)");
    core::ActivateInterfaceLanguage("en");
    CHECK(std::string_view(TrC("dialect origin|detected")) == "detected");
}

TEST_CASE("The French catalogue covers the launcher, the build window and the project settings")
{
    core::ActivateInterfaceLanguage("fr");
    for (const char* Label : {"Unrecognised statements", "Development (unsigned)", "macOS app", "Windows executable",
                              "Linux folder", "iOS (Xcode project)", "Missing", "My Projects", "Samples", "File System",
                              "Open any folder on this machine", "Screen orientation", "DBPro Advanced"})
        CHECK(std::string_view(Tr(Label)) != Label);
    CHECK(TrFmt("Line {0}: {1}", {"3", "x"}) == "Ligne 3 : x");
    core::ActivateInterfaceLanguage("en");
}

TEST_CASE("The French catalogue covers the code, media, output and snippet panels")
{
    core::ActivateInterfaceLanguage("fr");
    for (const char* Label : {"Filter by name", "Sort by name", "Auto-scroll", "No problems in the last run or check.",
                              "Replace All", "Not supported by OpenDBC: using it is a compile error.", "Insert Snippet",
                              "Lighting", "Save and Load", "Two edits touch the same text.", "Models"})
        CHECK(std::string_view(Tr(Label)) != Label);
    CHECK(TrFmt("{0} call(s)", {"3"}) == "3 appel(s)");
    CHECK(TrFmt("Saved in {0}", {"x"}) == "Enregistré dans x");
    core::ActivateInterfaceLanguage("en");
}

TEST_CASE("French translations keep the placeholders of their source text")
{
    std::ifstream File(std::filesystem::path(TEST_SOURCE_DIR) / "tools/studio/locales/fr.json", std::ios::binary);
    REQUIRE(File);
    std::stringstream Text;
    Text << File.rdbuf();

    std::string Error;
    const std::optional<core::TranslationCatalogue> Catalogue = core::ParseTranslationCatalogue(Text.str(), Error);
    CHECK(Error.empty());
    REQUIRE(Catalogue.has_value());
    for (const auto& [Source, Translation] : Catalogue->Entries)
    {
        INFO(Source);
        CHECK(Placeholders(Translation) == Placeholders(Source));
    }
}
