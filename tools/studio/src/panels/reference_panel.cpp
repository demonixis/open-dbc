#include "reference_panel.hpp"

#include "core/i18n.hpp"
#include "core/reference_archive.hpp"
#include "engine/systems/log.hpp"
#include "theme.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

#include <imgui.h>
#include <imhtml.hpp>
#include <nlohmann/json.hpp>

namespace dbc::studio::panels
{
    namespace
    {
        constexpr const char* KIndexPage = "index.html";

        // ImGui supplies search and the table of contents. The reference's web
        // stylesheet uses layout and control features unavailable in litehtml.
        constexpr std::string_view KViewerStyle = R"css(
body { margin: 0; color: var(--fg); font: 1rem/1.5 sans-serif; }
a { color: var(--accent); cursor: pointer; }
a:hover { color: var(--hover); }
h1, h2, h3, h4, p { margin: 0 0 0.5em; }
h1 { font-size: 1.8em; }
h2 { font-size: 1.2em; margin: 1em 0 0.5em; }
h3 { font-size: 1.15em; }
h4 { font-size: 0.9em; margin-top: 0.8em; }
code, pre, .chip { font-family: monospace; }
pre { white-space: pre-wrap; padding: 0.65em; margin: 0.4em 0 0.7em;
      background-color: var(--inset); border: 1px solid var(--line); }
.c-com { color: var(--comment); }
.c-str { color: var(--string); }
.c-num { color: var(--number); }
.c-kw { color: var(--accent); }
.layout, .cards, main, header, footer, details, summary { display: block; }
summary { font-size: 0.9em; color: var(--muted); margin: 0.4em 0; }
.sidebar, #search, #results { display: none; }
.page-head, .hero, .cmd, .card, .variant, .note, .diff-callout {
    padding: 0.8em; margin-bottom: 0.8em; background-color: var(--surface);
    border: 1px solid var(--line); border-radius: 6px;
}
.cmd { overflow: hidden; }
.card { display: block; color: var(--fg); text-decoration: none; }
.card h3, .crumbs { color: var(--accent); }
.card:hover { border-color: var(--accent); color: var(--fg); }
.crumbs { margin-bottom: 0.4em; }
.blurb, .counts, .meta, .typed, .source, .compat, .ex-path, .src-note, footer { color: var(--muted); }
.source, .meta, footer { font-size: 0.8em; }
.badge, .status, .chip, .ex-tag { display: inline-block; font-size: 0.8em; margin: 0 0.3em;
    padding: 0.1em 0.4em; background-color: var(--inset); border: 1px solid var(--line); border-radius: 4px; }
.badge.dbc, .badge.dbpro, .badge.new, .status.stub { color: var(--accent); }
.status.ok { color: var(--ok); }
.status.no, .diff-callout { border-color: var(--warning); }
.status.no { color: var(--warning); }
.status.blocked { color: var(--error); }
table.params { width: 100%; border-collapse: collapse; margin: 0.5em 0; }
table.params td, table.params th { border: 1px solid var(--line); padding: 0.25em 0.5em; text-align: left; }
table.params th { background-color: var(--inset); }
)css";

        bool IsSlug(std::string_view Text)
        {
            return !Text.empty() && std::ranges::all_of(Text,
                                                        [](char Character)
                                                        {
                                                            return (Character >= 'a' && Character <= 'z') ||
                                                                   (Character >= '0' && Character <= '9') ||
                                                                   Character == '-';
                                                        });
        }

        bool ContainsIgnoringCase(std::string_view Text, std::string_view Query)
        {
            return std::search(Text.begin(), Text.end(), Query.begin(), Query.end(),
                               [](char Left, char Right)
                               {
                                   return std::tolower(static_cast<unsigned char>(Left)) ==
                                          std::tolower(static_cast<unsigned char>(Right));
                               }) != Text.end();
        }

        std::string ViewerStyle()
        {
            std::string Result(KViewerStyle);
            Result += "\n:root {";
            const auto AddColor = [&Result](const char* Name, ImU32 Color)
            {
                char Hex[8]{};
                std::snprintf(Hex, sizeof(Hex), "#%02x%02x%02x", (Color >> IM_COL32_R_SHIFT) & 0xffu,
                              (Color >> IM_COL32_G_SHIFT) & 0xffu, (Color >> IM_COL32_B_SHIFT) & 0xffu);
                Result += std::string(Name) + ":" + Hex + ";";
            };
            AddColor("--surface", Theme::Panel);
            AddColor("--inset", Theme::PanelAlt);
            AddColor("--fg", Theme::Text);
            AddColor("--muted", Theme::TextDim);
            AddColor("--line", Theme::Border);
            AddColor("--accent", Theme::Keyword);
            AddColor("--hover", Theme::AccentHover);
            AddColor("--comment", Theme::Comment);
            AddColor("--string", Theme::String);
            AddColor("--number", Theme::Number);
            AddColor("--ok", Theme::Run);
            AddColor("--warning", Theme::Warning);
            AddColor("--error", Theme::Error);
            Result += "}\n";
            return Result;
        }
    } // namespace

    ReferencePanel::ReferencePanel() = default;

    ReferencePanel::~ReferencePanel() = default;

    void ReferencePanel::Open()
    {
        Visible_ = true;
        FocusRequested_ = true;
    }

    std::optional<ReferencePanel::Location> ReferencePanel::Resolve(std::string_view Url, std::string_view CurrentPage)
    {
        const std::size_t Hash = Url.find('#');
        std::string_view Page = Url.substr(0, Hash);
        if (Page.empty())
            Page = CurrentPage;
        const std::string_view Anchor = Hash == std::string_view::npos ? std::string_view{} : Url.substr(Hash + 1);
        if (!Page.ends_with(".html") || !IsSlug(Page.substr(0, Page.size() - 5)) ||
            (!Anchor.empty() && !IsSlug(Anchor)))
            return std::nullopt;

        return Location{std::string(Page), std::string(Anchor)};
    }

    void ReferencePanel::LoadIndex(const core::ReferenceArchive& Archive)
    {
        IndexLoaded_ = true;
        const auto Text = Archive.Read("search.json");
        const auto Index = Text ? nlohmann::json::parse(*Text, nullptr, false) : nlohmann::json{};
        if (!Index.is_array())
        {
            DBC_LOG_WARN("Cannot read the offline DBA reference search index");
            return;
        }

        for (const auto& Item : Index)
        {
            const auto Field = [&Item](const char* Key) -> std::string_view
            {
                const auto FieldValue = Item.find(Key);
                if (FieldValue == Item.end())
                    return {};
                const auto* Value = FieldValue->get_ptr<const nlohmann::json::string_t*>();
                return Value ? std::string_view(*Value) : std::string_view{};
            };
            const std::string_view Name = Field("s");
            const std::string_view Url = Field("u");
            const auto Destination = Resolve(Url, KIndexPage);
            if (!Name.empty() && !Url.empty() && Destination)
            {
                Entries_.push_back({*Destination, std::string(Name), std::string(Field("c")), std::string(Field("k")),
                                    std::string(Field("p"))});
            }
        }
    }

    void ReferencePanel::UpdateMatches()
    {
        std::string_view Query(Filter_.data());
        while (!Query.empty() && std::isspace(static_cast<unsigned char>(Query.front())))
            Query.remove_prefix(1);
        while (!Query.empty() && std::isspace(static_cast<unsigned char>(Query.back())))
            Query.remove_suffix(1);

        Matches_.clear();
        for (std::size_t Index = 0; Index < Entries_.size(); ++Index)
        {
            const auto& Entry = Entries_[Index];
            const bool Match =
                Query.empty() ? Entry.Destination.Page == Current_.Page : ContainsIgnoringCase(Entry.Name, Query);
            if (Match)
                Matches_.push_back(Index);
        }
    }

    void ReferencePanel::Navigate(const Location& Destination)
    {
        if (Destination != Current_)
        {
            History_.push_back({Current_, CurrentScrollY_});
            Current_ = Destination;
            RestoreScrollY_.reset();
            UpdateMatches();
        }

        ScrollRequested_ = true;
    }

    void ReferencePanel::DrawMatches()
    {
        const float Width = std::min(17.0f * ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x * 0.33f);
        std::optional<Location> Clicked;
        if (ImGui::BeginChild("##reference-results", ImVec2(Width, 0), ImGuiChildFlags_Borders))
        {
            if (Matches_.empty())
                ImGui::TextUnformatted(Tr("No results"));

            ImGuiListClipper Clipper;
            Clipper.Begin(static_cast<int>(Matches_.size()));
            while (Clipper.Step())
            {
                for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
                {
                    const auto& Entry = Entries_[Matches_[static_cast<std::size_t>(Row)]];
                    ImGui::PushID(Row);
                    if (ImGui::Selectable(Entry.Name.c_str(), Entry.Destination == Current_))
                        Clicked = Entry.Destination;
                    ImGui::SetItemTooltip("%s — %s — %s", Entry.Category.c_str(), Entry.Kind.c_str(),
                                          Entry.Profile.c_str());
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        if (Clicked)
            Navigate(*Clicked);
    }

    void ReferencePanel::DrawPage(const core::ReferenceArchive& Archive)
    {
        if (LoadedPage_ != Current_.Page)
        {
            LoadedPage_ = Current_.Page;
            Html_ = Archive.Read(LoadedPage_);
            Document_.reset();
            if (Html_)
            {
                // The bundled code font lacks the typed signature's right arrow.
                constexpr std::string_view Arrow = "&rarr;";
                for (std::size_t Position = Html_->find(Arrow); Position != std::string::npos;
                     Position = Html_->find(Arrow, Position + Arrow.size()))
                    Html_->replace(Position, Arrow.size(), "-&gt; ");
            }
            else
                DBC_LOG_WARN("Cannot read offline DBA reference page '{}'", LoadedPage_);
        }

        if (!Html_)
        {
            ImGui::TextUnformatted(Tr("Could not load the offline DBA reference."));
            return;
        }

        const float FontSize = ImGui::GetFontSize();
        if (!Document_ || FontSize_ != FontSize || ThemeTextColor_ != Theme::Text)
        {
            ImHTML::Config Config;
            Config.BaseFontSize = FontSize;
            Config.DefaultFont.Regular = ImGui::GetFont();
            Config.FontFamilies["monospace"].Regular = Theme::CodeFont;
            Config.LoadCSS = [Sheet = ViewerStyle()](const char* Url, const char*)
            { return std::strcmp(Url, "styles.css") == 0 ? Sheet : std::string{}; };
            Document_ = std::make_unique<ImHTML::Document>(Html_->c_str(), Config);
            ThemeTextColor_ = Theme::Text;
            FontSize_ = FontSize;
        }

        Document_->Layout(ImGui::GetContentRegionAvail());
        CurrentScrollY_ = ImGui::GetScrollY();
        if (ScrollRequested_)
        {
            if (RestoreScrollY_)
                ImGui::SetScrollY(*RestoreScrollY_);
            else if (Current_.Anchor.empty())
                ImGui::SetScrollY(0.0f);
            else
                ImGui::SetScrollY(Document_->AnchorY(Current_.Anchor.c_str()).value_or(0.0f));

            ImGui::SetScrollX(0.0f);
            ScrollRequested_ = false;
            RestoreScrollY_.reset();
        }

        const std::string Clicked = Document_->Draw();
        if (!Clicked.empty())
        {
            if (const auto Destination = Resolve(Clicked, Current_.Page))
                Navigate(*Destination);
        }
    }

    void ReferencePanel::Draw(const core::ReferenceArchive& Archive)
    {
        if (!Visible_)
            return;

        if (std::exchange(FocusRequested_, false))
            ImGui::SetNextWindowFocus();
        const float FontSize = ImGui::GetFontSize();
        const ImVec2 WorkSize = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(ImVec2(std::min(FontSize * 68.0f, WorkSize.x), std::min(FontSize * 44.0f, WorkSize.y)),
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin(TrId("DBA Reference"), &Visible_))
        {
            if (!IndexLoaded_)
            {
                LoadIndex(Archive);
                UpdateMatches();
            }

            ImGui::BeginDisabled(History_.empty());
            if (ImGui::Button(TrId("Back")))
            {
                auto Previous = std::move(History_.back());
                History_.pop_back();
                Current_ = std::move(Previous.Destination);
                RestoreScrollY_ = Previous.ScrollY;
                ScrollRequested_ = true;
                UpdateMatches();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TrId("Reference index")))
                Navigate(Location{KIndexPage, {}});
            const float ToolbarWidth = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
            if (ImGui::GetContentRegionAvail().x - ToolbarWidth >= 16.0f * FontSize)
                ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##reference-search", Tr("Search commands and examples"), Filter_.data(),
                                         Filter_.size()))
                UpdateMatches();

            if (!Matches_.empty() || Filter_.front() != '\0')
            {
                DrawMatches();
                ImGui::SameLine();
            }

            if (ImGui::BeginChild("##reference-page", ImVec2(0, 0), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar))
                DrawPage(Archive);
            ImGui::EndChild();
        }
        ImGui::End();
    }
} // namespace dbc::studio::panels
