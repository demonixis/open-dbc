#include "text_system.hpp"
#include "../engine_types.hpp"
#include "../noto_sans_regular_ttf.hpp"
#include "core_system.hpp"
#include "draw_system.hpp"
#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "log.hpp"
#include "path_utf8.hpp"
#include "sprite_system.hpp"
#include "text_state.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#ifdef _WIN32
#undef DrawText
#endif

namespace dbc
{
    // -----------------------------------------------------------------------
    // Anonymous-namespace helpers (string utilities, font alias tables)
    // -----------------------------------------------------------------------

    namespace
    {
        [[nodiscard]] std::string ToLower(std::string Value)
        {
            std::transform(Value.begin(), Value.end(), Value.begin(),
                           [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
            return Value;
        }

        [[nodiscard]] std::string CanonicalFontName(const std::string& Name)
        {
            std::string Out;
            Out.reserve(Name.size());
            for (char Ch : Name)
            {
                if (Ch == ' ' || Ch == '-' || Ch == '_')
                {
                    continue;
                }
                Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Ch))));
            }
            return Out;
        }

        [[nodiscard]] std::string BuildFontCacheKey(const std::string& SourceName, int Size, int Style)
        {
            return SourceName + "|" + std::to_string(std::max(1, Size)) + "|" + std::to_string(Style);
        }

        [[nodiscard]] std::string NormalizeFontRequestName(const std::string& Requested)
        {
            if (Requested.empty())
            {
                return "Arial";
            }
            return Requested;
        }

        [[nodiscard]] const std::vector<std::string>& AliasesForFont(const std::string& RequestedLower)
        {
            static const std::unordered_map<std::string, std::vector<std::string>> KAliases{
                {"arial", {"Arial", "Liberation Sans", "Noto Sans"}},
                {"timesnewroman", {"Times New Roman", "Liberation Serif", "Noto Serif"}},
                {"couriernew", {"Courier New", "Liberation Mono", "Noto Sans Mono"}},
            };
            static const std::vector<std::string> Empty;
            const auto It = KAliases.find(RequestedLower);
            return It == KAliases.end() ? Empty : It->second;
        }

        [[nodiscard]] bool SameFontRequest(const TextFontRequest& A, const TextFontRequest& B)
        {
            return A.Size == B.Size && A.Style == B.Style &&
                   CanonicalFontName(A.FontName) == CanonicalFontName(B.FontName);
        }

    } // namespace

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    TextSystem::TextSystem(EngineKernel& Kernel)
        : Kernel_(Kernel), ActiveTextRequest_(std::make_unique<TextFontRequest>()),
          ActiveTextResolved_(std::make_unique<TextFontResolved>()), TextAtlas_(std::make_unique<TextAtlas>())
    {
    }

    TextSystem::~TextSystem()
    {
        // Font cleanup is handled by DestroyTextSystem() called from Shutdown()
        // before the kernel is destroyed.
        DebugTextLines_.clear();
    }

    // -----------------------------------------------------------------------
    // DestroyTextSystem  (was destroy_text_system)
    // -----------------------------------------------------------------------

    void TextSystem::DestroyTextSystem()
    {
        ActiveTextFont_ = nullptr;

        for (auto& [Key, Font] : TextFontCache_)
        {
            (void)Key;
            if (Font)
            {
                TTF_CloseFont(Font);
            }
        }
        TextFontCache_.clear();
        SystemFontResolveCache_.clear();
        SystemFontCandidates_.clear();
        FontResolverScanned_ = false;
        *ActiveTextRequest_ = TextFontRequest{};
        *ActiveTextResolved_ = TextFontResolved{};

        if (TextAtlas_->Surface)
        {
            SDL_DestroySurface(TextAtlas_->Surface);
            TextAtlas_->Surface = nullptr;
        }
        if (TextAtlas_->GpuTexture && Kernel_.Gpu().GpuDevice())
        {
            SDL_ReleaseGPUTexture(Kernel_.Gpu().GpuDevice(), TextAtlas_->GpuTexture);
            TextAtlas_->GpuTexture = nullptr;
        }
        TextAtlas_->Glyphs.clear();
        TextAtlas_->NextX = 1;
        TextAtlas_->NextY = 1;
        TextAtlas_->RowH = 0;
        TextAtlas_->Dirty = false;

        if (TtfInitialized_)
        {
            TTF_Quit();
            TtfInitialized_ = false;
        }
    }

    // -----------------------------------------------------------------------
    // ScanSystemFontCandidates  (was anonymous scan_system_font_candidates)
    // -----------------------------------------------------------------------

    void TextSystem::ScanSystemFontCandidates()
    {
        if (FontResolverScanned_)
        {
            return;
        }

        std::vector<std::filesystem::path> Roots;
#if defined(DBC_MOBILE)
        // Sandboxed products always use the embedded Noto Sans fallback.
#elif defined(_WIN32)
        if (const char* WinDir = SDL_getenv("WINDIR"))
        {
            Roots.emplace_back(dbc::PathFromUtf8(WinDir) / "Fonts");
        }
        else
        {
            Roots.emplace_back("C:/Windows/Fonts");
        }
#elif defined(__APPLE__)
        Roots.emplace_back("/System/Library/Fonts");
        Roots.emplace_back("/Library/Fonts");
        if (const char* Home = SDL_getenv("HOME"))
        {
            Roots.emplace_back(dbc::PathFromUtf8(Home) / "Library/Fonts");
        }
#else
        Roots.emplace_back("/usr/share/fonts");
        if (const char* Home = SDL_getenv("HOME"))
        {
            Roots.emplace_back(dbc::PathFromUtf8(Home) / ".fonts");
            Roots.emplace_back(dbc::PathFromUtf8(Home) / ".local/share/fonts");
        }
#endif

        std::unordered_set<std::string> Seen;
        for (const auto& Root : Roots)
        {
            std::error_code Ec;
            if (!std::filesystem::exists(Root, Ec))
            {
                continue;
            }

            for (std::filesystem::recursive_directory_iterator
                     It(Root, std::filesystem::directory_options::skip_permission_denied, Ec),
                 End;
                 It != End; It.increment(Ec))
            {
                if (Ec)
                {
                    Ec.clear();
                    continue;
                }
                if (!It->is_regular_file(Ec))
                {
                    continue;
                }
                const std::string Ext = ToLower(dbc::PathToUtf8(It->path().extension()));
                if (Ext != ".ttf" && Ext != ".otf" && Ext != ".ttc")
                {
                    continue;
                }
                const std::string Path = dbc::PathToUtf8(It->path());
                if (Seen.insert(Path).second)
                {
                    SystemFontCandidates_.push_back(Path);
                }
            }
        }

        FontResolverScanned_ = true;
    }

    // -----------------------------------------------------------------------
    // ResolveSystemFontPath  (was anonymous resolve_system_font_path)
    // -----------------------------------------------------------------------

    std::string TextSystem::ResolveSystemFontPath(const std::string& RequestedName)
    {
        ScanSystemFontCandidates();

        const std::string Normalized = NormalizeFontRequestName(RequestedName);
        const std::string RequestedLower = CanonicalFontName(Normalized);

        const auto CacheIt = SystemFontResolveCache_.find(RequestedLower);
        if (CacheIt != SystemFontResolveCache_.end())
        {
            return CacheIt->second;
        }

        std::vector<std::string> Candidates;
        Candidates.push_back(Normalized);
        for (const std::string& Alias : AliasesForFont(RequestedLower))
        {
            Candidates.push_back(Alias);
        }

        for (const std::string& Candidate : Candidates)
        {
            const std::string CandidateKey = CanonicalFontName(Candidate);
            for (const std::string& Path : SystemFontCandidates_)
            {
                const std::string StemKey = CanonicalFontName(dbc::PathToUtf8(dbc::PathFromUtf8(Path).stem()));
                if (StemKey == CandidateKey)
                {
                    SystemFontResolveCache_[RequestedLower] = Path;
                    return Path;
                }
            }
        }

        for (const std::string& Candidate : Candidates)
        {
            const std::string CandidateKey = CanonicalFontName(Candidate);
            for (const std::string& Path : SystemFontCandidates_)
            {
                TTF_Font* Probe = TTF_OpenFont(Path.c_str(), 16.0f);
                if (!Probe)
                {
                    continue;
                }
                const char* Family = TTF_GetFontFamilyName(Probe);
                const std::string FamilyKey = CanonicalFontName(Family ? std::string(Family) : std::string{});
                TTF_CloseFont(Probe);
                if (FamilyKey == CandidateKey)
                {
                    SystemFontResolveCache_[RequestedLower] = Path;
                    return Path;
                }
            }
        }

        SystemFontResolveCache_[RequestedLower] = {};
        return {};
    }

    // -----------------------------------------------------------------------
    // EnsureTextAtlasSurface  (was anonymous ensure_text_atlas_surface)
    // -----------------------------------------------------------------------

    bool TextSystem::EnsureTextAtlasSurface()
    {
        if (TextAtlas_->Surface)
        {
            return true;
        }
        TextAtlas_->Surface = SDL_CreateSurface(TextAtlas_->Width, TextAtlas_->Height, SDL_PIXELFORMAT_RGBA32);
        if (!TextAtlas_->Surface)
        {
            DBC_LOG_ERROR("Text atlas surface creation failed: {}", SDL_GetError());
            return false;
        }
        SDL_FillSurfaceRect(TextAtlas_->Surface, nullptr, 0x00000000u);
        TextAtlas_->NextX = 1;
        TextAtlas_->NextY = 1;
        TextAtlas_->RowH = 0;
        TextAtlas_->Glyphs.clear();
        TextAtlas_->Dirty = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // EnsureActiveTextFont  (was ensure_active_text_font)
    // -----------------------------------------------------------------------

    bool TextSystem::EnsureActiveTextFont(const TextFontRequest& Request)
    {
        if (!Kernel_.Core().EnsureInitialized() || !TtfInitialized_)
        {
            return false;
        }

        if (ActiveTextFont_ && SameFontRequest(*ActiveTextRequest_, Request))
        {
            return true;
        }

        const std::string NormalizedName = NormalizeFontRequestName(Request.FontName);
        const std::string ResolvedPath = ResolveSystemFontPath(NormalizedName);
        const bool UseFallback = ResolvedPath.empty();
        const std::string SourceName =
            UseFallback ? std::string("embedded:NotoSans-Regular") : std::string("file:") + ResolvedPath;
        const std::string CacheKey = BuildFontCacheKey(SourceName, Request.Size, Request.Style);

        TTF_Font* Font = nullptr;
        const auto CacheIt = TextFontCache_.find(CacheKey);
        if (CacheIt != TextFontCache_.end())
        {
            Font = CacheIt->second;
        }
        else
        {
            if (UseFallback)
            {
                SDL_IOStream* Io = SDL_IOFromConstMem(third_party_fonts_NotoSans_Regular_ttf,
                                                      third_party_fonts_NotoSans_Regular_ttf_len);
                if (!Io)
                {
                    DBC_LOG_ERROR("fallback font IO creation failed: {}", SDL_GetError());
                    return false;
                }
                Font = TTF_OpenFontIO(Io, true, static_cast<float>(std::max(1, Request.Size)));
            }
            else
            {
                Font = TTF_OpenFont(ResolvedPath.c_str(), static_cast<float>(std::max(1, Request.Size)));
            }

            if (!Font)
            {
                DBC_LOG_ERROR("font open failed ({}): {}", SourceName, SDL_GetError());
                return false;
            }

            TTF_SetFontStyle(Font, static_cast<TTF_FontStyleFlags>(Request.Style));
            TTF_SetFontKerning(Font, true);
            TextFontCache_[CacheKey] = Font;
            if (UseFallback)
            {
                DBC_LOG_INFO("font fallback active for '{}': {}", NormalizedName, SourceName);
            }
        }

        ActiveTextFont_ = Font;
        *ActiveTextRequest_ = Request;
        ActiveTextRequest_->FontName = NormalizedName;
        ActiveTextResolved_->SourceName = SourceName;
        const char* Family = TTF_GetFontFamilyName(Font);
        ActiveTextResolved_->FamilyName = Family ? Family : "";
        ActiveTextResolved_->UsingFallback = UseFallback;
        return true;
    }

    // -----------------------------------------------------------------------
    // MeasureText  (was measure_text)
    // -----------------------------------------------------------------------

    bool TextSystem::MeasureText(const TextFontRequest& Request, const std::string& Value, int& OutW, int& OutH)
    {
        OutW = 0;
        OutH = 0;
        if (!EnsureActiveTextFont(Request))
        {
            return false;
        }

        const int LineSkip = std::max(1, TTF_GetFontLineSkip(ActiveTextFont_));
        if (Value.empty())
        {
            OutH = LineSkip;
            return true;
        }

        int MaxW = 0;
        int TotalH = 0;
        size_t Start = 0;
        while (Start <= Value.size())
        {
            const size_t End = Value.find('\n', Start);
            const std::string Line = Value.substr(Start, End == std::string::npos ? std::string::npos : End - Start);

            int LineW = 0;
            int LineH = 0;
            if (!Line.empty() && !TTF_GetStringSize(ActiveTextFont_, Line.c_str(), Line.size(), &LineW, &LineH))
            {
                return false;
            }

            MaxW = std::max(MaxW, LineW);
            TotalH += LineSkip;

            if (End == std::string::npos)
            {
                break;
            }
            Start = End + 1;
        }

        OutW = MaxW;
        OutH = std::max(LineSkip, TotalH);
        return true;
    }

    // -----------------------------------------------------------------------
    // EnsureTextGlyph  (was ensure_text_glyph)
    // -----------------------------------------------------------------------

    TextGlyph* TextSystem::EnsureTextGlyph(const TextFontRequest& Request, Uint32 Codepoint)
    {
        if (!EnsureActiveTextFont(Request) || !EnsureTextAtlasSurface())
        {
            return nullptr;
        }

        const std::string GlyphKey = BuildFontCacheKey(ActiveTextResolved_->SourceName, Request.Size, Request.Style) +
                                     "|" + std::to_string(Codepoint);
        auto Found = TextAtlas_->Glyphs.find(GlyphKey);
        if (Found != TextAtlas_->Glyphs.end())
        {
            return &Found->second;
        }

        SDL_Surface* GlyphSurface = TTF_RenderGlyph_Blended(ActiveTextFont_, Codepoint, SDL_Color{255, 255, 255, 255});
        if (!GlyphSurface)
        {
            return nullptr;
        }

        int Minx = 0, Maxx = 0, Miny = 0, Maxy = 0, Advance = GlyphSurface->w;
        if (!TTF_GetGlyphMetrics(ActiveTextFont_, Codepoint, &Minx, &Maxx, &Miny, &Maxy, &Advance))
        {
            Minx = 0;
            Miny = 0;
            Maxx = GlyphSurface->w;
            Maxy = GlyphSurface->h;
            Advance = GlyphSurface->w;
        }

        const int GlyphW = GlyphSurface->w;
        const int GlyphH = GlyphSurface->h;
        if (GlyphW <= 0 || GlyphH <= 0)
        {
            SDL_DestroySurface(GlyphSurface);
            auto [It, Inserted] = TextAtlas_->Glyphs.emplace(GlyphKey, TextGlyph{});
            (void)Inserted;
            It->second.Minx = Minx;
            It->second.Maxx = Maxx;
            It->second.Miny = Miny;
            It->second.Maxy = Maxy;
            It->second.Advance = Advance;
            return &It->second;
        }
        if (GlyphW + 2 >= TextAtlas_->Width || GlyphH + 2 >= TextAtlas_->Height)
        {
            SDL_DestroySurface(GlyphSurface);
            return nullptr;
        }

        if (TextAtlas_->NextX + GlyphW + 1 >= TextAtlas_->Width)
        {
            TextAtlas_->NextX = 1;
            TextAtlas_->NextY += TextAtlas_->RowH + 1;
            TextAtlas_->RowH = 0;
        }
        if (TextAtlas_->NextY + GlyphH + 1 >= TextAtlas_->Height)
        {
            SDL_DestroySurface(GlyphSurface);
            return nullptr;
        }

        SDL_Rect Dst{TextAtlas_->NextX, TextAtlas_->NextY, GlyphW, GlyphH};
        if (!SDL_BlitSurface(GlyphSurface, nullptr, TextAtlas_->Surface, &Dst))
        {
            SDL_DestroySurface(GlyphSurface);
            return nullptr;
        }

        TextGlyph Glyph{};
        Glyph.U0 = static_cast<float>(Dst.x) / static_cast<float>(TextAtlas_->Width);
        Glyph.V0 = static_cast<float>(Dst.y) / static_cast<float>(TextAtlas_->Height);
        Glyph.U1 = static_cast<float>(Dst.x + Dst.w) / static_cast<float>(TextAtlas_->Width);
        Glyph.V1 = static_cast<float>(Dst.y + Dst.h) / static_cast<float>(TextAtlas_->Height);
        Glyph.Width = GlyphW;
        Glyph.Height = GlyphH;
        Glyph.Minx = Minx;
        Glyph.Maxx = Maxx;
        Glyph.Miny = Miny;
        Glyph.Maxy = Maxy;
        Glyph.Advance = Advance;

        TextAtlas_->NextX += GlyphW + 1;
        TextAtlas_->RowH = std::max(TextAtlas_->RowH, GlyphH);
        TextAtlas_->Dirty = true;

        SDL_DestroySurface(GlyphSurface);
        auto [InsertedIt, Inserted] = TextAtlas_->Glyphs.emplace(GlyphKey, Glyph);
        (void)Inserted;
        return &InsertedIt->second;
    }

    // -----------------------------------------------------------------------
    // FindTextGlyph  (was anonymous find_text_glyph)
    // -----------------------------------------------------------------------

    TextGlyph* TextSystem::FindTextGlyph(const TextFontRequest& Request, Uint32 Codepoint)
    {
        if (!EnsureActiveTextFont(Request))
        {
            return nullptr;
        }
        const std::string GlyphKey = BuildFontCacheKey(ActiveTextResolved_->SourceName, Request.Size, Request.Style) +
                                     "|" + std::to_string(Codepoint);
        auto It = TextAtlas_->Glyphs.find(GlyphKey);
        return It == TextAtlas_->Glyphs.end() ? nullptr : &It->second;
    }

    // -----------------------------------------------------------------------
    // RefreshTextAtlasGpu  (was refresh_text_atlas_gpu)
    // -----------------------------------------------------------------------

    void TextSystem::RefreshTextAtlasGpu()
    {
        if (!TextAtlas_->Dirty || !TextAtlas_->Surface)
        {
            return;
        }
        if (TextAtlas_->GpuTexture && Kernel_.Gpu().GpuDevice())
        {
            SDL_ReleaseGPUTexture(Kernel_.Gpu().GpuDevice(), TextAtlas_->GpuTexture);
            TextAtlas_->GpuTexture = nullptr;
        }
        TextAtlas_->GpuTexture = Kernel_.Gpu().CreateGpuTextureFromSurface(TextAtlas_->Surface);
        TextAtlas_->Dirty = false;
    }

    // -----------------------------------------------------------------------
    // Bitmap and transient text rendering
    // -----------------------------------------------------------------------

    bool TextSystem::DrawText(int X, int Y, const std::string& Value, std::string* Error)
    {
        if (Value.empty())
        {
            return true;
        }

        const int BmpId = Kernel_.ImageStoreRef().CurrentBitmap();
        if (BmpId == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        const bool PersistentScreenText =
            BmpId == 0 && Kernel_.RuntimeCompatibility().ScreenText() == ScreenTextLifetime::PersistentBitmap;
        SDL_Surface* BmpSurface = BmpId > 0              ? Kernel_.ImageStoreRef().GetBitmapSurface(BmpId)
                                  : PersistentScreenText ? Kernel_.ImageStoreRef().EnsureScreenBitmap(Error)
                                                         : nullptr;
        if ((BmpId > 0 || PersistentScreenText) && !BmpSurface)
        {
            if (Error && BmpId > 0)
                *Error = "TEXT requires a valid current bitmap";
            return false;
        }
        if (BmpSurface)
        {
            TextFontRequest Request;
            Request.FontName = State().Font;
            Request.Size = std::max(1, State().Size);
            const int Style = State().Style;
            Request.Style = Style == 1   ? TTF_STYLE_BOLD
                            : Style == 2 ? TTF_STYLE_ITALIC
                            : Style == 3 ? (TTF_STYLE_BOLD | TTF_STYLE_ITALIC)
                                         : TTF_STYLE_NORMAL;
            if (!EnsureActiveTextFont(Request))
            {
                if (Error)
                    *Error = "TEXT could not initialize the requested font";
                return false;
            }
            {
                const SDL_Color Fg{static_cast<Uint8>((InkForeColor_ >> 16) & 0xFF),
                                   static_cast<Uint8>((InkForeColor_ >> 8) & 0xFF),
                                   static_cast<Uint8>(InkForeColor_ & 0xFF), 255};
                SDL_Surface* TextSurface = TTF_RenderText_Blended(ActiveTextFont_, Value.c_str(), Value.size(), Fg);
                if (!TextSurface)
                {
                    if (Error)
                        *Error = std::string("TEXT rasterization failed: ") + SDL_GetError();
                    return false;
                }
                {
                    SDL_Rect DstRect{X, Y, TextSurface->w, TextSurface->h};
                    if (State().Opaque)
                    {
                        const int Backdrop = Kernel_.Draw().BackdropColor();
                        SDL_FillSurfaceRect(BmpSurface, &DstRect,
                                            SDL_MapSurfaceRGBA(BmpSurface, static_cast<Uint8>((Backdrop >> 16) & 0xFF),
                                                               static_cast<Uint8>((Backdrop >> 8) & 0xFF),
                                                               static_cast<Uint8>(Backdrop & 0xFF), 255));
                    }
                    const bool Drawn = SDL_BlitSurface(TextSurface, nullptr, BmpSurface, &DstRect);
                    SDL_DestroySurface(TextSurface);
                    if (!Drawn)
                    {
                        if (Error)
                            *Error = std::string("TEXT bitmap copy failed: ") + SDL_GetError();
                        return false;
                    }
                    Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(BmpId);
                }
            }
            if (BmpId > 0)
                return true;
        }

        QueueTextLine(DebugTextLines_, X, Y, Value);
        return true;
    }

    void TextSystem::DrawOverlayText(int X, int Y, const std::string& Value)
    {
        if (!Value.empty())
        {
            QueueTextLine(OverlayTextLines_, X, Y, Value);
        }
    }

    void TextSystem::QueueTextLine(std::vector<DebugTextLine>& Lines, int X, int Y, const std::string& Value)
    {
        DebugTextLine Line;
        Line.X = static_cast<float>(X);
        Line.Y = static_cast<float>(Y);
        Line.Value = Value;
        Line.Color = InkForeColor_;
        Line.Request.FontName = State().Font;
        Line.Request.Size = std::max(1, State().Size);
        const int Style = State().Style;
        Line.Request.Style = Style == 1   ? TTF_STYLE_BOLD
                             : Style == 2 ? TTF_STYLE_ITALIC
                             : Style == 3 ? (TTF_STYLE_BOLD | TTF_STYLE_ITALIC)
                                          : TTF_STYLE_NORMAL;
        Line.Request.Opaque = State().Opaque;
        Lines.push_back(std::move(Line));
    }

    // -----------------------------------------------------------------------
    // TextWidthPx
    // -----------------------------------------------------------------------

    int TextSystem::TextWidthPx(const std::string& Value)
    {
        TextFontRequest Request;
        Request.FontName = State().Font;
        Request.Size = std::max(1, State().Size);
        const int Style = State().Style;
        Request.Style = Style == 1   ? TTF_STYLE_BOLD
                        : Style == 2 ? TTF_STYLE_ITALIC
                        : Style == 3 ? (TTF_STYLE_BOLD | TTF_STYLE_ITALIC)
                                     : TTF_STYLE_NORMAL;
        Request.Opaque = State().Opaque;
        int OutW = 0;
        int OutH = 0;
        if (!MeasureText(Request, Value, OutW, OutH))
        {
            return static_cast<int>(Value.size()) * 8;
        }
        return OutW;
    }

    // -----------------------------------------------------------------------
    // TextHeightPx
    // -----------------------------------------------------------------------

    int TextSystem::TextHeightPx(const std::string& Value)
    {
        TextFontRequest Request;
        Request.FontName = State().Font;
        Request.Size = std::max(1, State().Size);
        const int Style = State().Style;
        Request.Style = Style == 1   ? TTF_STYLE_BOLD
                        : Style == 2 ? TTF_STYLE_ITALIC
                        : Style == 3 ? (TTF_STYLE_BOLD | TTF_STYLE_ITALIC)
                                     : TTF_STYLE_NORMAL;
        Request.Opaque = State().Opaque;
        int OutW = 0;
        int OutH = 0;
        if (!MeasureText(Request, Value.empty() ? " " : Value, OutW, OutH))
        {
            return std::max(1, Request.Size);
        }
        return OutH;
    }

    std::vector<std::string> TextSystem::AvailableFontNames()
    {
        if (!Kernel_.Core().EnsureInitialized())
            return {};
        ScanSystemFontCandidates();
        std::unordered_set<std::string> Seen;
        std::vector<std::string> Names;
        for (const std::string& Path : SystemFontCandidates_)
        {
            TTF_Font* Font = TTF_OpenFont(Path.c_str(), 16.0f);
            if (!Font)
                continue;
            const char* Family = TTF_GetFontFamilyName(Font);
            if (Family && *Family && Seen.insert(Family).second)
                Names.emplace_back(Family);
            TTF_CloseFont(Font);
        }
        if (Names.empty())
            Names.emplace_back("Noto Sans");
        std::sort(Names.begin(), Names.end());
        return Names;
    }

    // === Mutable accessors (transitional) ===

    bool& TextSystem::TtfInitialized()
    {
        return TtfInitialized_;
    }
    bool& TextSystem::FontResolverScanned()
    {
        return FontResolverScanned_;
    }
    TTF_Font*& TextSystem::ActiveTextFont()
    {
        return ActiveTextFont_;
    }
    BasicTextState& TextSystem::State()
    {
        return State_;
    }
    const BasicTextState& TextSystem::State() const
    {
        return State_;
    }

    TextFontRequest& TextSystem::ActiveTextRequest()
    {
        return *ActiveTextRequest_;
    }

    TextFontResolved& TextSystem::ActiveTextResolved()
    {
        return *ActiveTextResolved_;
    }

    std::unordered_map<std::string, TTF_Font*>& TextSystem::TextFontCache()
    {
        return TextFontCache_;
    }

    std::unordered_map<std::string, std::string>& TextSystem::SystemFontResolveCache()
    {
        return SystemFontResolveCache_;
    }

    std::vector<std::string>& TextSystem::SystemFontCandidates()
    {
        return SystemFontCandidates_;
    }

    TextAtlas& TextSystem::Atlas()
    {
        return *TextAtlas_;
    }

    void TextSystem::ClearDebugTextLines()
    {
        DebugTextLines_.clear();
    }
    void TextSystem::ClearOverlayTextLines()
    {
        OverlayTextLines_.clear();
    }
    size_t TextSystem::OverlayTextLineCount() const
    {
        return OverlayTextLines_.size();
    }

    int& TextSystem::InkForeColor()
    {
        return InkForeColor_;
    }
    bool& TextSystem::ClsRequested()
    {
        return ClsRequested_;
    }
    int& TextSystem::ClsColor()
    {
        return ClsColor_;
    }

} // namespace dbc
