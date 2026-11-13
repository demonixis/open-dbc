#include "core_system.hpp"
#include "draw_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "input_system.hpp"
#include "renderer_internal.hpp"
#include "sprite_system.hpp"
#include "text_system.hpp"

#include <algorithm>
#include <cmath>

namespace dbc
{
    namespace
    {
        [[nodiscard]] Uint32 DecodeUtf8Codepoint(const std::string& Text, size_t& Index)
        {
            if (Index >= Text.size())
            {
                return 0;
            }
            const unsigned char C0 = static_cast<unsigned char>(Text[Index++]);
            if ((C0 & 0x80u) == 0)
            {
                return static_cast<Uint32>(C0);
            }
            if ((C0 & 0xE0u) == 0xC0u && Index < Text.size())
            {
                const unsigned char C1 = static_cast<unsigned char>(Text[Index++]);
                return static_cast<Uint32>(((C0 & 0x1Fu) << 6) | (C1 & 0x3Fu));
            }
            if ((C0 & 0xF0u) == 0xE0u && Index + 1 < Text.size())
            {
                const unsigned char C1 = static_cast<unsigned char>(Text[Index++]);
                const unsigned char C2 = static_cast<unsigned char>(Text[Index++]);
                return static_cast<Uint32>(((C0 & 0x0Fu) << 12) | ((C1 & 0x3Fu) << 6) | (C2 & 0x3Fu));
            }
            if ((C0 & 0xF8u) == 0xF0u && Index + 2 < Text.size())
            {
                const unsigned char C1 = static_cast<unsigned char>(Text[Index++]);
                const unsigned char C2 = static_cast<unsigned char>(Text[Index++]);
                const unsigned char C3 = static_cast<unsigned char>(Text[Index++]);
                return static_cast<Uint32>(((C0 & 0x07u) << 18) | ((C1 & 0x3Fu) << 12) | ((C2 & 0x3Fu) << 6) |
                                           (C3 & 0x3Fu));
            }
            return '?';
        }

        void AppendHudPrimitives(EngineKernel& K, std::vector<GpuVertex>& HudVertices,
                                 std::vector<GpuBatch>& HudBatches)
        {
            K.Draw().ForEachPrimitive2D(
                [&](const Primitive2D& P)
                {
                    const SDL_FColor C = UnpackColor(P.Color);
                    if (P.Type == Primitive2DType::Dot)
                    {
                        dbc::AppendGpuQuad(K, HudVertices, HudBatches, WhiteGpuTexture(K), static_cast<float>(P.X1),
                                           static_cast<float>(P.Y1), 1.0f, 1.0f, C);
                    }
                    else if (P.Type == Primitive2DType::Line)
                    {
                        dbc::AppendGpuLine(K, HudVertices, HudBatches, static_cast<float>(P.X1),
                                           static_cast<float>(P.Y1), static_cast<float>(P.X2), static_cast<float>(P.Y2),
                                           C, 1.0f);
                    }
                    else if (P.Type == Primitive2DType::Box)
                    {
                        // Filled, inclusive of its right and bottom edge, shaded between the corner colours.
                        const float L = static_cast<float>(std::min(P.X1, P.X2));
                        const float T = static_cast<float>(std::min(P.Y1, P.Y2));
                        const float R = static_cast<float>(std::max(P.X1, P.X2)) + 1.0f;
                        const float B = static_cast<float>(std::max(P.Y1, P.Y2)) + 1.0f;
                        const SDL_FColor Tl = UnpackColor(P.CornerColors[0]);
                        const SDL_FColor Tr = UnpackColor(P.CornerColors[1]);
                        const SDL_FColor Br = UnpackColor(P.CornerColors[2]);
                        const SDL_FColor Bl = UnpackColor(P.CornerColors[3]);
                        const SDL_FColor Corners[4]{Tl, Tr, Br, Bl};
                        const float ZeroN[3][3]{};
                        const float PosA[3][3]{{L, T, 0.0f}, {R, T, 0.0f}, {R, B, 0.0f}};
                        const SDL_FColor ColA[3]{Tl, Tl, Tl};
                        const SDL_FPoint UvA[3]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
                        dbc::AppendGpuTriangle(K, HudVertices, HudBatches, WhiteGpuTexture(K), PosA, ZeroN, ColA, UvA,
                                               false, true, false, {}, Corners);
                        const float PosB[3][3]{{L, T, 0.0f}, {R, B, 0.0f}, {L, B, 0.0f}};
                        const SDL_FColor ColB[3]{Tl, Tl, Tl};
                        const SDL_FPoint UvB[3]{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
                        dbc::AppendGpuTriangle(K, HudVertices, HudBatches, WhiteGpuTexture(K), PosB, ZeroN, ColB, UvB,
                                               false, true, false, {}, Corners);
                    }
                    else if (P.Type == Primitive2DType::Circle || P.Type == Primitive2DType::Ellipse)
                    {
                        const float Cx = static_cast<float>(P.X1);
                        const float Cy = static_cast<float>(P.Y1);
                        const float Rx = std::max(1.0f, std::abs(static_cast<float>(P.X2)));
                        const float Ry = std::max(1.0f, std::abs(static_cast<float>(P.Y2)));
                        constexpr int KSegs = 48;
                        float Px = Cx + Rx;
                        float Py = Cy;
                        for (int I = 1; I <= KSegs; ++I)
                        {
                            const float A = static_cast<float>(I) * static_cast<float>(2.0 * M_PI / KSegs);
                            const float Nx = Cx + std::cos(A) * Rx;
                            const float Ny = Cy + std::sin(A) * Ry;
                            dbc::AppendGpuLine(K, HudVertices, HudBatches, Px, Py, Nx, Ny, C, 1.0f);
                            Px = Nx;
                            Py = Ny;
                        }
                    }
                });
        }

        void AppendHudText(EngineKernel& K, std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
        {
            K.Text().ForEachDebugTextLine(
                [&](const DebugTextLine& Line)
                {
                    if (Line.Value.empty())
                        return;

                    int TextW = 0;
                    int TextH = 0;
                    if (Line.Request.Opaque && K.Text().MeasureText(Line.Request, Line.Value, TextW, TextH))
                    {
                        dbc::AppendGpuQuad(K, HudVertices, HudBatches, WhiteGpuTexture(K), Line.X, Line.Y,
                                           static_cast<float>(TextW), static_cast<float>(TextH),
                                           UnpackColor(K.Draw().BackdropColor()), 0.0f, false, true);
                    }

                    if (!K.Text().EnsureActiveTextFont(Line.Request))
                        return;
                    const int LineSkip = std::max(1, TTF_GetFontLineSkip(K.Text().ActiveTextFont()));
                    const SDL_FColor GlyphColor = UnpackColor(Line.Color);

                    float PenX = Line.X;
                    float PenY = Line.Y;
                    Uint32 PrevCp = 0;
                    bool HasPrev = false;
                    for (size_t I = 0; I < Line.Value.size();)
                    {
                        const Uint32 Cp = DecodeUtf8Codepoint(Line.Value, I);
                        if (Cp == '\r')
                            continue;
                        if (Cp == '\n')
                        {
                            PenX = Line.X;
                            PenY += static_cast<float>(LineSkip);
                            HasPrev = false;
                            PrevCp = 0;
                            continue;
                        }

                        TextGlyph* Glyph = K.Text().FindTextGlyph(Line.Request, Cp);
                        if (!Glyph)
                        {
                            HasPrev = false;
                            PrevCp = 0;
                            continue;
                        }

                        int Kerning = 0;
                        if (HasPrev)
                        {
                            (void)TTF_GetGlyphKerning(K.Text().ActiveTextFont(), PrevCp, Cp, &Kerning);
                        }

                        if (Glyph->Width > 0 && Glyph->Height > 0)
                        {
                            if (K.Text().Atlas().GpuTexture)
                            {
                                const float DrawX = PenX + static_cast<float>(Glyph->Minx + Kerning);
                                // SDL_ttf already places every rendered glyph surface on its line origin.
                                // Applying ascent/maxy here a second time makes characters jump vertically.
                                const float DrawY = PenY;
                                dbc::AppendGpuQuadUv(K, HudVertices, HudBatches, K.Text().Atlas().GpuTexture, DrawX,
                                                     DrawY, static_cast<float>(Glyph->Width),
                                                     static_cast<float>(Glyph->Height), GlyphColor,
                                                     SDL_FPoint{Glyph->U0, Glyph->V0}, SDL_FPoint{Glyph->U1, Glyph->V1},
                                                     0.0f, true, true);
                            }
                        }

                        PenX += static_cast<float>(Glyph->Advance + Kerning);
                        PrevCp = Cp;
                        HasPrev = true;
                    }
                });
        }

        void AppendHudImages(EngineKernel& K, std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
        {
            K.ImageStoreRef().ForEachOverlayBlit(
                [&](const OverlayImageBlit& Blit)
                {
                    Image* BlitImg = K.ImageStoreRef().GetImage(Blit.ImageId);
                    if (!BlitImg || (!BlitImg->Texture && !BlitImg->GpuTexture))
                        return;
                    SDL_GPUTexture* Tex = dbc::ImageGpuTexture(K, *BlitImg, Blit.TransparentBlack);
                    if (!Tex)
                        return;
                    dbc::AppendGpuQuad(K, HudVertices, HudBatches, Tex, Blit.X, Blit.Y,
                                       static_cast<float>(BlitImg->Width), static_cast<float>(BlitImg->Height),
                                       SDL_FColor{1, 1, 1, 1}, 0.0f, Blit.TransparentBlack || BlitImg->HasAlpha);
                });
        }

        void AppendHudSprites(EngineKernel& K, std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
        {
            const auto AppendSprite = [&](const SpriteData& Spr)
            {
                Image* SprImg = K.ImageStoreRef().GetImage(Spr.ImageId);
                if (!SprImg || (!SprImg->Texture && !SprImg->GpuTexture))
                    return;
                SDL_GPUTexture* Tex = dbc::ImageGpuTexture(K, *SprImg, false);
                if (!Tex)
                    return;
                float W = Spr.CustomSize
                              ? static_cast<float>(Spr.SizeX)
                              : static_cast<float>(Spr.AnimatedAtlas
                                                       ? Spr.FrameWidth
                                                       : (Spr.ImageWidth > 0 ? Spr.ImageWidth : SprImg->Width)) *
                                    Spr.ScaleX / 100.0f;
                float H = Spr.CustomSize
                              ? static_cast<float>(Spr.SizeY)
                              : static_cast<float>(Spr.AnimatedAtlas
                                                       ? Spr.FrameHeight
                                                       : (Spr.ImageHeight > 0 ? Spr.ImageHeight : SprImg->Height)) *
                                    Spr.ScaleY / 100.0f;
                SpriteData Positioned = Spr;
                Positioned.X = Spr.X * K.Sprites().SpriteGlobalScaleX() + K.Sprites().SpriteGlobalOffsetX();
                const float SpriteScaleX = Spr.CustomSize ? 1.0f : Spr.ScaleX / 100.0f;
                const float SpriteScaleY = Spr.CustomSize ? 1.0f : Spr.ScaleY / 100.0f;
                Positioned.OffsetX =
                    static_cast<int>(static_cast<float>(Spr.OffsetX) * SpriteScaleX * K.Sprites().SpriteGlobalScaleX());
                Positioned.OffsetY = static_cast<int>(static_cast<float>(Spr.OffsetY) * SpriteScaleY);
                W *= K.Sprites().SpriteGlobalScaleX();
                const SDL_FColor Color{
                    std::clamp(Spr.DiffuseR / 255.0f, 0.0f, 1.0f), std::clamp(Spr.DiffuseG / 255.0f, 0.0f, 1.0f),
                    std::clamp(Spr.DiffuseB / 255.0f, 0.0f, 1.0f), std::clamp(Spr.Alpha / 255.0f, 0.0f, 1.0f)};
                const bool Transparent = Color.a < 0.999f || (Spr.Transparent && SprImg->HasAlpha);
                dbc::AppendGpuSpriteQuad(K, HudVertices, HudBatches, Tex, Positioned, W, H, Color, Transparent);
            };
            K.Sprites().ForEachVisibleSprite(AppendSprite);
            K.Sprites().ForEachPastedSprite(AppendSprite);
        }
    } // namespace

    void EnsureHudGlyphs(EngineKernel& K)
    {
        K.Text().ForEachDebugTextLine(
            [&](const DebugTextLine& Line)
            {
                if (Line.Value.empty() || !K.Text().EnsureActiveTextFont(Line.Request))
                    return;
                for (size_t I = 0; I < Line.Value.size();)
                {
                    const Uint32 Cp = DecodeUtf8Codepoint(Line.Value, I);
                    if (Cp == '\r' || Cp == '\n')
                        continue;
                    (void)K.Text().EnsureTextGlyph(Line.Request, Cp);
                }
            });
    }

    void AppendVirtualPad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches)
    {
        if (!K.Input().VirtualPadVisible())
            return;
        const float Width = static_cast<float>(K.Core().WindowWidth());
        const float Height = static_cast<float>(K.Core().WindowHeight());
        SDL_GPUTexture* White = WhiteGpuTexture(K);
        for (int Index = 0; Index < KPadButtonCount; ++Index)
        {
            const PadButton Button = static_cast<PadButton>(Index);
            const SDL_FRect Rect = PadButtonRect(Button, Width, Height);
            const bool Held = K.Input().VirtualPadHeld(Button);
            // A dark plate under a light sign reads over any picture.
            dbc::AppendGpuQuad(K, Vertices, Batches, White, Rect.x, Rect.y, Rect.w, Rect.h,
                               SDL_FColor{0.0f, 0.0f, 0.0f, Held ? 0.55f : 0.3f}, 0.0f, true);
            const SDL_FColor Ink{1.0f, 1.0f, 1.0f, Held ? 0.95f : 0.6f};
            const float Unit = Rect.h;
            const float Thickness = std::max(1.0f, Unit * 0.07f);
            const float Cx = Rect.x + Rect.w * 0.5f;
            const float Cy = Rect.y + Rect.h * 0.5f;
            const auto Line = [&](float X1, float Y1, float X2, float Y2)
            { dbc::AppendGpuLine(K, Vertices, Batches, X1, Y1, X2, Y2, Ink, Thickness, 0.0f, true); };
            const float Arm = Unit * 0.2f;
            switch (Button)
            {
                case PadButton::Up:
                    Line(Cx - Arm, Cy + Arm * 0.5f, Cx, Cy - Arm * 0.5f);
                    Line(Cx, Cy - Arm * 0.5f, Cx + Arm, Cy + Arm * 0.5f);
                    break;
                case PadButton::Down:
                    Line(Cx - Arm, Cy - Arm * 0.5f, Cx, Cy + Arm * 0.5f);
                    Line(Cx, Cy + Arm * 0.5f, Cx + Arm, Cy - Arm * 0.5f);
                    break;
                case PadButton::Left:
                    Line(Cx + Arm * 0.5f, Cy - Arm, Cx - Arm * 0.5f, Cy);
                    Line(Cx - Arm * 0.5f, Cy, Cx + Arm * 0.5f, Cy + Arm);
                    break;
                case PadButton::Right:
                    Line(Cx - Arm * 0.5f, Cy - Arm, Cx + Arm * 0.5f, Cy);
                    Line(Cx + Arm * 0.5f, Cy, Cx - Arm * 0.5f, Cy + Arm);
                    break;
                case PadButton::Space:
                {
                    // The sign printed on a space bar.
                    const float Half = Rect.w * 0.28f;
                    const float Base = Cy + Arm * 0.5f;
                    Line(Cx - Half, Base - Arm, Cx - Half, Base);
                    Line(Cx - Half, Base, Cx + Half, Base);
                    Line(Cx + Half, Base, Cx + Half, Base - Arm);
                    break;
                }
                case PadButton::MouseLeft:
                case PadButton::MouseRight:
                {
                    // A mouse seen from above, with the button it stands for filled.
                    const float HalfW = Unit * 0.17f;
                    const float HalfH = Unit * 0.27f;
                    const float Split = Cy - HalfH * 0.25f;
                    Line(Cx - HalfW, Cy - HalfH, Cx + HalfW, Cy - HalfH);
                    Line(Cx + HalfW, Cy - HalfH, Cx + HalfW, Cy + HalfH);
                    Line(Cx + HalfW, Cy + HalfH, Cx - HalfW, Cy + HalfH);
                    Line(Cx - HalfW, Cy + HalfH, Cx - HalfW, Cy - HalfH);
                    Line(Cx - HalfW, Split, Cx + HalfW, Split);
                    Line(Cx, Cy - HalfH, Cx, Split);
                    const float Left = Button == PadButton::MouseLeft ? Cx - HalfW : Cx;
                    dbc::AppendGpuQuad(K, Vertices, Batches, White, Left, Cy - HalfH, HalfW, Split - Cy + HalfH, Ink,
                                       0.0f, true);
                    break;
                }
            }
        }
    }

    void AppendHudLayer(EngineKernel& K, std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
    {
        AppendHudPrimitives(K, HudVertices, HudBatches);
        AppendHudText(K, HudVertices, HudBatches);
        AppendHudImages(K, HudVertices, HudBatches);
        AppendHudSprites(K, HudVertices, HudBatches);
    }
} // namespace dbc
