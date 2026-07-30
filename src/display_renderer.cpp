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
// Catch-up: horizontal stubs only, 1px gap from the value bar (no vertical ticks).
constexpr int kPickupOverhang = 5;
constexpr int kPickupBarGap   = 1;
constexpr int kModDotOutset   = 2;
}

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
    // Always size columns as if the page held kCyclePageCols entries, so
    // 2- and 3-param blocks match 4-wide Trails/Swarm column width.
    const size_t slots
        = (count > 0 && count < kCyclePageCols) ? kCyclePageCols : count;
    const int total_w = kWidth - 2 * kMargin;
    const int col_w   = static_cast<int>(total_w / static_cast<int>(slots));
    const int x       = kMargin + static_cast<int>(index) * col_w;
    return {x, col_w, x + col_w / 2};
}

void DisplayRenderer::CycleWindow(size_t  param_count,
                                  size_t  active_col,
                                  size_t& out_start,
                                  size_t& out_page)
{
    out_page = param_count < kCyclePageCols ? param_count : kCyclePageCols;
    if(param_count <= out_page || out_page == 0)
    {
        out_start = 0;
        return;
    }
    // Keep active visible; left-align until the window must slide right.
    if(active_col + 1 <= out_page)
        out_start = 0;
    else
        out_start = active_col + 1 - out_page;
    if(out_start + out_page > param_count)
        out_start = param_count - out_page;
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

void DisplayRenderer::DrawDashedCenterLine(const ColumnGeom& col, bool full_width)
{
    const int y     = (kParamTop + kParamBottom) / 2;
    const int inset = full_width ? 1 : col.w / 4;
    for(int x = col.x + inset; x < col.x + col.w - inset; x += 2)
        display_.DrawPixel(x, y, true);
}

// 50% hint for crossfade-style unipolar params (e.g. Blend): one dot per
// side at half bar height, equal gap to the bar — deliberately subtler than
// the bipolar dashed zero line (the middle is an equal mix, not "no effect").
void DisplayRenderer::DrawCenterMark(const ColumnGeom& col, bool active)
{
    const int y        = ((kCeilingY + 1) + (kSegRowY - 1)) / 2;
    const int bar_w    = active ? kBarWide : kBarNarrow;
    const int bar_x    = col.cx - bar_w / 2;
    constexpr int kGap = 3;

    display_.DrawPixel(bar_x - kGap, y, true);
    display_.DrawPixel(bar_x + bar_w - 1 + kGap, y, true);
}

void DisplayRenderer::DrawUnipolarBar(const ColumnGeom& col,
                                      float             norm,
                                      bool              active)
{
    const int bar_w = active ? kBarWide : kBarNarrow;
    const int bar_x = col.cx - bar_w / 2;
    const int base_y = kSegRowY - 1;
    const int top_y  = kCeilingY + 1;
    const int span   = base_y - top_y;
    int       h      = static_cast<int>(norm * static_cast<float>(span) + 0.5f);

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
                                     bool              active)
{
    const int bar_w  = active ? kBarWide : kBarNarrow;
    const int bar_x  = col.cx - bar_w / 2;
    const int center = (kParamTop + kParamBottom) / 2;
    const int up_span   = center - (kCeilingY + 1);
    const int down_span = (kSegRowY - 1) - center;

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
                                 bool                active)
{
    (void)active;

    const bool on = *def.value_ptr >= (def.min_val + def.max_val) * 0.5f;

    const int x0  = col.x + 2;
    const int x1  = col.x + col.w - 3;
    const int y0  = kParamTop;
    const int y1  = kSegRowY - 3;
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
                                   bool                active)
{
    const float norm = ParameterRegistry::Normalize(def, *def.value_ptr);
    DrawUnipolarBar(col, norm, active);
}

void DisplayRenderer::DrawCountNum(const ColumnGeom&   col,
                                   const ParameterDef& def,
                                   bool                active)
{
    char val[8];
    FormatCountLabel(def, val, sizeof(val));

    const int font_w = active ? 7 : 6;
    const int text_w = static_cast<int>(strlen(val)) * font_w;
    const int x      = col.cx - text_w / 2;

    if(active)
    {
        display_.SetCursor(x, (kParamTop + kParamBottom) / 2 - 4);
        display_.WriteString(val, Font_7x10, true);
    }
    else
    {
        display_.SetCursor(x, kSegRowY - 9);
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
        const float signed_v
            = (ParameterRegistry::Normalize(def, *def.value_ptr) - 0.5f) * 200.f;
        snprintf(out, out_len, "%+d%%", static_cast<int>(signed_v + 0.5f));
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
                                      float                    cpu_load)
{
    char pos[8];
    FormatPosition(active_col, row.ParamCount(), pos, sizeof(pos));

    display_.SetCursor(0, kHeaderY);
    display_.WriteString(row.BlockName(), Font_6x8, true);

    constexpr int kGlyphW = 4;
    const int     pos_w   = static_cast<int>(strlen(pos)) * kGlyphW;

    if(show_cpu_meter)
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
        snprintf(cpu, sizeof(cpu), "C%u%%", static_cast<unsigned>(pct));
        const int cpu_w = static_cast<int>(strlen(cpu)) * kGlyphW;
        constexpr int kGap = 2;
        display_.SetCursor(kWidth - pos_w - kGap - cpu_w, kHeaderY + 1);
        display_.WriteString(cpu, Font_4x6, true);
    }

    display_.SetCursor(kWidth - pos_w, kHeaderY + 1);
    display_.WriteString(pos, Font_4x6, true);

    const ParameterDef* def = row.ParamAt(reg, active_col);
    if(def == nullptr || def->display_type == ParamDisplayType::Toggle)
        return;

    char val[12];
    FormatValue(*def, val, sizeof(val));

    const int text_w = static_cast<int>(strlen(val)) * 6;
    const int x      = (kWidth - text_w) / 2;

    display_.SetCursor(x, kHeaderY);
    display_.WriteString(val, Font_6x8, true);
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
    size_t       start = 0;
    size_t       page  = 0;
    CycleWindow(row.ParamCount(), active_col, start, page);

    for(size_t v = 0; v < page; ++v)
    {
        const size_t        i   = start + v;
        const ColumnGeom    col = ColumnGeometry(v, page);
        const ParameterDef* def = row.ParamAt(reg, i);
        if(def == nullptr)
            continue;

        const bool sel = (i == active_col);
        const int  pad = 1;

        if(sel)
        {
            display_.DrawRect(col.x + pad,
                              kSegRowY,
                              col.x + col.w - pad - 1,
                              kSegRowY + kSegRowH - 1,
                              true,
                              true);
            display_.SetCursor(col.x + pad + 2, kSegRowY + 2);
            display_.WriteString(def->abbrev, Font_6x8, false);
        }
        else
        {
            display_.DrawRect(col.x + pad,
                              kSegRowY,
                              col.x + col.w - pad - 1,
                              kSegRowY + kSegRowH - 1,
                              true,
                              false);
            display_.SetCursor(col.x + pad + 2, kSegRowY + 2);
            display_.WriteString(def->abbrev, Font_6x8, true);
        }

        // Dynamic side hint behind the abbrev (e.g. "sp"/"sw" for Blend):
        // shows which half of the travel the value sits in, nothing at
        // exactly 50%. Same row height as the abbrev, same inversion as the
        // cell, clipped if the column is too narrow.
        if(def->seg_hint_low != nullptr && def->seg_hint_high != nullptr)
        {
            const float n
                = ParameterRegistry::Normalize(*def, *def->value_ptr);
            const int pct = static_cast<int>(n * 100.f + 0.5f);

            const char* hint = nullptr;
            if(pct < 50)
                hint = def->seg_hint_low;
            else if(pct > 50)
                hint = def->seg_hint_high;

            if(hint != nullptr)
            {
                // Font_4x6 — same understated size as the CPU% figure, offset
                // like the header pairing (small font sits 1px below 6x8).
                const int abbrev_w
                    = static_cast<int>(strlen(def->abbrev)) * 6;
                const int hint_w = static_cast<int>(strlen(hint)) * 4;
                const int hx     = col.x + pad + 2 + abbrev_w + 2;
                if(hx + hint_w <= col.x + col.w - pad - 1)
                {
                    display_.SetCursor(hx, kSegRowY + 3);
                    display_.WriteString(hint, Font_4x6, !sel);
                }
            }
        }
    }
}

void DisplayRenderer::DrawCycleView(const ParameterRegistry& reg,
                                    const CycleRow&          row,
                                    size_t                   active_col,
                                    float                    modulated_norm,
                                    bool                     show_cpu_meter,
                                    float                    cpu_load)
{
    Clear();
    DrawCeilingLine();

    size_t start = 0;
    size_t page  = 0;
    CycleWindow(row.ParamCount(), active_col, start, page);

    for(size_t v = 0; v < page; ++v)
    {
        const size_t        i   = start + v;
        const ColumnGeom    col = ColumnGeometry(v, page);
        const ParameterDef* def = row.ParamAt(reg, i);
        if(def == nullptr)
            continue;

        const bool active = (i == active_col);
        DrawColumnSides(col, active);

        const float norm = ParameterRegistry::Normalize(*def, *def->value_ptr);

        switch(def->display_type)
        {
        case ParamDisplayType::Unipolar:
            if(def->center_mark)
                DrawCenterMark(col, active);
            DrawUnipolarBar(col, norm, active);
            break;
        case ParamDisplayType::Bipolar:
            DrawDashedCenterLine(col, active);
            DrawBipolarBar(col, norm, active);
            break;
        case ParamDisplayType::Toggle:
            DrawToggle(col, *def, active);
            break;
        case ParamDisplayType::CountBar:
            DrawCountBar(col, *def, active);
            break;
        case ParamDisplayType::CountNum:
            DrawCountNum(col, *def, active);
            break;
        case ParamDisplayType::Seconds:
        case ParamDisplayType::HoldTime:
            DrawUnipolarBar(col, norm, active);
            break;
        }
    }

    // Header n/m uses full ParamCount (not the visible page).
    DrawValueHeader(reg, row, active_col, show_cpu_meter, cpu_load);
    DrawSegmentedRow(reg, row, active_col);

    // Catch-up line on the bound column (solid + end ticks), not while scrolling
    // the cycle list — only the physical pot vs. stored value matters (4.6).
    if(row.PickupActive() && !row.InCycleScroll())
    {
        const size_t bound = row.BoundIndex();
        if(bound >= start && bound < start + page)
        {
            if(const ParameterDef* def = row.ParamAt(reg, bound))
            {
                if(def->display_type != ParamDisplayType::Toggle)
                {
                    DrawPickupLine(ColumnGeometry(bound - start, page),
                                   row.PickupPotNorm(),
                                   def->display_type);
                }
            }
        }
    }

    if(modulated_norm >= 0.f && active_col >= start
       && active_col < start + page)
    {
        if(const ParameterDef* def = row.ParamAt(reg, active_col))
            DrawModDots(ColumnGeometry(active_col - start, page),
                        modulated_norm,
                        def->display_type);
    }
}

void DisplayRenderer::DrawTrailLifeBar(int                x0,
                                       int                y,
                                       int                w,
                                       int                h,
                                       const TrailLifeUi& life)
{
    const int x1 = x0 + w - 1;
    const int y1 = y + h - 1;
    display_.DrawRect(x0, y, x1, y1, true, false);

    if(life.phase == TrailLifePhase::Empty)
        return;

    float fill = life.fill;
    if(fill < 0.f)
        fill = 0.f;
    if(fill > 1.f)
        fill = 1.f;

    // FIN: solid fill L→R. Recording: striped (≠ FadeIn). FOUT: empty L→R.
    const int inner_w = w - 2;
    const int fill_w
        = static_cast<int>(fill * static_cast<float>(inner_w) + 0.5f);
    if(fill_w > 0)
    {
        if(life.phase == TrailLifePhase::FadeOut)
        {
            const int fx0 = x0 + 1 + (inner_w - fill_w);
            display_.DrawRect(fx0, y + 1, x0 + inner_w, y1 - 1, true, true);
        }
        else if(life.phase == TrailLifePhase::Recording)
        {
            for(int x = x0 + 1; x <= x0 + fill_w; x += 2)
                display_.DrawLine(x, y + 1, x, y1 - 1, true);
        }
        else
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

        // Font_4x6 fits inside the bar; Font_6x8 inverted bled past the frame.
        constexpr int kGlyphW = 4;
        const int     text_w  = static_cast<int>(strlen(label)) * kGlyphW;
        int           tx      = x0 + (w - text_w) / 2;
        if(tx < x0 + 1)
            tx = x0 + 1;
        const int ty = y; // 1px higher so Font_4x6 sits centered in the bar
        display_.SetCursor(tx, ty);
        display_.WriteString(label, Font_4x6, false);

        // Restore bar outline in case the glyph cell clipped past the frame.
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
                                    float               xfade_amp)
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

    // Same understated Font_4x6 as the CPU/RAM meter (ARCHITECTURE 4.9).
    char rec_hdr[12];
    if(rec_trig_active)
        snprintf(rec_hdr, sizeof(rec_hdr), "REC%u", rec_trail_slot);
    else
        snprintf(rec_hdr, sizeof(rec_hdr), "R%u", rec_trail_slot);
    display_.SetCursor(54, 1);
    display_.WriteString(rec_hdr, Font_4x6, true);

    // PLAY/PAUSE right-aligned; optional C… / R… meters immediately left (4.9).
    const char* play_str = playing ? "PLAY" : "PAUSE";
    const int   play_w   = static_cast<int>(strlen(play_str)) * 6;
    int         right_x  = kWidth - play_w;

    if(show_cpu_meter || show_ram_meter)
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

        char meter[20];
        meter[0] = '\0';
        if(show_cpu_meter && show_ram_meter)
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

        constexpr int kGlyphW = 4;
        const int     meter_w = static_cast<int>(strlen(meter)) * kGlyphW;
        constexpr int kGap    = 2;
        display_.SetCursor(right_x - kGap - meter_w, 1);
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

        DrawTrailLifeBar(kBarX, y, kBarW, kBarH, life[i]);
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
