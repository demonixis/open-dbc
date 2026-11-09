#include <doctest/doctest.h>

#include <imgui.h>
#include <imhtml.hpp>

#include <memory>
#include <optional>
#include <string>

namespace
{
    class ReferenceCanvas
    {
      public:
        ReferenceCanvas()
        {
            ImGui::CreateContext();
            ImGuiIO& Io = ImGui::GetIO();
            Io.IniFilename = nullptr;
            Io.DisplaySize = ImVec2(1000.0f, 800.0f);
            Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
            ImHTML::RegisterCustomElement("reference-probe", [this](const ImRect&, const auto&) { Painted_ = true; });
        }

        ~ReferenceCanvas()
        {
            Document_.reset();
            ImHTML::UnregisterCustomElement("reference-probe");
            ImGui::DestroyContext();
        }

        void Draw(float Width = 700.0f, std::optional<float> ScrollTo = std::nullopt)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f));
            ImGui::SetNextWindowSize(ImVec2(Width, 600.0f));
            ImGui::Begin("Reference");
            ImGui::BeginChild("Canvas", ImVec2(0, 0), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if (!Document_)
            {
                constexpr const char* KHtml = R"(
<style>body{margin:0} article{display:block;overflow:hidden} reference-probe{height:30px}</style>
<a href="#target">Jump</a><div style="height:2400px"></div>
<p>The position of the next heading changes as this text wraps onto more lines.</p>
<article id="target"><h2>Target</h2><reference-probe></reference-probe></article>
<div style="height:1200px"></div>)";
                ImHTML::Config Config;
                Config.DefaultFont.Regular = ImGui::GetFont();
                Document_ = std::make_unique<ImHTML::Document>(KHtml, Config);
            }

            Document_->Layout(ImGui::GetContentRegionAvail());
            Origin_ = ImGui::GetCursorScreenPos();
            ScrollY_ = ImGui::GetScrollY();
            if (ScrollTo)
                ImGui::SetScrollY(*ScrollTo);
            Painted_ = false;
            Clicked_ = Document_->Draw();
            ImGui::EndChild();
            ImGui::End();
            ImGui::Render();
        }

        std::unique_ptr<ImHTML::Document> Document_;
        ImVec2 Origin_{};
        float ScrollY_ = 0.0f;
        bool Painted_ = false;
        std::string Clicked_;
    };
} // namespace

TEST_CASE("The Studio reference follows internal anchors and paints the visible article")
{
    ReferenceCanvas Canvas;
    Canvas.Draw();
    Canvas.Draw();
    const auto Target = Canvas.Document_->AnchorY("target");
    REQUIRE(Target);
    CHECK(*Target > 2400.0f);
    CHECK_FALSE(Canvas.Document_->AnchorY("missing"));
    CHECK_FALSE(Canvas.Painted_);

    ImGuiIO& Io = ImGui::GetIO();
    Io.AddMousePosEvent(Canvas.Origin_.x + 5.0f, Canvas.Origin_.y + 8.0f);
    Io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Canvas.Draw();
    Io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Canvas.Draw();
    CHECK(Canvas.Clicked_ == "#target");

    Canvas.Draw(700.0f, Target);
    Canvas.Draw();
    CHECK(Canvas.ScrollY_ == doctest::Approx(*Target));
    CHECK(Canvas.Painted_);

    Canvas.Draw(360.0f);
    const auto NarrowTarget = Canvas.Document_->AnchorY("target");
    REQUIRE(NarrowTarget);
    CHECK(*NarrowTarget > *Target);
}
