#include <doctest/doctest.h>

#include "core/i18n.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <string_view>

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
