#include "display_renderer.h"

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
constexpr int kPickupInset   = 3; // catch-up line spans active column interior
constexpr int kModDotOutset  = 2; // mod dots sit outside pickup line ends
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
    const int total_w = kWidth - 2 * kMargin;
    const int col_w   = static_cast<int>(total_w / static_cast<int>(count));
    const int x       = kMargin + static_cast<int>(index) * col_w;
    return {x, col_w, x + col_w / 2};
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

void DisplayRenderer::DrawUnipolarBar(const ColumnGeom& col,
                                      float             norm,
                                      bool              active)
{
    const int bar_w = active ? kBarWide : kBarNarrow;
    const int bar_x = col.cx - bar_w / 2;
    const int base_y = kSegRowY - 1;
    const int top_y  = kCeilingY + 1;
    const int span   = base_y - top_y;
    const int h      = static_cast<int>(norm * static_cast<float>(span) + 0.5f);

    if(h > 0)
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
    snprintf(val, sizeof(val), "%d", static_cast<int>(*def.value_ptr + 0.5f));

    if(active)
    {
        display_.SetCursor(col.cx - 6, (kParamTop + kParamBottom) / 2 - 4);
        display_.WriteString(val, Font_7x10, true);
    }
    else
    {
        display_.SetCursor(col.cx - 3, kSegRowY - 9);
        display_.WriteString(val, Font_6x8, true);
    }
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
        snprintf(out, out_len, "%d", static_cast<int>(*def.value_ptr + 0.5f));
        break;
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
                                      size_t                   active_col)
{
    char pos[8];
    FormatPosition(active_col, row.ParamCount(), pos, sizeof(pos));

    display_.SetCursor(0, kHeaderY);
    display_.WriteString(row.BlockName(), Font_6x8, true);

    const int pos_w = static_cast<int>(strlen(pos)) * 6;
    display_.SetCursor(kWidth - pos_w, kHeaderY);
    display_.WriteString(pos, Font_6x8, true);

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

void DisplayRenderer::DrawPickupLine(const ColumnGeom& col, float pot_norm)
{
    const int base_y = kSegRowY - 1;
    const int top_y  = kCeilingY + 1;
    const int span   = base_y - top_y;
    const int y      = base_y - static_cast<int>(pot_norm * static_cast<float>(span));
    const int x0       = col.x + kPickupInset;
    const int x1       = col.x + col.w - kPickupInset - 1;
    display_.DrawLine(x0, y, x1, y, true);
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

    const int x0 = col.x + kPickupInset;
    const int x1 = col.x + col.w - kPickupInset - 1;

    display_.DrawPixel(x0 - kModDotOutset, y, true);
    display_.DrawPixel(x0 - kModDotOutset - 2, y, true);
    display_.DrawPixel(x1 + kModDotOutset, y, true);
    display_.DrawPixel(x1 + kModDotOutset + 2, y, true);
}

void DisplayRenderer::DrawSegmentedRow(const ParameterRegistry& reg,
                                       const CycleRow&          row,
                                       size_t                   active_col)
{
    const size_t count = row.ParamCount();
    for(size_t i = 0; i < count; ++i)
    {
        const ColumnGeom col = ColumnGeometry(i, count);
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
    }
}

void DisplayRenderer::DrawCycleView(const ParameterRegistry& reg,
                                    const CycleRow&          row,
                                    size_t                   active_col,
                                    float                    modulated_norm)
{
    Clear();
    DrawCeilingLine();

    const size_t count = row.ParamCount();
    for(size_t i = 0; i < count; ++i)
    {
        const ColumnGeom      col = ColumnGeometry(i, count);
        const ParameterDef* def = row.ParamAt(reg, i);
        if(def == nullptr)
            continue;

        const bool active = (i == active_col);
        DrawColumnSides(col, active);

        const float norm = ParameterRegistry::Normalize(*def, *def->value_ptr);

        switch(def->display_type)
        {
        case ParamDisplayType::Unipolar:
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
        }
    }

    DrawValueHeader(reg, row, active_col);
    DrawSegmentedRow(reg, row, active_col);

    if(row.PickupActive())
    {
        if(const ParameterDef* def = row.ParamAt(reg, active_col))
        {
            if(def->display_type != ParamDisplayType::Toggle)
            {
                const ColumnGeom col = ColumnGeometry(active_col, count);
                DrawPickupLine(col, row.PickupPotNorm());
            }
        }
    }

    if(modulated_norm >= 0.f)
    {
        if(const ParameterDef* def = row.ParamAt(reg, active_col))
            DrawModDots(ColumnGeometry(active_col, count),
                        modulated_norm,
                        def->display_type);
    }
}

void DisplayRenderer::DrawDashboard(bool                     playing,
                                    bool                     reset_confirm,
                                    uint32_t                 reset_seconds_left,
                                    const TrailLevelController& trails)
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
        snprintf(countdown, sizeof(countdown), "%us", reset_seconds_left);
        display_.SetCursor(88, 40);
        display_.WriteString(countdown, Font_6x8, false);
        display_.SetCursor(4, 54);
        display_.WriteString("Short=OK  Turn=Cancel", Font_6x8, true);
        return;
    }

    display_.SetCursor(0, 0);
    display_.WriteString("PERSEIDS", Font_6x8, true);

    char rec_hdr[12];
    if(trails.RecTrigActive())
        snprintf(rec_hdr, sizeof(rec_hdr), "REC%u", trails.RecTrailSlot());
    else
        snprintf(rec_hdr, sizeof(rec_hdr), "R%u", trails.RecTrailSlot());
    display_.SetCursor(54, 0);
    display_.WriteString(rec_hdr, Font_6x8, true);

    const int play_x = kWidth - (playing ? 4 : 5) * 6;
    display_.SetCursor(play_x, 0);
    display_.WriteString(playing ? "PLAY" : "PAUSE", Font_6x8, true);

    display_.SetCursor(0, 9);
    display_.WriteString("     LVL LK SO", Font_6x8, true);

    for(size_t i = 0; i < TrailLevelController::kCount; ++i)
    {
        const TrailSnapshot& t = trails.Trail(i);
        char                 line[20];
        const unsigned pct
            = static_cast<unsigned>(t.level * 100.f + 0.5f);
        snprintf(line,
                 sizeof(line),
                 "T%u %3u%%",
                 static_cast<unsigned>(i + 1),
                 pct);

        const int y = 17 + static_cast<int>(i) * 8;
        display_.SetCursor(0, y);
        display_.WriteString(line, Font_6x8, true);

        int x = 66;
        if(t.locked)
        {
            display_.SetCursor(x, y);
            display_.WriteString("L", Font_6x8, true);
            x += 18;
        }
        if(t.solo)
        {
            display_.SetCursor(x, y);
            display_.WriteString("S", Font_6x8, true);
        }
    }
}

} // namespace perseids
