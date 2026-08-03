#include "display_renderer.h"

#include "capture_engine.h"
#include "hw_pins.h"

#include <cstdio>
#include <cmath>
#include <cstring>

namespace perseids
{

namespace
{
constexpr int kMargin        = 2;
constexpr int kBarWide       = 14;
constexpr int kBarNarrow     = 6;
constexpr int kBarFar        = 3; // carousel rim only
// Catch-up: horizontal stubs only, 1px gap from the value bar (no vertical ticks).
constexpr int kPickupOverhang = 5;
constexpr int kPickupBarGap   = 1;
constexpr int kModDotOutset   = 2;

// Rim-only shrink (~half height). Neighbors left/right of center stay full size.
constexpr float kDepthFar = 0.55f;
// Segment cell for outer peeks — top-aligned with the full row; one pixel
// taller than the glyph band so the label isn't glued to the bottom edge.
constexpr int kRimSegH = 9;

float CarouselDepth(size_t slot, size_t page)
{
    if(page != DisplayRenderer::kCyclePageCols)
        return 1.f;
    if(slot == 0 || slot == page - 1)
        return kDepthFar;
    return 1.f; // center + both neighbors: full size
}

void RimSegGeom(int& out_y, int& out_h)
{
    // Top-align with the full segment row so the upper box edge reads as one
    // continuous horizontal line across the display.
    out_h = kRimSegH;
    out_y = DisplayRenderer::kSegRowY;
}

void ParamBand(float depth, int& out_top, int& out_base)
{
    const int full_top  = DisplayRenderer::kCeilingY + 1;
    const int full_base = DisplayRenderer::kSegRowY - 1;

    // Full columns: bars sit on the segment row (no floating).
    if(depth >= 0.99f)
    {
        out_top  = full_top;
        out_base = full_base;
        return;
    }

    // Rim: shorter bars that rest on the vertically-centered outer box.
    int rim_y = 0;
    int rim_h = 0;
    RimSegGeom(rim_y, rim_h);
    out_base = rim_y - 1;
    const int full_span = full_base - full_top;
    int       span
        = static_cast<int>(static_cast<float>(full_span) * depth + 0.5f);
    if(span < 4)
        span = 4;
    out_top = out_base - span;
    if(out_top < full_top)
        out_top = full_top;
}

int BarWidthFor(bool active, float depth)
{
    if(depth < 0.99f)
        return kBarFar;
    return active ? kBarWide : kBarNarrow;
}
} // namespace

void DisplayRenderer::Init(daisy::DaisySeed& seed)
{
    (void)seed;
    daisy::OledDisplay<daisy::SSD130x4WireSpi128x64Driver>::Config cfg;
    cfg.driver_config.transport_config.spi_config.pin_config.sclk
        = hw::kOledSck;
    cfg.driver_config.transport_config.spi_config.pin_config.mosi
        = hw::kOledMosi;
    cfg.driver_config.transport_config.spi_config.pin_config.nss = hw::kOledCs;
    cfg.driver_config.transport_config.pin_config.dc             = hw::kOledDc;
    cfg.driver_config.transport_config.pin_config.reset          = hw::kOledReset;
    display_.Init(cfg);
    display_.Fill(false);
    display_.Update();
}

DisplayRenderer::ColumnGeom DisplayRenderer::ColumnGeometry(size_t index,
                                                            size_t count) const
{
    const int total_w = kWidth - 2 * kMargin;

    // Single param: one standard-width column at screen center.
    if(count == 1)
    {
        const int col_w = total_w / 4;
        const int x     = (kWidth - col_w) / 2;
        return {x, col_w, x + col_w / 2};
    }

    // Five slots: half | full | center | full | half (sum = 4 full widths).
    // Center of slot 2 lands exactly on kWidth/2.
    if(count == kCyclePageCols)
    {
        constexpr int kUnits[kCyclePageCols] = {1, 2, 2, 2, 1};
        const int     unit = total_w / 8;
        const int     used = unit * 8;
        int           x    = kMargin + (total_w - used) / 2;
        if(index >= kCyclePageCols)
            index = kCyclePageCols - 1;
        for(size_t i = 0; i < index; ++i)
            x += kUnits[i] * unit;
        const int w = kUnits[index] * unit;
        return {x, w, x + w / 2};
    }

    // Fallback: equal columns sized as a 4-slot page.
    const size_t slots
        = (count > 0 && count < 4) ? 4 : count;
    const int col_w = static_cast<int>(total_w / static_cast<int>(slots));
    const int x     = kMargin + static_cast<int>(index) * col_w;
    return {x, col_w, x + col_w / 2};
}

void DisplayRenderer::CycleWindow(size_t  param_count,
                                  size_t  active_col,
                                  size_t* out_indices,
                                  size_t& out_page)
{
    if(out_indices == nullptr || param_count == 0)
    {
        out_page = 0;
        return;
    }

    const size_t act
        = (active_col < param_count) ? active_col : (param_count - 1);

    // One entry: single centered column.
    if(param_count == 1)
    {
        out_page       = 1;
        out_indices[0] = 0;
        return;
    }

    // Always 5 visual slots with wrap — active fixed in the true center slot.
    out_page           = kCyclePageCols;
    const size_t focus = kCycleFocusSlot;
    for(size_t v = 0; v < out_page; ++v)
    {
        const size_t offset = v + param_count - focus;
        out_indices[v]      = (act + offset) % param_count;
    }
}

void DisplayRenderer::Clear()
{
    display_.Fill(false);
}

void DisplayRenderer::Present()
{
    display_.Update();
}

void DisplayRenderer::DrawCeilingLine()
{
    display_.DrawLine(0, kCeilingY, kWidth - 1, kCeilingY, true);
}

void DisplayRenderer::DrawColumnSides(const ColumnGeom& col, bool active)
{
    if(!active)
        return;
    display_.DrawLine(col.x, kCeilingY, col.x, kSegRowY + kSegRowH - 1, true);
    display_.DrawLine(col.x + col.w - 1,
                      kCeilingY,
                      col.x + col.w - 1,
                      kSegRowY + kSegRowH - 1,
                      true);
}

void DisplayRenderer::DrawDashedCenterLine(const ColumnGeom& col,
                                           bool              full_width,
                                           float             depth)
{
    int top = 0;
    int base = 0;
    ParamBand(depth, top, base);
    const int y     = (top + base) / 2;
    const int inset = full_width ? 1 : col.w / 4;
    for(int x = col.x + inset; x < col.x + col.w - inset; x += 2)
        display_.DrawPixel(x, y, true);
}

// 50% hint for crossfade-style unipolar params (e.g. Blend): one dot per
// side at half bar height, equal gap to the bar — deliberately subtler than
// the bipolar dashed zero line (the middle is an equal mix, not "no effect").
void DisplayRenderer::DrawCenterMark(const ColumnGeom& col,
                                     bool              active,
                                     float             depth)
{
    int top = 0;
    int base = 0;
    ParamBand(depth, top, base);
    const int y     = (top + base) / 2;
    const int bar_w = BarWidthFor(active, depth);
    const int bar_x = col.cx - bar_w / 2;
    constexpr int kGap = 2;

    display_.DrawPixel(bar_x - kGap, y, true);
    display_.DrawPixel(bar_x + bar_w - 1 + kGap, y, true);
}

void DisplayRenderer::DrawUnipolarBar(const ColumnGeom& col,
                                      float             norm,
                                      bool              active,
                                      float             depth)
{
    const int bar_w  = BarWidthFor(active, depth);
    const int bar_x  = col.cx - bar_w / 2;
    int       top_y  = 0;
    int       base_y = 0;
    ParamBand(depth, top_y, base_y);
    const int span = base_y - top_y;
    int       h    = static_cast<int>(norm * static_cast<float>(span) + 0.5f);

    // Active at 0%: still show a 1px floor so the column doesn't look empty (e.g. ENS).
    if(h <= 0)
    {
        if(!active)
            return;
        h = 1;
    }

    display_.DrawRect(bar_x, base_y - h, bar_x + bar_w - 1, base_y, true, true);
}

void DisplayRenderer::DrawBipolarBar(const ColumnGeom& col,
                                     float             norm,
                                     bool              active,
                                     float             depth)
{
    const int bar_w  = BarWidthFor(active, depth);
    const int bar_x  = col.cx - bar_w / 2;
    int       top_y  = 0;
    int       base_y = 0;
    ParamBand(depth, top_y, base_y);
    const int center    = (top_y + base_y) / 2;
    const int up_span   = center - top_y;
    const int down_span = base_y - center;

    const float signed_v = (norm - 0.5f) * 2.f;

    if(signed_v >= 0.f)
    {
        const int h = static_cast<int>(signed_v * static_cast<float>(up_span) + 0.5f);
        if(h > 0)
            display_.DrawRect(bar_x, center - h, bar_x + bar_w - 1, center, true, true);
    }
    else
    {
        const int h
            = static_cast<int>(-signed_v * static_cast<float>(down_span) + 0.5f);
        if(h > 0)
            display_.DrawRect(bar_x, center, bar_x + bar_w - 1, center + h, true, true);
    }
}

void DisplayRenderer::DrawToggle(const ColumnGeom&   col,
                                 const ParameterDef& def,
                                 bool                active,
                                 float               depth)
{
    (void)active;

    const bool on = *def.value_ptr >= (def.min_val + def.max_val) * 0.5f;

    int top_y  = 0;
    int base_y = 0;
    ParamBand(depth, top_y, base_y);

    // Far rim: tiny O/I peek — full OFF/ON will not fit.
    if(depth < 0.99f)
    {
        const char* mark = on ? "I" : "O";
        const int   tw   = 4;
        display_.SetCursor(col.cx - tw / 2, (top_y + base_y) / 2 - 3);
        display_.WriteString(mark, Font_4x6, true);
        return;
    }

    const int x0  = col.x + 2;
    const int x1  = col.x + col.w - 3;
    const int y0  = top_y;
    const int y1  = base_y;
    const int mid = (y0 + y1) / 2;

    constexpr int kOffW  = 3 * 5; // Font_5x8 "OFF"
    constexpr int kOnW   = 2 * 5; // Font_5x8 "ON"
    constexpr int kFontH = 8;

    const int off_x = col.cx - kOffW / 2;
    const int on_x  = col.cx - kOnW / 2;
    const int off_y = y0 + (mid - y0 - kFontH) / 2;
    const int on_y  = mid + (y1 - mid + 1 - kFontH) / 2;

    if(!on)
    {
        display_.DrawRect(x0, y0, x1, mid - 1, true, true);
        display_.SetCursor(off_x, off_y);
        display_.WriteString("OFF", Font_5x8, false);
        display_.SetCursor(on_x, on_y);
        display_.WriteString("ON", Font_5x8, true);
    }
    else
    {
        display_.DrawRect(x0, mid, x1, y1, true, true);
        display_.SetCursor(off_x, off_y);
        display_.WriteString("OFF", Font_5x8, true);
        display_.SetCursor(on_x, on_y);
        display_.WriteString("ON", Font_5x8, false);
    }
}

void DisplayRenderer::DrawCountBar(const ColumnGeom&   col,
                                   const ParameterDef& def,
                                   bool                active,
                                   float               depth)
{
    const float norm = ParameterRegistry::Normalize(def, *def.value_ptr);
    DrawUnipolarBar(col, norm, active, depth);
}

void DisplayRenderer::DrawCountNum(const ColumnGeom&   col,
                                   const ParameterDef& def,
                                   bool                active,
                                   float               depth)
{
    char val[8];
    FormatCountLabel(def, val, sizeof(val));

    int top_y  = 0;
    int base_y = 0;
    ParamBand(depth, top_y, base_y);
    const int mid_y = (top_y + base_y) / 2;

    // Outer rim: 1–2 glyphs, small font, centered in the short band.
    if(depth < 0.99f)
    {
        char peek[3] = {0, 0, 0};
        peek[0]      = val[0];
        if(val[1] != '\0')
            peek[1] = val[1];
        const int tw = static_cast<int>(strlen(peek)) * 4;
        display_.SetCursor(col.cx - tw / 2, mid_y - 3);
        display_.WriteString(peek, Font_4x6, true);
        return;
    }

    if(active)
    {
        const int font_w = 7;
        const int text_w = static_cast<int>(strlen(val)) * font_w;
        display_.SetCursor(col.cx - text_w / 2, mid_y - 5);
        display_.WriteString(val, Font_7x10, true);
    }
    else
    {
        const int font_w = 6;
        const int text_w = static_cast<int>(strlen(val)) * font_w;
        display_.SetCursor(col.cx - text_w / 2, kSegRowY - 9);
        display_.WriteString(val, Font_6x8, true);
    }
}

void DisplayRenderer::FormatCountLabel(const ParameterDef& def,
                                       char*               out,
                                       size_t              out_len) const
{
    const int v   = static_cast<int>(*def.value_ptr + 0.5f);
    const int min = static_cast<int>(def.min_val + 0.5f);
    const int max = static_cast<int>(def.max_val + 0.5f);

    if(def.enum_labels != nullptr && max >= min)
    {
        int idx = v - min;
        if(idx < 0)
            idx = 0;
        if(idx > max - min)
            idx = max - min;
        if(def.enum_labels[idx] != nullptr)
        {
            snprintf(out, out_len, "%s", def.enum_labels[idx]);
            return;
        }
    }
    snprintf(out, out_len, "%d", v);
}

void DisplayRenderer::FormatValue(const ParameterDef& def,
                                  char*               out,
                                  size_t              out_len) const
{
    switch(def.display_type)
    {
    case ParamDisplayType::Unipolar:
    {
        const int pct = static_cast<int>(
            ParameterRegistry::Normalize(def, *def.value_ptr) * 100.f + 0.5f);
        snprintf(out, out_len, "%d%%", pct);
        break;
    }
    case ParamDisplayType::Bipolar:
    {
        float signed_v
            = (ParameterRegistry::Normalize(def, *def.value_ptr) - 0.5f) * 200.f;
        // Engines Pitch Both: expand displayed span ±100% → ±200% with the pot.
        if(def.bipolar_span_ptr != nullptr)
        {
            float s = *def.bipolar_span_ptr;
            if(s < 0.f)
                s = 0.f;
            if(s > 1.f)
                s = 1.f;
            signed_v *= (1.f + s);
        }
        const int rounded = static_cast<int>(
            signed_v >= 0.f ? signed_v + 0.5f : signed_v - 0.5f);
        snprintf(out, out_len, "%+d%%", rounded);
        break;
    }
    case ParamDisplayType::Toggle:
        snprintf(out,
                 out_len,
                 "%s",
                 (*def.value_ptr >= (def.min_val + def.max_val) * 0.5f) ? "ON"
                                                                          : "OFF");
        break;
    case ParamDisplayType::CountBar:
    case ParamDisplayType::CountNum:
        FormatCountLabel(def, out, out_len);
        break;
    case ParamDisplayType::Seconds:
    {
        // Integer formatting — newlib-nano often omits %f support (would print only "s").
        float s = *def.value_ptr;
        if(s < 0.f)
            s = 0.f;
        if(s < 10.f)
        {
            const int hundredths = static_cast<int>(s * 100.f + 0.5f);
            snprintf(out,
                     out_len,
                     "%d.%02ds",
                     hundredths / 100,
                     hundredths % 100);
        }
        else
        {
            const int tenths = static_cast<int>(s * 10.f + 0.5f);
            snprintf(out, out_len, "%d.%ds", tenths / 10, tenths % 10);
        }
        break;
    }
    case ParamDisplayType::HoldTime:
    {
        float s = *def.value_ptr;
        if(s > 30.f)
        {
            snprintf(out, out_len, "INF");
        }
        else if(s < 10.f)
        {
            if(s < 0.f)
                s = 0.f;
            const int tenths = static_cast<int>(s * 10.f + 0.5f);
            snprintf(out, out_len, "%d.%ds", tenths / 10, tenths % 10);
        }
        else
        {
            snprintf(out, out_len, "%ds", static_cast<int>(s + 0.5f));
        }
        break;
    }
    }
}

void DisplayRenderer::FormatPosition(size_t index,
                                     size_t count,
                                     char*  out,
                                     size_t out_len) const
{
    snprintf(out, out_len, "%u/%u", static_cast<unsigned>(index + 1), static_cast<unsigned>(count));
}

void DisplayRenderer::DrawValueHeader(const ParameterRegistry& reg,
                                      const CycleRow&          row,
                                      size_t                   active_col,
                                      bool                     show_cpu_meter,
                                      float                    cpu_load,
                                      bool                     governor)
{
    char pos[8];
    FormatPosition(active_col, row.ParamCount(), pos, sizeof(pos));

    const char* name = row.BlockName();
    display_.SetCursor(0, kHeaderY);
    display_.WriteString(name, Font_6x8, true);
    const int name_w = static_cast<int>(strlen(name)) * 6;

    constexpr int kGlyphW = 4;
    constexpr int kGap    = 2;
    const int     pos_w   = static_cast<int>(strlen(pos)) * kGlyphW;
    int           right_edge = kWidth - pos_w;

    if(show_cpu_meter || governor)
    {
        float load = cpu_load;
        if(load < 0.f)
            load = 0.f;
        if(load > 9.99f)
            load = 9.99f;
        int pct = static_cast<int>(load * 100.f + 0.5f);
        if(pct < 0)
            pct = 0;
        if(pct > 999)
            pct = 999;

        char cpu[8];
        if(!show_cpu_meter)
            snprintf(cpu, sizeof(cpu), "L!");
        else if(governor)
        {
            // Governor marker replaces the trailing '%'.
            snprintf(cpu, sizeof(cpu), "L!C%u", static_cast<unsigned>(pct));
        }
        else
            snprintf(cpu, sizeof(cpu), "C%u%%", static_cast<unsigned>(pct));
        const int cpu_w = static_cast<int>(strlen(cpu)) * kGlyphW;
        right_edge -= kGap + cpu_w;
        display_.SetCursor(right_edge, kHeaderY + 1);
        display_.WriteString(cpu, Font_4x6, true);
    }

    display_.SetCursor(kWidth - pos_w, kHeaderY + 1);
    display_.WriteString(pos, Font_4x6, true);

    const ParameterDef* def = row.ParamAt(reg, active_col);
    if(def == nullptr || def->display_type == ParamDisplayType::Toggle)
        return;

    char val[12];
    FormatValue(*def, val, sizeof(val));

    // Named-pole side hint (Blend SP/SW, Umbra/Aurora, …): value header,
    // Font_4x6. Below 50% → low label before the number; above → high label
    // after; nothing at exact center. Pole name replaces ±; '%' is omitted
    // while a hint is showing so the right-hand label cannot overwrite it
    // (Pitch / Velocity and other plain ±% params are unchanged).
    const char* side_hint   = nullptr;
    bool        side_before = false;
    if(def->seg_hint_low != nullptr && def->seg_hint_high != nullptr)
    {
        const float n
            = ParameterRegistry::Normalize(*def, *def->value_ptr);
        const int pct = static_cast<int>(n * 100.f + 0.5f);
        if(pct < 50)
        {
            side_hint   = def->seg_hint_low;
            side_before = true;
        }
        else if(pct > 50)
        {
            side_hint   = def->seg_hint_high;
            side_before = false;
        }
    }

    char num[12];
    {
        const char* src = val;
        if(src[0] == '+' || src[0] == '-')
            ++src;
        snprintf(num, sizeof(num), "%s", src);
        if(side_hint != nullptr)
        {
            const size_t nlen = strlen(num);
            if(nlen > 0 && num[nlen - 1] == '%')
                num[nlen - 1] = '\0';
        }
    }

    const int hint_w
        = (side_hint != nullptr) ? static_cast<int>(strlen(side_hint)) * kGlyphW
                                 : 0;
    const int hint_gap = (hint_w > 0) ? 1 : 0;

    // Long block names (Resonator / Crossfade / Pan Drift = 9×6 = 54 px)
    // collide with a centred bipolar "+100%". Draw the sign in Font_4x6 and
    // clamp the value so it never eats into the title or the right chrome.
    const bool small_sign = def->display_type == ParamDisplayType::Bipolar
                            && (val[0] == '+' || val[0] == '-')
                            && side_hint == nullptr;
    const int sign_w = small_sign ? kGlyphW : 0;
    const int body_w = static_cast<int>(strlen(num)) * 6;
    const int text_w = hint_w + hint_gap + sign_w + body_w;

    int x = (kWidth - text_w) / 2;
    if(x < name_w + kGap)
        x = name_w + kGap;
    if(x + text_w > right_edge - kGap)
        x = right_edge - kGap - text_w;

    int cursor = x;
    if(side_hint != nullptr && side_before)
    {
        display_.SetCursor(cursor, kHeaderY + 1);
        display_.WriteString(side_hint, Font_4x6, true);
        cursor += hint_w + hint_gap;
    }
    if(small_sign)
    {
        char sign[2] = {val[0], '\0'};
        display_.SetCursor(cursor, kHeaderY + 1);
        display_.WriteString(sign, Font_4x6, true);
        cursor += sign_w;
    }
    display_.SetCursor(cursor, kHeaderY);
    display_.WriteString(num, Font_6x8, true);
    cursor += body_w;
    if(side_hint != nullptr && !side_before)
    {
        display_.SetCursor(cursor + hint_gap, kHeaderY + 1);
        display_.WriteString(side_hint, Font_4x6, true);
    }
}

void DisplayRenderer::DrawPickupLine(const ColumnGeom& col,
                                     float             pot_norm,
                                     ParamDisplayType  type)
{
    // Physical pot 0…1 → bottom→top. Gap the line around the value bar (4.6).
    (void)type;
    const int base_y = kSegRowY - 1;
    const int top_y  = kCeilingY + 1;
    const int span   = base_y - top_y;
    float     n      = pot_norm;
    if(n < 0.f)
        n = 0.f;
    if(n > 1.f)
        n = 1.f;
    const int y = base_y - static_cast<int>(n * static_cast<float>(span) + 0.5f);

    const int bar_half = kBarWide / 2;
    const int bar_l    = col.cx - bar_half;
    const int bar_r    = col.cx + bar_half - 1;

    int outer_l = col.cx - bar_half - kPickupOverhang;
    int outer_r = col.cx + bar_half + kPickupOverhang - 1;
    if(outer_l < col.x + 1)
        outer_l = col.x + 1;
    if(outer_r > col.x + col.w - 2)
        outer_r = col.x + col.w - 2;

    // Left stub: … up to 1px before the value bar.
    const int left_end = bar_l - kPickupBarGap - 1;
    if(left_end >= outer_l)
        display_.DrawLine(outer_l, y, left_end, y, true);

    // Right stub: from 1px after the value bar …
    const int right_start = bar_r + kPickupBarGap + 1;
    if(right_start <= outer_r)
        display_.DrawLine(right_start, y, outer_r, y, true);
}

void DisplayRenderer::DrawModDots(const ColumnGeom& col,
                                  float             mod_norm,
                                  ParamDisplayType  type)
{
    const int base_y = kSegRowY - 1;
    const int top_y  = kCeilingY + 1;
    const int center = (kParamTop + kParamBottom) / 2;

    int y = base_y;
    if(type == ParamDisplayType::Bipolar)
    {
        const float signed_v = (mod_norm - 0.5f) * 2.f;
        if(signed_v >= 0.f)
            y = center - static_cast<int>(signed_v * static_cast<float>(center - top_y));
        else
            y = center
                + static_cast<int>(-signed_v * static_cast<float>(base_y - center));
    }
    else
    {
        y = base_y - static_cast<int>(mod_norm * static_cast<float>(base_y - top_y));
    }

    // Outside the catch-up line's reach (bar/2 + overhang + outset).
    const int half = kBarWide / 2 + kPickupOverhang + kModDotOutset;
    const int x0   = col.cx - half;
    const int x1   = col.cx + half;

    display_.DrawPixel(x0, y, true);
    display_.DrawPixel(x0 - 2, y, true);
    display_.DrawPixel(x1, y, true);
    display_.DrawPixel(x1 + 2, y, true);
}

void DisplayRenderer::DrawSegmentedRow(const ParameterRegistry& reg,
                                       const CycleRow&          row,
                                       size_t                   active_col)
{
    size_t indices[kCyclePageCols];
    size_t page = 0;
    CycleWindow(row.ParamCount(), active_col, indices, page);

    for(size_t v = 0; v < page; ++v)
    {
        const size_t        i   = indices[v];
        const ColumnGeom    col = ColumnGeometry(v, page);
        const ParameterDef* def = row.ParamAt(reg, i);
        if(def == nullptr)
            continue;

        const bool sel       = (i == active_col);
        const int  pad       = 1;
        const bool half_edge = (page == kCyclePageCols
                                && (v == 0 || v == page - 1));

        // Outer peeks: shorter box, top-aligned; neighbors + center: full size.
        int seg_y = kSegRowY;
        int seg_h = kSegRowH;
        if(half_edge)
            RimSegGeom(seg_y, seg_h);

        int x0 = col.x + pad;
        int x1 = col.x + col.w - pad - 1;
        // Abut outer boxes to their neighbors — no gap between rim and next.
        if(half_edge && v == 0 && page > 1)
        {
            const ColumnGeom next = ColumnGeometry(1, page);
            x1 = next.x + pad; // share neighbor's left wall
        }
        else if(half_edge && v == page - 1 && page > 1)
        {
            const ColumnGeom prev = ColumnGeometry(page - 2, page);
            x0 = prev.x + prev.w - pad - 1; // share neighbor's right wall
        }

        char peek[3] = {0, 0, 0};
        if(half_edge && def->abbrev != nullptr && def->abbrev[0] != '\0')
        {
            peek[0] = def->abbrev[0];
            if(def->abbrev[1] != '\0')
                peek[1] = def->abbrev[1];
        }

        display_.DrawRect(x0, seg_y, x1, seg_y + seg_h - 1, true, sel);

        if(half_edge)
        {
            if(peek[0] != '\0')
            {
                // Center 1–2 glyphs inside the rim box (not sitting on the bottom).
                const int tw = static_cast<int>(strlen(peek)) * 4;
                const int cx = (x0 + x1) / 2;
                int       tx = cx - tw / 2;
                if(tx < x0 + 1)
                    tx = x0 + 1;
                const int ty = seg_y + (seg_h - 6) / 2;
                display_.SetCursor(tx, ty);
                display_.WriteString(peek, Font_4x6, !sel);
            }
        }
        else
        {
            display_.SetCursor(col.x + pad + 2, seg_y + 2);
            display_.WriteString(def->abbrev, Font_6x8, !sel);
        }
    }
}

void DisplayRenderer::DrawCycleView(const ParameterRegistry& reg,
                                    const CycleRow&          row,
                                    size_t                   active_col,
                                    float                    modulated_norm,
                                    bool                     show_cpu_meter,
                                    float                    cpu_load,
                                    bool                     governor)
{
    Clear();
    DrawCeilingLine();

    size_t indices[kCyclePageCols];
    size_t page = 0;
    CycleWindow(row.ParamCount(), active_col, indices, page);
    const size_t focus
        = (page == kCyclePageCols) ? kCycleFocusSlot
          : (page > 0)             ? (page - 1) / 2
                                   : 0;

    for(size_t v = 0; v < page; ++v)
    {
        const size_t        i   = indices[v];
        const ColumnGeom    col = ColumnGeometry(v, page);
        const ParameterDef* def = row.ParamAt(reg, i);
        if(def == nullptr)
            continue;

        const bool active = (i == active_col);
        const float depth = CarouselDepth(v, page);
        DrawColumnSides(col, active);

        const float norm = ParameterRegistry::Normalize(*def, *def->value_ptr);

        switch(def->display_type)
        {
        case ParamDisplayType::Unipolar:
            if(def->center_mark)
                DrawCenterMark(col, active, depth);
            DrawUnipolarBar(col, norm, active, depth);
            break;
        case ParamDisplayType::Bipolar:
            DrawDashedCenterLine(col, active, depth);
            DrawBipolarBar(col, norm, active, depth);
            break;
        case ParamDisplayType::Toggle:
            DrawToggle(col, *def, active, depth);
            break;
        case ParamDisplayType::CountBar:
            DrawCountBar(col, *def, active, depth);
            break;
        case ParamDisplayType::CountNum:
            DrawCountNum(col, *def, active, depth);
            break;
        case ParamDisplayType::Seconds:
        case ParamDisplayType::HoldTime:
            DrawUnipolarBar(col, norm, active, depth);
            break;
        }
    }

    // Header n/m uses full ParamCount (not the visible page).
    DrawValueHeader(reg, row, active_col, show_cpu_meter, cpu_load, governor);
    DrawSegmentedRow(reg, row, active_col);

    // Catch-up line on the bound column (solid + end ticks), not while scrolling
    // the cycle list — only the physical pot vs. stored value matters (4.6).
    // Active is always drawn in the focus slot when not scrolling.
    if(row.PickupActive() && !row.InCycleScroll() && page > 0)
    {
        if(const ParameterDef* def = row.ParamAt(reg, row.BoundIndex()))
        {
            if(def->display_type != ParamDisplayType::Toggle)
            {
                DrawPickupLine(ColumnGeometry(focus, page),
                               row.PickupPotNorm(),
                               def->display_type);
            }
        }
    }

    if(modulated_norm >= 0.f && page > 0)
    {
        if(const ParameterDef* def = row.ParamAt(reg, active_col))
            DrawModDots(ColumnGeometry(focus, page),
                        modulated_norm,
                        def->display_type);
    }
}

namespace
{
// Cheap deterministic sparkle — no RNG state needed across frames.
uint32_t SparkleHash(int x, int y, uint32_t seed)
{
    uint32_t n = static_cast<uint32_t>(x) * 374761393u
                 ^ static_cast<uint32_t>(y) * 668265263u ^ seed;
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

// Soft Perseids rec gate (UI-only; audio has no gap).
constexpr uint32_t kRecSoftMs = 200;
} // namespace

void DisplayRenderer::DrawRecSparkleFill(int      x0,
                                         int      y,
                                         int      w,
                                         int      h,
                                         float    grow,
                                         uint32_t seed,
                                         bool     left_to_right,
                                         float    visibility)
{
    if(grow <= 0.f || visibility <= 0.f)
        return;
    if(grow > 1.f)
        grow = 1.f;
    if(visibility > 1.f)
        visibility = 1.f;

    const int x_lo = x0 + 1;
    const int x_hi = x0 + w - 2;
    const int y_lo = y + 1;
    const int y_hi = y + h - 2;
    if(x_hi < x_lo || y_hi < y_lo)
        return;

    const float cx     = 0.5f * static_cast<float>(x_lo + x_hi);
    const float half_w = 0.5f * static_cast<float>(x_hi - x_lo + 1);
    const float inner  = static_cast<float>(x_hi - x_lo + 1);
    const float radius = grow * half_w;
    if(!left_to_right && radius < 0.5f)
        return;
    if(left_to_right && grow * inner < 0.5f)
        return;

    const int   y_span = y_hi - y_lo + 1;
    const int   kSparks = 18;
    const float t_sec
        = static_cast<float>(daisy::System::GetNow()) * 0.001f;
    const uint32_t vis_thr
        = static_cast<uint32_t>(visibility * 255.f);
    const float edge_x
        = static_cast<float>(x_lo) + grow * inner; // L→R reveal front

    for(int i = 0; i < kSparks; ++i)
    {
        // Soft gate — thins the field in/out without teleporting.
        if((SparkleHash(i, 3, seed) & 255u) >= vis_thr)
            continue;

        const uint32_t h   = SparkleHash(i, 7, seed);
        const float    spd = 0.45f + static_cast<float>((h >> 1) & 255u)
                                          * (0.70f / 255.f); // trips/sec
        const float    ph0 = static_cast<float>((h >> 9) & 255u) * (1.f / 255.f);
        float          u   = ph0 + t_sec * spd;
        u -= floorf(u);
        if(u < 0.f)
            u += 1.f;

        int x;
        if(left_to_right)
        {
            // Full-bar travel, clipped by grow — no compressing into a left blob.
            const float x_f = static_cast<float>(x_lo) + u * inner;
            if(x_f > edge_x + 0.5f)
                continue;
            x = static_cast<int>(x_f + 0.5f);

            // Density peaks at mid-bar (~+50%), thins toward left and right.
            const float xn  = (x_f - static_cast<float>(x_lo)) / (inner + 0.01f);
            const float mid = 1.f - 2.f * fabsf(xn - 0.5f); // 0 at edges, 1 at 50%
            if(mid < 0.f)
                continue;
            const float dens = 0.65f + 0.35f * mid; // edges ~0.65, center 1.0
            if((SparkleHash(i, 11, seed) & 255u)
               >= static_cast<uint32_t>(dens * 255.f))
                continue;
        }
        else
        {
            const float side = (h & 1u) ? 1.f : -1.f;
            x = static_cast<int>(cx + side * u * radius + 0.5f);
        }
        if(x < x_lo || x > x_hi)
            continue;

        const int py
            = y_lo
              + static_cast<int>((h >> 17) % static_cast<uint32_t>(y_span));
        display_.DrawPixel(x, py, true);
    }
}

void DisplayRenderer::DrawRecCenterSolid(int   x0,
                                         int   y,
                                         int   w,
                                         int   h,
                                         float grow)
{
    if(grow <= 0.f)
        return;
    if(grow > 1.f)
        grow = 1.f;

    const int inner_w = w - 2;
    const int span
        = static_cast<int>(grow * static_cast<float>(inner_w) + 0.5f);
    if(span <= 0)
        return;

    const int mid   = x0 + 1 + inner_w / 2;
    int       left  = mid - span / 2;
    int       right = left + span - 1;
    if(left < x0 + 1)
        left = x0 + 1;
    if(right > x0 + inner_w)
        right = x0 + inner_w;
    display_.DrawRect(left, y + 1, right, y + h - 2, true, true);
}

void DisplayRenderer::DrawTrailLifeBar(int                x0,
                                       int                y,
                                       int                w,
                                       int                h,
                                       size_t             trail_index,
                                       const TrailLifeUi& life,
                                       uint8_t            rec_style)
{
    const int x1 = x0 + w - 1;
    const int y1 = y + h - 1;
    display_.DrawRect(x0, y, x1, y1, true, false);

    if(life.phase == TrailLifePhase::Empty)
    {
        if(trail_index < kTrailCount)
        {
            life_anim_[trail_index].last_phase = TrailLifePhase::Empty;
            life_anim_[trail_index].soft       = RecSoft::Idle;
            life_anim_[trail_index].grow_latch = 1.f;
        }
        return;
    }

    float fill = life.fill;
    if(fill < 0.f)
        fill = 0.f;
    if(fill > 1.f)
        fill = 1.f;

    const int inner_w = w - 2;
    const int fill_w
        = static_cast<int>(fill * static_cast<float>(inner_w) + 0.5f);

    const uint32_t seed
        = 0xA5u + static_cast<uint32_t>(trail_index) * 97u;
    // 0 PRS center-out, 1 PLR L→R, 2 CTR solid center-out.
    const bool perseids = (rec_style <= 1);
    const bool ltr      = (rec_style == 1);

    LifeBarAnim*   anim = (trail_index < kTrailCount) ? &life_anim_[trail_index]
                                                      : nullptr;
    const uint32_t now  = daisy::System::GetNow();

    if(anim != nullptr && perseids)
    {
        if(life.phase == TrailLifePhase::Recording
           && anim->last_phase != TrailLifePhase::Recording
           && anim->soft != RecSoft::FadeOut)
        {
            // Soft fade-in only for PRS (center). PLR starts clean L→R —
            // a soft gate + min-grow looked like grains piling on the left.
            if(!ltr)
            {
                anim->soft      = RecSoft::FadeIn;
                anim->soft_t0   = now;
                anim->ltr_latch = false;
            }
        }

        if(anim->last_phase == TrailLifePhase::Recording
           && (life.phase == TrailLifePhase::FadeIn
               || life.phase == TrailLifePhase::Hold)
           && anim->soft != RecSoft::FadeOut)
        {
            anim->soft       = RecSoft::FadeOut;
            anim->soft_t0    = now;
            anim->ltr_latch  = ltr;
            if(anim->grow_latch < 0.15f)
                anim->grow_latch = 1.f;
        }

        if(life.phase == TrailLifePhase::Recording)
        {
            if(fill > 0.05f)
                anim->grow_latch = fill;
            anim->ltr_latch = ltr;
        }

        if(anim->soft != RecSoft::Idle)
        {
            const uint32_t dt = now - anim->soft_t0;
            if(dt >= kRecSoftMs)
            {
                const RecSoft done = anim->soft;
                anim->soft         = RecSoft::Idle;
                if(done == RecSoft::FadeOut)
                {
                    // Fall through to solid FIN/Hold this frame.
                }
                else if(life.phase == TrailLifePhase::Recording)
                {
                    DrawRecSparkleFill(x0, y, w, h, fill, seed, ltr, 1.f);
                    anim->last_phase = life.phase;
                    return;
                }
            }
            else
            {
                const float t = static_cast<float>(dt)
                                / static_cast<float>(kRecSoftMs);
                float vis;
                float grow_draw;
                bool  use_ltr = anim->ltr_latch;
                if(anim->soft == RecSoft::FadeIn)
                {
                    vis       = t * (2.f - t); // ease-out appear
                    grow_draw = fill; // true progress — no left blob floor
                    use_ltr   = ltr;
                }
                else
                {
                    const float u = 1.f - t;
                    vis           = u * u; // ease-in disappear
                    grow_draw     = anim->grow_latch;
                }
                DrawRecSparkleFill(
                    x0, y, w, h, grow_draw, seed, use_ltr, vis);
                anim->last_phase = life.phase;
                return; // hold solid FIN until embers clear
            }
        }

        anim->last_phase = life.phase;
    }
    else if(anim != nullptr)
    {
        anim->soft       = RecSoft::Idle;
        anim->last_phase = life.phase;
    }

    // Recording: PRS/PLR embers or CTR solid. FIN L→R, FOUT empty L→R.
    if(fill_w > 0 || life.phase == TrailLifePhase::Recording)
    {
        if(life.phase == TrailLifePhase::FadeOut)
        {
            const int fx0 = x0 + 1 + (inner_w - fill_w);
            display_.DrawRect(fx0, y + 1, x0 + inner_w, y1 - 1, true, true);
        }
        else if(life.phase == TrailLifePhase::Recording)
        {
            if(rec_style >= 2)
                DrawRecCenterSolid(x0, y, w, h, fill);
            else
                DrawRecSparkleFill(x0, y, w, h, fill, seed, ltr, 1.f);
        }
        else if(fill_w > 0)
        {
            display_.DrawRect(x0 + 1, y + 1, x0 + fill_w, y1 - 1, true, true);
        }
    }

    if(life.phase == TrailLifePhase::Hold && fill_w >= inner_w)
    {
        char label[8];
        if(life.hold_sec < 0)
            snprintf(label, sizeof(label), "INF");
        else
            snprintf(label, sizeof(label), "%ds", static_cast<int>(life.hold_sec));

        constexpr int kGlyphW = 4;
        const int     text_w  = static_cast<int>(strlen(label)) * kGlyphW;
        int           tx      = x0 + (w - text_w) / 2;
        if(tx < x0 + 1)
            tx = x0 + 1;
        const int ty = y;
        display_.SetCursor(tx, ty);
        display_.WriteString(label, Font_4x6, false);
        display_.DrawRect(x0, y, x1, y1, true, false);
    }
}

void DisplayRenderer::DrawDashboard(bool                playing,
                                    bool                reset_confirm,
                                    uint32_t            reset_seconds_left,
                                    uint8_t             rec_trail_slot,
                                    bool                rec_trig_active,
                                    const TrailSnapshot trails[kTrailCount],
                                    float               input_level,
                                    float               threshold,
                                    const TrailLifeUi   life[kTrailCount],
                                    size_t              active_trail_count,
                                    bool                show_cpu_meter,
                                    bool                show_ram_meter,
                                    float               cpu_load,
                                    float               xfade_focus,
                                    float               xfade_amp,
                                    bool                governor,
                                    uint8_t             rec_style)
{
    Clear();

    if(reset_confirm)
    {
        display_.DrawRect(8, 14, kWidth - 9, 49, true, true);
        display_.SetCursor(16, 20);
        display_.WriteString("Delete all", Font_7x10, false);
        display_.SetCursor(16, 32);
        display_.WriteString("Trails?", Font_7x10, false);
        char countdown[12];
        snprintf(countdown,
                 sizeof(countdown),
                 "%lus",
                 static_cast<unsigned long>(reset_seconds_left));
        display_.SetCursor(88, 40);
        display_.WriteString(countdown, Font_6x8, false);
        display_.SetCursor(4, 54);
        display_.WriteString("Short=OK  Turn=Cancel", Font_6x8, true);
        return;
    }

    display_.SetCursor(0, 0);
    display_.WriteString("PERSEIDS", Font_6x8, true);

    // PLAY/PAUSE right-aligned; optional C… / R… meters immediately left (4.9).
    const char* play_str = playing ? "PLAY" : "PAUSE";
    const int   play_w   = static_cast<int>(strlen(play_str)) * 6;
    const int   right_x  = kWidth - play_w;

    constexpr int kMeterGlyphW = 4;
    constexpr int kMeterGap    = 2;

    char meter[20];
    meter[0] = '\0';
    if(show_cpu_meter || show_ram_meter || governor)
    {
        float load = cpu_load;
        if(load < 0.f)
            load = 0.f;
        if(load > 9.99f)
            load = 9.99f;
        int cpu_pct = static_cast<int>(load * 100.f + 0.5f);
        if(cpu_pct < 0)
            cpu_pct = 0;
        if(cpu_pct > 999)
            cpu_pct = 999;

        constexpr unsigned long long kSdramTotal
            = 64ull * 1024ull * 1024ull;
        constexpr unsigned kSdramPct = static_cast<unsigned>(
            (100ull * CaptureEngine::kTrailSdramBytes + kSdramTotal / 2)
            / kSdramTotal);

        // Governor marker "L!" sits directly left of the CPU figure. While it
        // shows, the SDRAM figure is suppressed: it is a compile-time constant
        // and the live CPU number is what matters when the cloud is throttled.
        if(governor)
        {
            if(show_cpu_meter)
            {
                snprintf(meter,
                         sizeof(meter),
                         "L!C%u",
                         static_cast<unsigned>(cpu_pct));
            }
            else if(show_ram_meter)
                snprintf(meter, sizeof(meter), "L!R%u", kSdramPct);
            else
                snprintf(meter, sizeof(meter), "L!");
        }
        else if(show_cpu_meter && show_ram_meter)
        {
            snprintf(meter,
                     sizeof(meter),
                     "C%u R%u",
                     static_cast<unsigned>(cpu_pct),
                     kSdramPct);
        }
        else if(show_cpu_meter)
        {
            snprintf(meter,
                     sizeof(meter),
                     "C%u%%",
                     static_cast<unsigned>(cpu_pct));
        }
        else
        {
            snprintf(meter, sizeof(meter), "R%u", kSdramPct);
        }
    }

    const int meter_w = static_cast<int>(strlen(meter)) * kMeterGlyphW;
    const int meter_x
        = meter[0] != '\0' ? right_x - kMeterGap - meter_w : right_x;

    // Same understated Font_4x6 as the CPU/RAM meter (ARCHITECTURE 4.9).
    char rec_hdr[12];
    if(rec_trig_active)
        snprintf(rec_hdr, sizeof(rec_hdr), "REC%u", rec_trail_slot);
    else
        snprintf(rec_hdr, sizeof(rec_hdr), "R%u", rec_trail_slot);
    const int rec_w = static_cast<int>(strlen(rec_hdr)) * kMeterGlyphW;
    // Nominally at 54, but the meter block wins: REC slides left rather than
    // collide with it. 48 is the first free column after "PERSEIDS".
    int rec_x = 54;
    if(rec_x + rec_w + kMeterGap > meter_x)
        rec_x = meter_x - rec_w - kMeterGap;
    if(rec_x < 48)
        rec_x = 48;
    display_.SetCursor(rec_x, 1);
    display_.WriteString(rec_hdr, Font_4x6, true);

    if(meter[0] != '\0')
    {
        display_.SetCursor(meter_x, 1);
        display_.WriteString(meter, Font_4x6, true);
    }

    display_.SetCursor(right_x, 0);
    display_.WriteString(play_str, Font_6x8, true);

    // Input threshold VU (left)
    constexpr int kVuX0 = 0;
    constexpr int kVuX1 = 9;
    constexpr int kVuY0 = 17;
    constexpr int kVuY1 = 56;
    display_.DrawRect(kVuX0, kVuY0, kVuX1, kVuY1, true, false);

    const float lvl
        = input_level < 0.f ? 0.f : (input_level > 1.f ? 1.f : input_level);
    const float thr
        = threshold < 0.f ? 0.f : (threshold > 1.f ? 1.f : threshold);
    const int span = kVuY1 - kVuY0 - 2;
    const int fill = static_cast<int>(lvl * static_cast<float>(span) + 0.5f);
    if(fill > 0)
    {
        display_.DrawRect(kVuX0 + 1,
                          kVuY1 - 1 - fill,
                          kVuX1 - 1,
                          kVuY1 - 1,
                          true,
                          true);
    }
    const int thr_y
        = kVuY1 - 1 - static_cast<int>(thr * static_cast<float>(span) + 0.5f);
    for(int x = kVuX0; x <= kVuX1; ++x)
        display_.DrawPixel(x, thr_y, true);

    // Row layout: VU | [xf] T# [xf] | 3px | % | 1px | L | 1px | S | 1px | life
    // Font_6x8: T#=12px, %%=24px ("100%"), L/S=6px each.
    // T# sits 1px further right so Crossfade side ticks are equal width.
    constexpr int kTx     = 13;
    constexpr int kGapT   = 3;
    constexpr int kGap    = 1;
    constexpr int kTWidth = 12; // "T1"
    constexpr int kPctW   = 24; // "100%"
    constexpr int kFlagW  = 6;  // "L" / "S"
    constexpr int kPctX   = kTx + kTWidth + kGapT;
    constexpr int kLX     = kPctX + kPctW + kGap;
    constexpr int kSX     = kLX + kFlagW + kGap;
    constexpr int kBarX   = kSX + kFlagW + kGap;
    constexpr int kBarR   = 126;
    constexpr int kBarW   = kBarR - kBarX + 1;
    constexpr int kBarH   = 7;

    size_t shown = active_trail_count;
    if(shown < 1)
        shown = 1;
    if(shown > kTrailCount)
        shown = kTrailCount;

    // Crossfade focus — thin ticks left & right of T# (Block 9).
    DrawCrossfadeFocusBars(kTx, kTWidth, 17, 8, shown, xfade_focus, xfade_amp);

    for(size_t i = 0; i < shown; ++i)
    {
        const TrailSnapshot& t = trails[i];
        const int            y = 17 + static_cast<int>(i) * 8;

        char tlab[4];
        snprintf(tlab, sizeof(tlab), "T%u", static_cast<unsigned>(i + 1));
        display_.SetCursor(kTx, y);
        display_.WriteString(tlab, Font_6x8, true);

        char pct[8];
        const unsigned p = static_cast<unsigned>(t.level * 100.f + 0.5f);
        snprintf(pct, sizeof(pct), "%3u%%", p);
        display_.SetCursor(kPctX, y);
        display_.WriteString(pct, Font_6x8, true);

        if(t.locked)
        {
            display_.SetCursor(kLX, y);
            display_.WriteString("L", Font_6x8, true);
        }
        if(t.solo)
        {
            display_.SetCursor(kSX, y);
            display_.WriteString("S", Font_6x8, true);
        }

        DrawTrailLifeBar(kBarX, y, kBarW, kBarH, i, life[i], rec_style);
    }
}

void DisplayRenderer::DrawCrossfadeFocusBars(int    t_x,
                                             int    t_w,
                                             int    row0_y,
                                             int    row_h,
                                             size_t shown,
                                             float  focus,
                                             float  amp)
{
    if(amp < 0.04f || shown < 1 || row_h < 1 || t_w < 1)
        return;

    float f = focus;
    if(f < 0.f)
        f = 0.f;
    const float fmax = static_cast<float>(shown) - 0.001f;
    if(f > fmax)
        f = fmax;

    // Continuous Y through the Trail stack (row centers).
    const float y_mid
        = static_cast<float>(row0_y) + f * static_cast<float>(row_h) + 3.f;
    int h = 2 + static_cast<int>(amp * 5.f + 0.5f);
    if(h > 7)
        h = 7;
    int y0 = static_cast<int>(y_mid - 0.5f * static_cast<float>(h) + 0.5f);
    int y1 = y0 + h - 1;

    const int band0 = row0_y;
    const int band1 = row0_y + static_cast<int>(shown) * row_h - 1;
    if(y0 < band0)
        y0 = band0;
    if(y1 > band1)
        y1 = band1;
    if(y1 < y0)
        return;

    // Matching 1px ticks immediately left and right of the T# glyph.
    const int x_left  = t_x - 1;
    const int x_right = t_x + t_w;
    if(x_left >= 0)
        display_.DrawRect(x_left, y0, x_left, y1, true, true);
    if(x_right < kWidth)
        display_.DrawRect(x_right, y0, x_right, y1, true, true);
}

} // namespace perseids
