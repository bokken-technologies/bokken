#include "TextInput.hpp"
#include "../Drawing.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            namespace
            {
                /* Per-node caret position (byte index into value) and
                 * placeholder text. We keep these out of the Node struct
                 * to avoid bloating every View/Label. Cleared lazily in
                 * insertText/handleKey when the node has gone (raw ptr
                 * lookup; OK because the maps are only consulted from
                 * the Canvas module which holds a shared_ptr already). */
                std::map<Node *, int> s_caretMap;
                std::map<Node *, std::string> s_placeholderMap;
                std::map<Node *, float> s_blinkMap; // 0..1, animated by Canvas update

                /* Selection: byte-index range. selStart == selEnd means
                 * "no selection" — caret only. selStart can be greater
                 * than selEnd when the user shift-drags backwards;
                 * normalisation happens at use sites. */
                struct Selection
                {
                    int start = 0;
                    int end = 0;
                };
                std::map<Node *, Selection> s_selectionMap;

                /* Resize drag override. When style.resize is true and
                 * the user drags the bottom-right grip, we store the
                 * resulting (w, h) here, and TextInput::draw applies
                 * it just before delegating to Label::draw / caret math.
                 *
                 * Caveat: the map is keyed on the node pointer, so it
                 * does not survive a setState re-render (synchronize_tree
                 * builds fresh nodes). Drag is purely visual within a
                 * tree's lifetime. Apps that need persistence can read
                 * `onResize` (TODO — not wired yet) and store in JS state. */
                struct ResizeOverride
                {
                    float w, h;
                };
                std::map<Node *, ResizeOverride> s_resizeMap;

                /* Drag state for the resize grip. */
                struct ResizeDrag
                {
                    float startMouseX = 0, startMouseY = 0;
                    float startW = 0, startH = 0;
                };
                std::map<Node *, ResizeDrag> s_resizeDragMap;
            }

            int TextInput::getCaret(const std::shared_ptr<Node> &node)
            {
                if (!node)
                    return 0;
                const int n = (int)node->value.size();
                auto it = s_caretMap.find(node.get());
                int caret = (it == s_caretMap.end()) ? n : it->second;
                if (caret < 0)
                    caret = 0;
                if (caret > n)
                    caret = n;
                while (caret > 0 && caret < n &&
                       (((unsigned char)node->value[caret] & 0xC0u) == 0x80u))
                {
                    caret--;
                }
                if (it != s_caretMap.end() && it->second != caret)
                    it->second = caret;
                return caret;
            }

            void TextInput::setCaret(std::shared_ptr<Node> node, int byteIndex)
            {
                if (!node)
                    return;
                int n = (int)node->value.size();
                if (byteIndex < 0)
                    byteIndex = 0;
                if (byteIndex > n)
                    byteIndex = n;
                /* Snap to a UTF-8 codepoint boundary — never leave the
                 * caret inside a continuation byte. */
                while (byteIndex > 0 && byteIndex < n &&
                       (((unsigned char)node->value[byteIndex] & 0xC0u) == 0x80u))
                {
                    byteIndex--;
                }
                s_caretMap[node.get()] = byteIndex;
                /* Reset the blink so the caret appears immediately on
                 * any caret movement — feels much snappier. */
                s_blinkMap[node.get()] = 0.0f;
            }

            void TextInput::setPlaceholderFor(std::shared_ptr<Node> node, const std::string &p)
            {
                if (!node)
                    return;
                if (p.empty())
                    s_placeholderMap.erase(node.get());
                else
                    s_placeholderMap[node.get()] = p;
            }

            void TextInput::tickBlink(std::shared_ptr<Node> node, float dt)
            {
                if (!node || !node->isFocused)
                    return;
                float &t = s_blinkMap[node.get()];
                t += dt;
                /* 1-second cycle: 0..0.5 shows, 0.5..1.0 hides. Wrap
                 * back at 1.0. Keeps the values bounded so floats
                 * don't drift over a long session. */
                while (t >= 1.0f)
                    t -= 1.0f;
            }

            bool TextInput::hitTestGrip(std::shared_ptr<Node> node, float mx, float my)
            {
                if (!node || !node->style.resize)
                    return false;
                /* Grip is a 16×16 square in the bottom-right (matches
                 * the visual ladder + a little hit slop). */
                const float gripPad = 4.0f;
                const float gripSize = 16.0f;
                const float gx = node->layout.x + node->layout.w - gripPad - gripSize;
                const float gy = node->layout.y + node->layout.h - gripPad - gripSize;
                return mx >= gx && mx <= gx + gripSize &&
                       my >= gy && my <= gy + gripSize;
            }

            void TextInput::beginResize(std::shared_ptr<Node> node, float mx, float my)
            {
                if (!node)
                    return;
                ResizeDrag d;
                d.startMouseX = mx;
                d.startMouseY = my;
                d.startW = node->layout.w;
                d.startH = node->layout.h;
                s_resizeDragMap[node.get()] = d;
            }

            void TextInput::dragResize(std::shared_ptr<Node> node, float mx, float my)
            {
                if (!node)
                    return;
                auto it = s_resizeDragMap.find(node.get());
                if (it == s_resizeDragMap.end())
                    return;
                /* New size = start + delta, clamped to a sensible
                 * minimum. We respect the user's minimumWidth /
                 * minimumHeight if set, else 80×40. */
                const float dx = mx - it->second.startMouseX;
                const float dy = my - it->second.startMouseY;
                const float minW = node->style.minimumWidth > 0.0f
                                       ? node->style.minimumWidth
                                       : 80.0f;
                const float minH = node->style.minimumHeight > 0.0f
                                       ? node->style.minimumHeight
                                       : 40.0f;
                node->layout.w = std::max(minW, it->second.startW + dx);
                node->layout.h = std::max(minH, it->second.startH + dy);
                /* Cache the override so future Layout::run calls
                 * within the SAME tree (rare — Layout reruns only on
                 * setState, which builds a fresh tree anyway) can
                 * reapply it. */
                s_resizeMap[node.get()] = ResizeOverride{node->layout.w, node->layout.h};
            }

            void TextInput::endResize(std::shared_ptr<Node> node)
            {
                if (!node)
                    return;
                s_resizeDragMap.erase(node.get());
            }

            bool TextInput::isResizing(std::shared_ptr<Node> node)
            {
                if (!node)
                    return false;
                return s_resizeDragMap.find(node.get()) != s_resizeDragMap.end();
            }

            void TextInput::applyDefaults(SimpleStyleSheet &s)
            {
                /* Make focusable + I-beam by default. Required for the
                 * click-to-focus path to set s_focused_node, which in
                 * turn enables SDL_StartTextInput to deliver typed
                 * characters here. */
                if (s.tabIndex == -1)
                    s.tabIndex = 0;
                if (s.cursor == Cursor::Default)
                    s.cursor = Cursor::Text;
                /* Text inputs read more naturally with the text at the
                 * top of the content area — multi-line wrap then grows
                 * downward as the user types, matching native textareas
                 * everywhere. The default View alignItems is Center,
                 * which floats the first line in the middle and made
                 * the multi-line caret math drift onto the wrong row. */
                if (s.alignItems == SimpleStyleSheet::AlignItems::Center)
                    s.alignItems = SimpleStyleSheet::AlignItems::Start;
                /* Sensible defaults — padding generous enough that the
                 * caret + text + placeholder all sit comfortably inside.
                 * minimumHeight bumped to 40 (was 36) so the input
                 * doesn't feel cramped at typical font sizes; user can
                 * still override. */
                if (s.padding == 0.0f)
                    s.padding = 10.0f;
                if (s.borderRadius == 0.0f)
                    s.borderRadius = 6.0f;
                if (s.minimumHeight == 0.0f)
                    s.minimumHeight = 40.0f;
                if (s.minimumWidth == 0.0f)
                    s.minimumWidth = 120.0f;
            }

            std::shared_ptr<Node> TextInput::toNode()
            {
                auto node = std::make_shared<Node>("TextInput");
                node->style = m_style;
                node->value = m_initialValue;
                node->textContent = m_initialValue;
                applyDefaults(node->style);

                if (!m_placeholder.empty())
                    setPlaceholderFor(node, m_placeholder);

                node->onCompute = &computeNode;

                /* Cleanup on tree replacement. */
                node->onDeconstruct = [n = node.get()]()
                {
                    s_caretMap.erase(n);
                    s_placeholderMap.erase(n);
                    s_blinkMap.erase(n);
                    s_selectionMap.erase(n);
                    s_resizeMap.erase(n);
                    s_resizeDragMap.erase(n);
                };
                return node;
            }

            void TextInput::computeNode(std::shared_ptr<Node> node, AssetPack *assets)
            {
                /* Measure exactly like a single-line Label using the
                 * value (or placeholder if value is empty) so the input
                 * has a sensible intrinsic size. The actual rendering
                 * truncates left when the value overflows. */
                auto saved = node->textContent;
                if (node->value.empty())
                {
                    auto it = s_placeholderMap.find(node.get());
                    if (it != s_placeholderMap.end())
                        node->textContent = it->second;
                }
                else
                {
                    node->textContent = node->value;
                }
                Label::computeNode(node, assets);
                node->textContent = saved;
            }

            void TextInput::insertText(std::shared_ptr<Node> node, const std::string &utf8)
            {
                if (!node || utf8.empty())
                    return;
                /* If there's an active selection, typing replaces it.
                 * Matches native textarea behaviour everywhere. */
                {
                    int lo = 0, hi = 0;
                    auto it = s_selectionMap.find(node.get());
                    if (it != s_selectionMap.end())
                    {
                        lo = std::min(it->second.start, it->second.end);
                        hi = std::max(it->second.start, it->second.end);
                    }
                    if (hi > lo)
                    {
                        node->value.erase(lo, hi - lo);
                        s_caretMap[node.get()] = lo;
                        s_selectionMap.erase(node.get());
                    }
                }
                int caret = getCaret(node);
                node->value.insert(caret, utf8);
                setCaret(node, caret + (int)utf8.size());
                node->textContent = node->value;
                if (node->onChange)
                    node->onChange(node->value);
            }

            namespace
            {
                /* Move byte index left by one full codepoint. */
                int prevCodepoint(const std::string &s, int byteIndex)
                {
                    if (byteIndex <= 0)
                        return 0;
                    byteIndex--;
                    while (byteIndex > 0 &&
                           (((unsigned char)s[byteIndex] & 0xC0u) == 0x80u))
                    {
                        byteIndex--;
                    }
                    return byteIndex;
                }
                int nextCodepoint(const std::string &s, int byteIndex)
                {
                    int n = (int)s.size();
                    if (byteIndex >= n)
                        return n;
                    byteIndex++;
                    while (byteIndex < n &&
                           (((unsigned char)s[byteIndex] & 0xC0u) == 0x80u))
                    {
                        byteIndex++;
                    }
                    return byteIndex;
                }
            }

            namespace
            {
                /* Selection helpers. selStart..selEnd may be in either
                 * order; lo/hi normalise. */
                void getSelectionRange(Node *raw, int &lo, int &hi)
                {
                    auto it = s_selectionMap.find(raw);
                    if (it == s_selectionMap.end())
                    {
                        lo = hi = 0;
                        return;
                    }
                    lo = std::min(it->second.start, it->second.end);
                    hi = std::max(it->second.start, it->second.end);
                }
                bool hasSelection(Node *raw)
                {
                    int lo, hi;
                    getSelectionRange(raw, lo, hi);
                    return hi > lo;
                }
                void clearSelection(Node *raw)
                {
                    s_selectionMap.erase(raw);
                }
                /* Delete the selected range and move caret to its
                 * start. Returns true if anything was deleted. */
                bool deleteSelection(std::shared_ptr<Node> node)
                {
                    int lo, hi;
                    getSelectionRange(node.get(), lo, hi);
                    if (hi <= lo)
                        return false;
                    node->value.erase(lo, hi - lo);
                    s_caretMap[node.get()] = lo;
                    s_blinkMap[node.get()] = 0.0f;
                    clearSelection(node.get());
                    return true;
                }
                /* Extend selection. anchor stays put, head follows the
                 * caret. If no selection exists yet, both anchor and
                 * head start at the current caret. */
                void extendSelection(Node *raw, int from, int to)
                {
                    auto it = s_selectionMap.find(raw);
                    if (it == s_selectionMap.end())
                    {
                        s_selectionMap[raw] = Selection{from, to};
                    }
                    else
                    {
                        it->second.end = to;
                    }
                }
            }

            bool TextInput::handleKey(std::shared_ptr<Node> node, int sdlScancode)
            {
                if (!node)
                    return false;
                int caret = getCaret(node);
                bool changed = false;
                bool consumed = true;

                /* Modifier state — needed for Cmd/Ctrl + A/C/V/X and
                 * for shift-extends-selection on the arrow keys. SDL's
                 * GUI modifier maps to ⌘ on macOS and the Windows key
                 * elsewhere; we treat Ctrl + the same letters as a
                 * fallback so Linux/Windows users get the same shortcuts. */
                SDL_Keymod mods = SDL_GetModState();
                const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                const bool cmd = (mods & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;

                /* Clipboard / select-all shortcuts. These short-circuit
                 * the normal handleKey switch so the user can hit them
                 * regardless of caret position. */
                if (cmd)
                {
                    if (sdlScancode == SDL_SCANCODE_A)
                    {
                        s_selectionMap[node.get()] =
                            Selection{0, (int)node->value.size()};
                        return true;
                    }
                    if (sdlScancode == SDL_SCANCODE_C ||
                        sdlScancode == SDL_SCANCODE_X)
                    {
                        int lo, hi;
                        getSelectionRange(node.get(), lo, hi);
                        if (hi > lo)
                        {
                            std::string sel = node->value.substr(lo, hi - lo);
                            SDL_SetClipboardText(sel.c_str());
                            if (sdlScancode == SDL_SCANCODE_X &&
                                !node->style.disabled)
                            {
                                deleteSelection(node);
                                changed = true;
                            }
                        }
                        if (changed)
                        {
                            node->textContent = node->value;
                            if (node->onChange)
                                node->onChange(node->value);
                        }
                        return true;
                    }
                    if (sdlScancode == SDL_SCANCODE_V)
                    {
                        if (node->style.disabled)
                            return true;
                        char *clip = SDL_GetClipboardText();
                        if (clip && clip[0])
                        {
                            /* Replace any selection with the pasted
                             * text — same as native textareas. */
                            deleteSelection(node);
                            insertText(node, clip);
                        }
                        if (clip)
                            SDL_free(clip);
                        return true;
                    }
                }

                /* Anchor for shift-arrows. Without an existing selection
                 * the anchor IS the current caret, so a shift+arrow
                 * starts a fresh selection. */
                int selAnchor = caret;
                {
                    auto it = s_selectionMap.find(node.get());
                    if (it != s_selectionMap.end())
                        selAnchor = it->second.start;
                }

                switch (sdlScancode)
                {
                case SDL_SCANCODE_BACKSPACE:
                    if (deleteSelection(node))
                    {
                        changed = true;
                        break;
                    }
                    if (caret > 0)
                    {
                        int prev = prevCodepoint(node->value, caret);
                        node->value.erase(prev, caret - prev);
                        setCaret(node, prev);
                        changed = true;
                    }
                    break;
                case SDL_SCANCODE_DELETE:
                    if (deleteSelection(node))
                    {
                        changed = true;
                        break;
                    }
                    if (caret < (int)node->value.size())
                    {
                        int next = nextCodepoint(node->value, caret);
                        node->value.erase(caret, next - caret);
                        changed = true;
                    }
                    break;
                case SDL_SCANCODE_LEFT:
                {
                    int newCaret = prevCodepoint(node->value, caret);
                    setCaret(node, newCaret);
                    if (shift)
                        extendSelection(node.get(), selAnchor, newCaret);
                    else
                        clearSelection(node.get());
                    break;
                }
                case SDL_SCANCODE_RIGHT:
                {
                    int newCaret = nextCodepoint(node->value, caret);
                    setCaret(node, newCaret);
                    if (shift)
                        extendSelection(node.get(), selAnchor, newCaret);
                    else
                        clearSelection(node.get());
                    break;
                }
                case SDL_SCANCODE_HOME:
                    setCaret(node, 0);
                    if (shift)
                        extendSelection(node.get(), selAnchor, 0);
                    else
                        clearSelection(node.get());
                    break;
                case SDL_SCANCODE_END:
                {
                    int newCaret = (int)node->value.size();
                    setCaret(node, newCaret);
                    if (shift)
                        extendSelection(node.get(), selAnchor, newCaret);
                    else
                        clearSelection(node.get());
                    break;
                }
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                    /* For multi-line inputs, Enter inserts a newline
                     * (so the user can author paragraphs). For single-
                     * line we treat it as form-submit and just fire
                     * onChange one more time. */
                    if (node->style.wordWrap && !node->style.disabled)
                    {
                        deleteSelection(node);
                        insertText(node, "\n");
                        return true;
                    }
                    if (node->onChange)
                        node->onChange(node->value);
                    break;
                default:
                    consumed = false;
                    break;
                }

                if (changed)
                {
                    node->textContent = node->value;
                    if (node->onChange)
                        node->onChange(node->value);
                }
                return consumed;
            }

            void TextInput::draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node,
                                 AssetPack *assets, int layer)
            {
                /* Background, border, shadow — exactly like a View. */
                View::draw(batcher, node, layer);

                /* If focused and the user has no border styled, draw a
                 * 1.5px focus ring. Skipping when the user already styled
                 * a border keeps their design intent intact. */
                if (node->isFocused && node->style.borderWidth == 0.0f)
                {
                    const float pad = 0.0f;
                    const auto corners = Drawing::resolveCorners(node->style,
                                                                 node->layout.w,
                                                                 node->layout.h);
                    const uint32_t ring = Drawing::applyTint(node->style.color, node->getGlobalOpacity());
                    const float widths[4] = {1.5f, 1.5f, 1.5f, 1.5f};
                    const uint32_t colors[4] = {ring, ring, ring, ring};
                    Drawing::strokeRoundedBorder(batcher,
                                                 node->layout.x - pad, node->layout.y - pad,
                                                 node->layout.w + pad * 2.0f,
                                                 node->layout.h + pad * 2.0f,
                                                 corners, widths, colors, ring,
                                                 layer + 1);
                }

                /* Render placeholder or value. We swap textContent
                 * temporarily so Label::draw does the work — same trick
                 * as computeNode. */
                std::string saved = node->textContent;
                uint32_t savedColor = node->style.color;
                if (node->value.empty())
                {
                    auto it = s_placeholderMap.find(node.get());
                    if (it != s_placeholderMap.end())
                    {
                        const_cast<std::string &>(node->textContent) = it->second;
                        node->style.color = Drawing::applyTint(savedColor, 0.5f);
                    }
                }
                else
                {
                    const_cast<std::string &>(node->textContent) = node->value;
                }

                /* Selection highlight: drawn BEFORE the text so the
                 * glyphs land on top of it. We compute the highlight
                 * rect(s) by replaying wrapLines (same algorithm as
                 * the caret math below). For multi-line selections
                 * we emit one rect per line spanned. */
                if (node->isFocused && hasSelection(node.get()) &&
                    !node->value.empty())
                {
                    int lo, hi;
                    getSelectionRange(node.get(), lo, hi);
                    const auto &sst = node->style;
                    const float spT = resolveSide(sst.paddingTop, sst.padding);
                    const float spL = resolveSide(sst.paddingLeft, sst.padding);
                    const float spR = resolveSide(sst.paddingRight, sst.padding);
                    const float sFontSize = sst.fontSize > 0.f ? sst.fontSize : 16.f;
                    TTF_Font *sf = Label::get_font(
                        sst.font.empty() ? "fonts/default.ttf" : sst.font,
                        sFontSize, assets);
                    if (sf)
                    {
                        const float sLineH = (float)TTF_GetFontHeight(sf) * (sst.lineHeight > 0.0f ? sst.lineHeight : 1.0f);
                        const uint32_t selColor =
                            Drawing::applyTint(0x6366F166, node->getGlobalOpacity()); // indigo @ 40%

                        auto measureSubstr = [&](const std::string &line, int start, int end)
                        {
                            if (start >= end)
                                return std::pair<int, int>{0, 0};
                            std::string left = line.substr(0, start);
                            std::string mid = line.substr(start, end - start);
                            int lw = 0, lh = 0, mw = 0, mh = 0;
                            if (!left.empty())
                                TTF_GetStringSize(sf, left.c_str(), 0, &lw, &lh);
                            if (!mid.empty())
                                TTF_GetStringSize(sf, mid.c_str(), 0, &mw, &mh);
                            return std::pair<int, int>{lw, mw};
                        };

                        if (sst.wordWrap)
                        {
                            const float contentW = std::max(0.0f, node->layout.w - spL - spR);
                            auto lines = Label::wrapLines(node->value, sf, contentW, sst.letterSpacing);
                            int byteSeen = 0;
                            for (size_t li = 0; li < lines.size(); li++)
                            {
                                int lineLen = (int)lines[li].size();
                                int lineStart = byteSeen;
                                int lineEnd = byteSeen + lineLen;
                                /* Intersect this line with [lo, hi). */
                                int sStart = std::max(lo, lineStart) - lineStart;
                                int sEnd = std::min(hi, lineEnd) - lineStart;
                                if (sStart < sEnd && sStart >= 0)
                                {
                                    auto [leftW, midW] = measureSubstr(lines[li], sStart, sEnd);
                                    float rx = node->layout.x + spL + (float)leftW;
                                    float ry = node->layout.y + spT + (float)li * sLineH;
                                    batcher.drawRect(rx, ry, (float)midW, sLineH,
                                                     selColor, layer + 1);
                                }
                                byteSeen += lineLen + 1; // +1 for consumed separator
                            }
                        }
                        else
                        {
                            auto [leftW, midW] = measureSubstr(node->value, lo, hi);
                            float rx = node->layout.x + spL + (float)leftW;
                            float ry = node->layout.y + spT;
                            batcher.drawRect(rx, ry, (float)midW, sLineH,
                                             selColor, layer + 1);
                        }
                    }
                }

                Label::draw(batcher, node, assets, layer + 2);
                const_cast<std::string &>(node->textContent) = saved;
                node->style.color = savedColor;

                /* Resize grip: a small triangular ladder in the bottom-
                 * right corner when style.resize is true. Three short
                 * diagonal lines — same affordance as macOS textareas
                 * and most browser <textarea>s. Drawn even when
                 * unfocused so it reads as "this can be resized". */
                if (node->style.resize)
                {
                    const float gripPad = 4.0f;
                    const float gripSize = 12.0f;
                    const float gx = node->layout.x + node->layout.w - gripPad - gripSize;
                    const float gy = node->layout.y + node->layout.h - gripPad - gripSize;
                    const uint32_t gripColor =
                        Drawing::applyTint(0x94A3B8FF, node->getGlobalOpacity());
                    /* Three diagonal ticks marching from inner to outer. */
                    for (int i = 0; i < 3; i++)
                    {
                        float off = (float)i * 4.0f;
                        float x0 = gx + gripSize - off;
                        float y0 = gy + gripSize;
                        float x1 = gx + gripSize;
                        float y1 = gy + gripSize - off;
                        /* Drawn as a 2px tall solid quad rotated 45° — we
                         * approximate by drawing two 1px squares along
                         * the line. Keeps it batched in SolidRect mode
                         * with no extra geometry path. */
                        const float steps = std::max(2.0f, off);
                        for (int k = 0; k <= (int)steps; k++)
                        {
                            float t = (float)k / steps;
                            float px = x0 + (x1 - x0) * t;
                            float py = y0 + (y1 - y0) * t;
                            batcher.drawRect(std::round(px), std::round(py), 1.5f, 1.5f,
                                             gripColor, layer + 3);
                        }
                    }
                }

                /* Caret: only when focused. Half-second blink on/off. */
                if (!node->isFocused)
                    return;

                float blink = 0.0f;
                auto bit = s_blinkMap.find(node.get());
                if (bit != s_blinkMap.end())
                    blink = bit->second;
                /* blink in [0..1] is the timer; we treat <0.5 as "show". */
                if (blink >= 0.5f)
                    return;

                const auto &s = node->style;
                const float pT = resolveSide(s.paddingTop, s.padding);
                const float pL = resolveSide(s.paddingLeft, s.padding);
                const float pR = resolveSide(s.paddingRight, s.padding);
                const float pB = resolveSide(s.paddingBottom, s.padding);
                const float fSize = s.fontSize > 0.f ? s.fontSize : 16.f;
                TTF_Font *font = Label::get_font(s.font.empty() ? "fonts/default.ttf" : s.font, fSize, assets);
                if (!font)
                    return;

                int caret = getCaret(node);
                const float lineH = (float)TTF_GetFontHeight(font) * (s.lineHeight > 0.0f ? s.lineHeight : 1.0f);
                const float caretH = std::max(2.0f, lineH);

                float caretX, caretY;
                if (s.wordWrap)
                {
                    /* Multi-line caret. We replay Label's wrapLines
                     * with the same content-width to find which line
                     * the caret falls on, then measure the partial
                     * head of that line for the within-line X.
                     *
                     * Edge case: caret exactly at a soft-wrap boundary
                     * is ambiguous (end of line N or start of line N+1).
                     * We resolve to "end of line N" — matches what most
                     * editors do; pressing End on the wrapped line
                     * lands here, pressing Down moves to N+1. */
                    const float contentW = std::max(0.0f, node->layout.w - pL - pR);
                    auto lines = Label::wrapLines(node->value, font, contentW, s.letterSpacing);
                    int byteSeen = 0;
                    int lineIdx = 0;
                    int caretInLine = caret;
                    for (size_t i = 0; i < lines.size(); i++)
                    {
                        int lineLen = (int)lines[i].size();
                        /* wrapLines may have eaten an inter-line space; the
                         * canonical mapping caret-byte → (line, offset) is
                         * approximate but good enough for visual feedback
                         * since we re-derive it on every draw. */
                        if (byteSeen + lineLen >= caret)
                        {
                            lineIdx = (int)i;
                            caretInLine = caret - byteSeen;
                            break;
                        }
                        byteSeen += lineLen + 1; // +1 for the consumed space/newline
                        lineIdx = (int)i + 1;
                        caretInLine = caret - byteSeen;
                    }
                    if (caretInLine < 0)
                        caretInLine = 0;
                    std::string head = (lineIdx < (int)lines.size())
                                           ? lines[lineIdx].substr(0, std::min(caretInLine, (int)lines[lineIdx].size()))
                                           : std::string();
                    int textW = 0, textH = 0;
                    if (!head.empty())
                        TTF_GetStringSize(font, head.c_str(), 0, &textW, &textH);
                    caretX = std::round(node->layout.x + pL + (float)textW);
                    caretY = node->layout.y + pT + (float)lineIdx * lineH;
                }
                else
                {
                    /* Single-line caret. */
                    std::string head = node->value.substr(0, caret);
                    int textW = 0, textH = 0;
                    if (!head.empty())
                        TTF_GetStringSize(font, head.c_str(), 0, &textW, &textH);
                    caretX = std::round(node->layout.x + pL + (float)textW);
                    caretY = node->layout.y + pT;
                }

                /* Clamp caret X to the content rect — long values
                 * truncate visually for now (no horizontal scroll yet). */
                const float maxX = node->layout.x + node->layout.w - pR - 1.0f;
                caretX = std::min(caretX, maxX);

                const uint32_t caretRgba = Drawing::applyTint(s.color, node->getGlobalOpacity());
                batcher.drawRect(caretX, caretY, 1.5f, caretH, caretRgba, layer + 3);
            }
        }
    }
}
