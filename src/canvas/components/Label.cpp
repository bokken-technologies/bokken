#include "Label.hpp"
#include "../Drawing.hpp"

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            namespace
            {
                size_t decodeUTF8(const char *p, const char *end, uint32_t *cp)
                {
                    if (p >= end) { *cp = 0; return 0; }
                    const unsigned char c = (unsigned char)p[0];
                    if (c < 0x80) { *cp = c; return 1; }
                    if ((c & 0xE0) == 0xC0 && p + 1 < end)
                    {
                        *cp = ((c & 0x1F) << 6) | ((unsigned char)p[1] & 0x3F);
                        return 2;
                    }
                    if ((c & 0xF0) == 0xE0 && p + 2 < end)
                    {
                        *cp = ((c & 0x0F) << 12)
                            | (((unsigned char)p[1] & 0x3F) << 6)
                            | ((unsigned char)p[2] & 0x3F);
                        return 3;
                    }
                    if ((c & 0xF8) == 0xF0 && p + 3 < end)
                    {
                        *cp = ((c & 0x07) << 18)
                            | (((unsigned char)p[1] & 0x3F) << 12)
                            | (((unsigned char)p[2] & 0x3F) << 6)
                            | ((unsigned char)p[3] & 0x3F);
                        return 4;
                    }
                    *cp = '?'; return 1;
                }

                /* Width of a string in pixels using SDL_ttf, plus any
                 * extra advance from letterSpacing. We can't trust
                 * TTF_GetStringSize alone because it doesn't know about
                 * letterSpacing — we add (n_glyphs * letterSpacing). */
                float measureRun(TTF_Font *font, const std::string &s, float letterSpacing)
                {
                    if (s.empty()) return 0.0f;
                    int w = 0, h = 0;
                    TTF_GetStringSize(font, s.c_str(), 0, &w, &h);

                    if (letterSpacing == 0.0f) return (float)w;

                    /* Count UTF-8 codepoints to know how many gaps we
                     * need. */
                    int n = 0;
                    const char *p = s.data();
                    const char *e = p + s.size();
                    while (p < e)
                    {
                        uint32_t cp = 0;
                        size_t adv = decodeUTF8(p, e, &cp);
                        if (adv == 0) break;
                        p += adv;
                        n++;
                    }
                    /* (n-1) gaps between glyphs; clamp to >=0. */
                    return (float)w + std::max(0, n - 1) * letterSpacing;
                }
            }

            TTF_Font *Label::get_font(const std::string &path, float size, AssetPack *assets)
            {
                const int px = (int)(size + 0.5f);
                std::string key = path + ":" + std::to_string(px);
                auto it = s_measureFonts.find(key);
                if (it != s_measureFonts.end()) return it->second;

                if (!assets)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Label] AssetPack not initialized.");
                    return nullptr;
                }

                std::string targetPath = path;
                if (targetPath.empty() || !assets->exists(targetPath))
                    targetPath = "fonts/default.ttf";
                if (!assets->exists(targetPath))
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Label] Font not found: %s", targetPath.c_str());
                    return nullptr;
                }

                SDL_IOStream *stream = assets->openIOStream(targetPath);
                if (!stream) return nullptr;
                TTF_Font *font = TTF_OpenFontIO(stream, true, px);
                if (!font)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "[Label] TTF_OpenFontIO failed: %s", SDL_GetError());
                    return nullptr;
                }
                s_measureFonts[key] = font;
                return font;
            }

            void Label::clear_font_cache()
            {
                for (auto &kv : s_measureFonts)
                    if (kv.second) TTF_CloseFont(kv.second);
                s_measureFonts.clear();
            }

            std::vector<std::string> Label::wrapLines(const std::string &text,
                                                      TTF_Font *font,
                                                      float maximumWidth,
                                                      float letterSpacing)
            {
                std::vector<std::string> out;
                if (text.empty()) { out.push_back(""); return out; }
                if (!font || maximumWidth <= 0.0f)
                {
                    /* No wrap possible — preserve explicit \n breaks
                     * regardless. */
                    std::string cur;
                    for (char c : text)
                    {
                        if (c == '\n') { out.push_back(cur); cur.clear(); }
                        else cur.push_back(c);
                    }
                    out.push_back(cur);
                    return out;
                }

                std::string cur;
                std::string word;

                auto pushWord = [&]() {
                    if (word.empty()) return;
                    /* Try to add word to current line. If it doesn't fit
                     * and the line isn't empty, wrap to a new line. */
                    std::string trial = cur.empty() ? word : (cur + " " + word);
                    if (measureRun(font, trial, letterSpacing) <= maximumWidth || cur.empty())
                    {
                        cur = trial;
                    }
                    else
                    {
                        out.push_back(cur);
                        cur = word;
                    }
                    /* If the word alone exceeds maximumWidth we accept the
                     * overflow on this line. Hard-breaking inside a word
                     * is a UI choice with locale concerns — Latin scripts
                     * usually prefer overflow to mid-word break. */
                    word.clear();
                };

                const char *p = text.data();
                const char *e = p + text.size();
                while (p < e)
                {
                    char c = *p++;
                    if (c == '\n')
                    {
                        pushWord();
                        out.push_back(cur);
                        cur.clear();
                    }
                    else if (c == ' ' || c == '\t')
                    {
                        pushWord();
                    }
                    else
                    {
                        word.push_back(c);
                    }
                }
                pushWord();
                out.push_back(cur);
                return out;
            }

            void Label::computeNode(std::shared_ptr<Node> node, AssetPack *assets)
            {
                const auto &s = node->style;
                const float fSize = s.fontSize > 0.f ? s.fontSize : 16.f;
                const std::string fontPath = s.font.empty() ? std::string("fonts/default.ttf") : s.font;

                TTF_Font *font = Label::get_font(fontPath, fSize, assets);
                if (!font) return;

                const int lineHeight = TTF_GetFontHeight(font);
                const float scaledLine = (float)lineHeight * (s.lineHeight > 0.0f ? s.lineHeight : 1.0f);

                const float pT = resolveSide(s.paddingTop,    s.padding);
                const float pB = resolveSide(s.paddingBottom, s.padding);
                const float pL = resolveSide(s.paddingLeft,   s.padding);
                const float pR = resolveSide(s.paddingRight,  s.padding);

                /* Decide wrap vs single-line. Wrap requires a fixed
                 * width to wrap into; otherwise single-line. */
                const bool hasFixedWidth = (s.width > 0.0f) || s.widthIsPercent
                    || (node->layout.w > 0.0f && s.wordWrap);
                bool useWrap = s.wordWrap && hasFixedWidth;

                if (useWrap)
                {
                    /* For wrap measurement we need to know the content
                     * width. Two cases:
                     *   - explicit s.width > 0  → use it minus padding.
                     *   - s.widthIsPercent      → resolved on layout.w
                     *                             which Layout::measure
                     *                             will set later. We use
                     *                             the current layout.w
                     *                             as a working estimate.
                    */
                    float contentW = (s.width > 0.0f && !s.widthIsPercent)
                        ? s.width - pL - pR
                        : node->layout.w - pL - pR;
                    if (contentW <= 0.0f) contentW = node->layout.w;

                    auto lines = wrapLines(node->textContent, font, contentW, s.letterSpacing);
                    if (lines.empty()) lines.push_back("");

                    float maxLine = 0.0f;
                    for (auto &ln : lines)
                        maxLine = std::max(maxLine, measureRun(font, ln, s.letterSpacing));

                    node->intrinsicW = maxLine;
                    node->intrinsicH = scaledLine * lines.size();

                    if (s.width <= 0.0f && !s.widthIsPercent)
                        node->layout.w = maxLine + pL + pR;
                    if (s.height <= 0.0f && !s.heightIsPercent)
                        node->layout.h = node->intrinsicH + pT + pB;
                }
                else
                {
                    int textW = 0, textH = 0;
                    TTF_GetStringSize(font, node->textContent.c_str(), 0, &textW, &textH);
                    float runW = (float)textW;
                    if (s.letterSpacing != 0.0f)
                        runW = measureRun(font, node->textContent, s.letterSpacing);

                    node->intrinsicW = runW;
                    node->intrinsicH = scaledLine;

                    if (s.width <= 0.0f && !s.widthIsPercent)
                        node->layout.w = runW + pL + pR;
                    if (s.height <= 0.0f && !s.heightIsPercent)
                        node->layout.h = scaledLine + pT + pB;
                }
            }

            void Label::layoutNode(std::shared_ptr<Node> /*node*/) {}

            std::shared_ptr<Node> Label::toNode()
            {
                auto node = std::make_shared<Node>("Label");
                node->textContent = m_text;
                node->style = m_style;
                node->onCompute = &computeNode;
                node->onLayout  = &layoutNode;
                return node;
            }

            void Label::draw(Renderer::SpriteBatcher &batcher,
                             std::shared_ptr<Node> node,
                             AssetPack *assets, int layer)
            {
                if (!node || node->textContent.empty()) return;
                if (!s_glyphCache) return;

                const auto &s = node->style;
                const float fSize = s.fontSize > 0.f ? s.fontSize : 16.f;
                const std::string fontPath = s.font.empty() ? std::string("fonts/default.ttf") : s.font;

                /* Color resolution. hoverColor with non-zero alpha takes
                 * precedence when hovered. Opacity multiplies alpha. */
                uint32_t baseRgba = s.color;
                if (node->isHovered && (s.hoverColor & 0xFFu))
                    baseRgba = s.hoverColor;
                const uint32_t tint = Drawing::applyTint(baseRgba, node->getGlobalOpacity()
                                                                    * (s.disabled ? 0.5f : 1.0f));
                if ((tint & 0xFFu) == 0u) return;

                const float pT = resolveSide(s.paddingTop, s.padding);
                const float pB = resolveSide(s.paddingBottom, s.padding);
                const float pL = resolveSide(s.paddingLeft, s.padding);
                const float pR = resolveSide(s.paddingRight, s.padding);

                TTF_Font *ttf = Label::get_font(fontPath, fSize, assets);
                if (!ttf) return;
                /* Fetch the GlyphCache font handle ONCE here, outside
                 * the per-glyph loop. Without this, getGlyph builds a
                 * std::string ("path@size") for every codepoint —
                 * thousands of allocations per frame on a Code block. */
                Renderer::GlyphCache::FontHandle fontHandle =
                    s_glyphCache->getFontHandle(fontPath, fSize, assets);
                if (!fontHandle.valid()) return;
                const float ascent = (float)TTF_GetFontAscent(ttf);
                const float descent = (float)TTF_GetFontDescent(ttf);  /* typically negative */
                const int   metricLine = TTF_GetFontHeight(ttf);
                const float scaledLine = (float)metricLine * (s.lineHeight > 0.0f ? s.lineHeight : 1.0f);

                const float contentLeft   = node->layout.x + pL;
                const float contentTop    = node->layout.y + pT;
                const float contentRight  = node->layout.x + node->layout.w - pR;
                const float contentBottom = node->layout.y + node->layout.h - pB;
                const float contentW = std::max(0.0f, contentRight - contentLeft);
                const float contentH = std::max(0.0f, contentBottom - contentTop);

                /* Build the lines we'll actually render. Cached on the
                 * node — wrapLines is the per-frame hot spot for any
                 * scene with many wrapping Labels because it walks the
                 * UTF-8 string and calls TTF_GetStringSize per word
                 * boundary. We invalidate by comparing against the
                 * cached input key (text + contentW + font + size +
                 * letterSpacing); when it matches we reuse the lines. */
                std::vector<std::string> *linesPtr = nullptr;
                std::vector<std::string> tmpLines;
                const bool hasFixedWidth = (s.width > 0.0f) || s.widthIsPercent;
                if (s.wordWrap && hasFixedWidth)
                {
                    const bool cacheHit =
                        node->cachedWrapText == node->textContent &&
                        node->cachedWrapFont == fontPath &&
                        node->cachedWrapFontSz == fSize &&
                        std::abs(node->cachedWrapWidth - contentW) < 0.5f &&
                        node->cachedWrapLetterSp == s.letterSpacing &&
                        !node->cachedWrappedLines.empty();
                    if (!cacheHit)
                    {
                        node->cachedWrappedLines = wrapLines(
                            node->textContent, ttf, contentW, s.letterSpacing);
                        node->cachedWrapText    = node->textContent;
                        node->cachedWrapFont    = fontPath;
                        node->cachedWrapFontSz  = fSize;
                        node->cachedWrapWidth   = contentW;
                        node->cachedWrapLetterSp = s.letterSpacing;
                    }
                    linesPtr = &node->cachedWrappedLines;
                }
                else
                {
                    tmpLines.push_back(node->textContent);
                    linesPtr = &tmpLines;
                }
                std::vector<std::string> &lines = *linesPtr;
                if (lines.empty()) return;

                /* Vertical block placement — supports cross-axis
                 * alignItems Start/Center/End on the parent's resolved
                 * size (we only see the content box here, but Layout has
                 * already computed our height). For Stretch we treat as
                 * Center.
                 *
                 * Important: for a SINGLE line with Center alignment we
                 * use the *visible ink* height (ascent + |descent|)
                 * instead of `scaledLine`. The full metric line height
                 * includes leading (the gap between consecutive lines),
                 * but the gap above the cap-height and below the descent
                 * is empty — using `scaledLine` for centering makes a
                 * single-line label visually float toward the top. The
                 * web fixes this by always centering on `line-height`
                 * (which is what we did before), but the result looks
                 * wrong inside chunky controls like buttons and inputs
                 * where the user expects the text to sit visually
                 * centered.
                 *
                 * Multi-line labels keep using scaledLine so consecutive
                 * lines maintain their natural leading. */
                const bool singleLine = (lines.size() == 1);
                const float inkH = singleLine ? (ascent - descent) : scaledLine;
                const float blockH = singleLine ? inkH : (scaledLine * (float)lines.size());
                float blockTop;
                switch (s.alignItems)
                {
                case SimpleStyleSheet::AlignItems::Start:   blockTop = contentTop;                          break;
                case SimpleStyleSheet::AlignItems::End:     blockTop = contentBottom - blockH;              break;
                default:                                    blockTop = contentTop + (contentH - blockH) * 0.5f; break;
                }

                const Renderer::Texture2D *atlas = s_glyphCache->atlas();
                if (!atlas || !atlas->isValid()) return;
                const float atlasW = (float)atlas->width();
                const float atlasH = (float)atlas->height();

                const float scale = node->getGlobalScale();
                const bool isScaling = (scale != 1.0f);

                /* Shaped-quad cache check
                 *
                 * The expensive path below iterates codepoints, looks
                 * up glyphs, accumulates kerning/letterSpacing, and
                 * emits one quad per glyph. For static text (which is
                 * most text in any UI), nothing about that work
                 * changes between frames — yet it ran every frame.
                 *
                 * Cache the per-glyph (x, y, w, h, u0, v0, u1, v1)
                 * tuples relative to a (0, 0) origin. On cache hit we
                 * skip straight to the emit loop, translating each
                 * quad by (contentLeft, blockTop) and applying the
                 * current scale around the text's centre. Color,
                 * opacity, scale, and screen position all live at
                 * emit time so cache hits survive hover transitions
                 * and scroll.
                 *
                 * Cache invalidation: any input that changes the
                 * shaped geometry (text, font, size, contentW for
                 * wrap, letterSpacing, lineHeight, textAlign,
                 * alignItems, container size) is in the key. */
                const int alignH = (int)s.textAlign;
                const int alignV = (int)s.alignItems;
                const bool shapeCacheHit =
                    !node->cachedShapedQuads.empty() &&
                    node->cachedShapeText    == node->textContent &&
                    node->cachedShapeFont    == fontPath &&
                    node->cachedShapeFontSz  == fSize &&
                    std::abs(node->cachedShapeWidth - contentW) < 0.5f &&
                    std::abs(node->cachedShapeContainerW - contentW) < 0.5f &&
                    std::abs(node->cachedShapeContainerH - contentH) < 0.5f &&
                    node->cachedShapeLetterSp == s.letterSpacing &&
                    node->cachedShapeLineH   == s.lineHeight &&
                    node->cachedShapeAlignH  == alignH &&
                    node->cachedShapeAlignV  == alignV;

                if (shapeCacheHit)
                {
                    /* Fast path. Just emit the cached quads — translated
                     * to the current screen position, scaled around the
                     * text centre if hover-animating, tinted with the
                     * current colour. */
                    const float originX = contentLeft + contentW * 0.5f;
                    const float originY = contentTop + contentH * 0.5f;
                    for (const auto &q : node->cachedShapedQuads)
                    {
                        float gx = contentLeft + q.x;
                        float gy = contentTop + q.y;
                        float gw = q.w;
                        float gh = q.h;
                        if (isScaling)
                        {
                            gx = originX + (gx - originX) * scale;
                            gy = originY + (gy - originY) * scale;
                            gw *= scale;
                            gh *= scale;
                        }
                        const float u0 = q.u0 / atlasW;
                        const float v0 = q.v0 / atlasH;
                        const float u1 = q.u1 / atlasW;
                        const float v1 = q.v1 / atlasH;
                        batcher.drawTextured(atlas, gx, gy, gw, gh,
                                              u0, v0, u1, v1, tint, layer);
                    }
                    return;
                }

                /* Cache miss — shape into the cache as we go. We keep
                 * the original shaping loop intact (still emits) but
                 * also populate cachedShapedQuads with the relative
                 * coordinates so the next frame hits the fast path. */
                node->cachedShapedQuads.clear();
                node->cachedShapedQuads.reserve(node->textContent.size());

                /* Draw line by line. */
                for (size_t li = 0; li < lines.size(); li++)
                {
                    const std::string &line = lines[li];
                    if (line.empty()) continue;

                    float lineW = measureRun(ttf, line, s.letterSpacing);

                    /* Horizontal placement: textAlign overrides
                     * justifyContent only when we have a fixed content
                     * box; otherwise the line just starts at content
                     * left. */
                    float penX;
                    if (hasFixedWidth)
                    {
                        switch (s.textAlign)
                        {
                        case TextAlign::Center: penX = contentLeft + (contentW - lineW) * 0.5f; break;
                        case TextAlign::Right:  penX = contentRight - lineW;                    break;
                        default:                penX = contentLeft;                              break;
                        }
                    }
                    else
                    {
                        penX = contentLeft;
                    }

                    /* Baseline for this line.
                     *
                     * Single-line: blockTop is at the top of the ink
                     * (ascent above baseline). baseline = blockTop + ascent.
                     *
                     * Multi-line: blockTop is at the top of the metric
                     * box; we add the line's index times scaledLine, plus
                     * any leading (scaledLine - metricLine) split evenly
                     * above and below to keep the baseline visually
                     * inside its line. */
                    float baseline;
                    if (singleLine)
                    {
                        baseline = blockTop + ascent;
                    }
                    else
                    {
                        baseline = blockTop + scaledLine * (float)li + ascent
                                    + (scaledLine - (float)metricLine) * 0.5f;
                    }
                    if (!isScaling)
                    {
                        penX = std::round(penX);
                        baseline = std::round(baseline);
                    }

                    float remainder = 0.0f;
                    const char *p = line.data();
                    const char *end = p + line.size();
                    while (p < end)
                    {
                        uint32_t cp = 0;
                        size_t adv = decodeUTF8(p, end, &cp);
                        if (adv == 0) break;
                        p += adv;

                        const auto *g = s_glyphCache->getGlyphFast(fontHandle, cp);
                        if (!g) continue;

                        if (g->width == 0 || g->height == 0)
                        {
                            if (isScaling)
                                penX += ((float)g->advance + s.letterSpacing) * scale;
                            else
                            {
                                float rawAdv = (float)g->advance + s.letterSpacing + remainder;
                                float snapped = std::round(rawAdv);
                                remainder = rawAdv - snapped;
                                penX += snapped;
                            }
                            continue;
                        }

                        float gx = penX + (float)g->bearingX;
                        float gy = baseline - (float)g->bearingY;
                        float gw = (float)g->width;
                        float gh = (float)g->height;

                        if (isScaling)
                        {
                            const float originX = penX + lineW * 0.5f;
                            const float originY = baseline - ascent + scaledLine * 0.5f;
                            gx = originX + (gx - originX) * scale;
                            gy = originY + (gy - originY) * scale;
                            gw *= scale;
                            gh *= scale;
                        }
                        else
                        {
                            gx = std::round(gx);
                            gy = std::round(gy);
                            gw = std::round(gw);
                            gh = std::round(gh);
                        }

                        const float u0 = (float)g->u0 / atlasW;
                        const float v0 = (float)g->v0 / atlasH;
                        const float u1 = (float)g->u1 / atlasW;
                        const float v1 = (float)g->v1 / atlasH;

                        batcher.drawTextured(atlas, gx, gy, gw, gh,
                                              u0, v0, u1, v1, tint, layer);

                        /* Populate the shape cache with coordinates
                         * relative to (contentLeft, contentTop). On
                         * cache hit next frame we re-translate to the
                         * current contentLeft/Top (so scrolling and
                         * window resize don't invalidate). UVs go in
                         * as raw atlas pixel coords; the fast path
                         * normalises to atlas size at emit so the
                         * cache survives atlas grow. */
                        if (!isScaling)
                        {
                            Node::ShapedQuad sq;
                            sq.x = gx - contentLeft;
                            sq.y = gy - contentTop;
                            sq.w = gw;
                            sq.h = gh;
                            sq.u0 = (float)g->u0;
                            sq.v0 = (float)g->v0;
                            sq.u1 = (float)g->u1;
                            sq.v1 = (float)g->v1;
                            node->cachedShapedQuads.push_back(sq);
                        }

                        if (isScaling)
                            penX += ((float)g->advance + s.letterSpacing) * scale;
                        else
                        {
                            float rawAdv = (float)g->advance + s.letterSpacing + remainder;
                            float snapped = std::round(rawAdv);
                            remainder = rawAdv - snapped;
                            penX += snapped;
                        }
                    }
                }

                /* Commit cache key. We only cache the un-scaled shape
                 * (scale is applied at emit), so isScaling builds don't
                 * populate the cache and won't trigger a hit on the
                 * next frame either — by design, an animating Label
                 * goes through the slow path until it settles, then
                 * the resting frame populates the cache. */
                if (!isScaling)
                {
                    node->cachedShapeText    = node->textContent;
                    node->cachedShapeFont    = fontPath;
                    node->cachedShapeFontSz  = fSize;
                    node->cachedShapeWidth   = contentW;
                    node->cachedShapeContainerW = contentW;
                    node->cachedShapeContainerH = contentH;
                    node->cachedShapeLetterSp = s.letterSpacing;
                    node->cachedShapeLineH   = s.lineHeight;
                    node->cachedShapeAlignH  = alignH;
                    node->cachedShapeAlignV  = alignV;
                }
                else
                {
                    /* Don't leave a stale cache around when animating —
                     * the next still frame will rebuild from scratch. */
                    node->cachedShapedQuads.clear();
                }
            }
        }
    }
}