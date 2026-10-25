#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_stdinc.h>

#include "../engine_types.hpp"
#include "text_state.hpp"

// Windows headers define DrawText as a macro; undef to avoid renaming our method
#ifdef _WIN32
#undef DrawText
#endif

struct TTF_Font;

namespace dbc
{
    class EngineKernel;

    class TextSystem
    {
      public:
        explicit TextSystem(EngineKernel& Kernel);
        ~TextSystem();

        void DestroyTextSystem();
        bool EnsureActiveTextFont(const TextFontRequest& Request);
        bool MeasureText(const TextFontRequest& Request, const std::string& Value, int& OutW, int& OutH);
        TextGlyph* EnsureTextGlyph(const TextFontRequest& Request, Uint32 Codepoint);
        void RefreshTextAtlasGpu();

        [[nodiscard]] TextGlyph* FindTextGlyph(const TextFontRequest& Request, Uint32 Codepoint);

        // Offscreen bitmap text is immediate. Screen text follows the active compatibility policy.
        bool DrawText(int X, int Y, const std::string& Value, std::string* Error = nullptr);
        void DrawOverlayText(int X, int Y, const std::string& Value);
        int TextWidthPx(const std::string& Value);
        int TextHeightPx(const std::string& Value);
        std::vector<std::string> AvailableFontNames();
        BasicTextState& State();
        const BasicTextState& State() const;

        // Mutable access (transitional for renderer.cpp, resource_systems.cpp)
        bool& TtfInitialized();
        bool& FontResolverScanned();
        TTF_Font*& ActiveTextFont();
        TextFontRequest& ActiveTextRequest();
        TextFontResolved& ActiveTextResolved();
        std::unordered_map<std::string, TTF_Font*>& TextFontCache();
        std::unordered_map<std::string, std::string>& SystemFontResolveCache();
        std::vector<std::string>& SystemFontCandidates();
        TextAtlas& Atlas();

        template <typename Fn> void ForEachDebugTextLine(Fn&& FnValue) const
        {
            for (const auto& Line : DebugTextLines_)
                FnValue(Line);
            for (const auto& Line : OverlayTextLines_)
                FnValue(Line);
        }

        void ClearDebugTextLines();
        void ClearOverlayTextLines();
        [[nodiscard]] size_t OverlayTextLineCount() const;

        // Drawing state (shared with 2D primitives)
        int& InkForeColor();
        bool& ClsRequested();
        int& ClsColor();

      private:
        // --- Private font resolution helpers ---

        void ScanSystemFontCandidates();
        [[nodiscard]] std::string ResolveSystemFontPath(const std::string& RequestedName);
        [[nodiscard]] bool EnsureTextAtlasSurface();
        void QueueTextLine(std::vector<DebugTextLine>& Lines, int X, int Y, const std::string& Value);

        EngineKernel& Kernel_;
        bool TtfInitialized_ = false;
        bool FontResolverScanned_ = false;
        TTF_Font* ActiveTextFont_ = nullptr;
        std::unique_ptr<TextFontRequest> ActiveTextRequest_;
        std::unique_ptr<TextFontResolved> ActiveTextResolved_;
        std::unordered_map<std::string, TTF_Font*> TextFontCache_;
        std::unordered_map<std::string, std::string> SystemFontResolveCache_;
        std::vector<std::string> SystemFontCandidates_;
        std::unique_ptr<TextAtlas> TextAtlas_;
        std::vector<DebugTextLine> DebugTextLines_;
        std::vector<DebugTextLine> OverlayTextLines_;
        BasicTextState State_{};
        int InkForeColor_ = 0x00FFFFFF;
        bool ClsRequested_ = false;
        int ClsColor_ = 0;
    };

} // namespace dbc
