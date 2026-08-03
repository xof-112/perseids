#pragma once

#include "capture_params.h"
#include "cycle_row.h"
#include "param_registry.h"

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"

namespace perseids
{

// Reusable 4.11 display vocabulary — blocks only supply CycleRow + ParameterRegistry data.
class DisplayRenderer
{
  public:
    static constexpr int kWidth  = 128;
    static constexpr int kHeight = 64;

    static constexpr int kHeaderY     = 0;
    static constexpr int kCeilingY    = 10;
    static constexpr int kSegRowY     = 52;
    static constexpr int kSegRowH     = 12;
    static constexpr int kParamTop    = 12;
    static constexpr int kParamBottom = 51;
    // CycleView slots: half | full | center | full | half (4 column-widths).
    // Active is always the center slot (true screen middle). Extra params wrap
    // in; on very short rows the slots a wrap would duplicate stay empty.
    static constexpr size_t kCyclePageCols  = 5;
    static constexpr size_t kCycleFocusSlot = 2;

    void Init(daisy::DaisySeed& seed);

    void DrawDashboard(bool                playing,
                       bool                reset_confirm,
                       uint32_t            reset_seconds_left,
                       uint8_t             rec_trail_slot,
                       bool                rec_trig_active,
                       const TrailSnapshot trails[kTrailCount],
                       float               input_level,
                       float               threshold,
                       const TrailLifeUi   life[kTrailCount],
                       size_t              active_trail_count,
                       bool                show_cpu_meter = false,
                       bool                show_ram_meter = false,
                       float               cpu_load       = 0.f,
                       float               xfade_focus    = 0.f,
                       float               xfade_amp      = 0.f,
                       bool                governor       = false,
                       uint8_t             rec_style      = 1);

    void DrawCycleView(const ParameterRegistry& reg,
                       const CycleRow&          row,
                       size_t                   active_col,
                       float                    modulated_norm = -1.f,
                       bool                     show_cpu_meter = false,
                       float                    cpu_load       = 0.f,
                       bool                     governor       = false);

    void Present();

  private:
    struct ColumnGeom
    {
        int x;
        int w;
        int cx;
    };

    ColumnGeom ColumnGeometry(size_t index, size_t count) const;

    // Circular window: active in kCycleFocusSlot (center); wraps at list ends.
    // Fills out_indices[0..out_page) with param indices; page is 5 for any
    // non-empty row. out_indices needs at least kCyclePageCols entries. Short
    // lists blank a slot whose param already sits closer to the centre — such a
    // slot carries index == param_count, for which ParamAt() yields nullptr.
    static void CycleWindow(size_t  param_count,
                            size_t  active_col,
                            size_t* out_indices,
                            size_t& out_page);

    void Clear();
    void DrawCeilingLine();
    void DrawColumnSides(const ColumnGeom& col, bool active);
    void DrawDashedCenterLine(const ColumnGeom& col,
                              bool              full_width,
                              float             depth = 1.f);
    void DrawCenterMark(const ColumnGeom& col,
                        bool              active,
                        float             depth = 1.f);
    void DrawUnipolarBar(const ColumnGeom& col,
                         float             norm,
                         bool              active,
                         float             depth = 1.f);
    void DrawBipolarBar(const ColumnGeom& col,
                        float             norm,
                        bool              active,
                        float             depth = 1.f);
    void DrawToggle(const ColumnGeom&   col,
                    const ParameterDef& def,
                    bool                active,
                    float               depth = 1.f);
    void DrawCountBar(const ColumnGeom&   col,
                      const ParameterDef& def,
                      bool                active,
                      float               depth = 1.f);
    void DrawCountNum(const ColumnGeom&   col,
                      const ParameterDef& def,
                      bool                active,
                      float               depth = 1.f);
    void DrawSegmentedRow(const ParameterRegistry& reg,
                          const CycleRow&          row,
                          size_t                   active_col);
    void DrawPickupLine(const ColumnGeom&  col,
                        float              pot_norm,
                        ParamDisplayType   type);
    void DrawModDots(const ColumnGeom& col,
                     float             mod_norm,
                     ParamDisplayType  type);
    void DrawValueHeader(const ParameterRegistry& reg,
                         const CycleRow&          row,
                         size_t                   active_col,
                         bool                     show_cpu_meter,
                         float                    cpu_load,
                         bool                     governor);
    void DrawTrailLifeBar(int                x0,
                          int                y,
                          int                w,
                          int                h,
                          size_t             trail_index,
                          const TrailLifeUi& life,
                          uint8_t            rec_style);
    // Perseids embers: motion CenterOut (PRS) or L→R (PLR); visibility 0..1 soft gate.
    void DrawRecSparkleFill(int      x0,
                            int      y,
                            int      w,
                            int      h,
                            float    grow,
                            uint32_t seed,
                            bool     left_to_right,
                            float    visibility);
    // Recording life fill: solid bar grows center → L+R (style CTR).
    void DrawRecCenterSolid(int x0, int y, int w, int h, float grow);
    void DrawCrossfadeFocusBars(int    t_x,
                                int    t_w,
                                int    row0_y,
                                int    row_h,
                                size_t shown,
                                float  focus,
                                float  amp);

    void FormatValue(const ParameterDef& def, char* out, size_t out_len) const;
    void FormatCountLabel(const ParameterDef& def,
                          char*               out,
                          size_t              out_len) const;
    void FormatPosition(size_t index, size_t count, char* out, size_t out_len) const;

    // UI-only soft gate for Perseids rec (~200 ms in / out).
    enum class RecSoft : uint8_t
    {
        Idle = 0,
        FadeIn,
        FadeOut,
    };
    struct LifeBarAnim
    {
        TrailLifePhase last_phase = TrailLifePhase::Empty;
        RecSoft        soft       = RecSoft::Idle;
        uint32_t       soft_t0    = 0;
        float          grow_latch = 1.f;
        bool           ltr_latch  = false;
    };

    daisy::OledDisplay<daisy::SSD130x4WireSpi128x64Driver> display_;
    LifeBarAnim life_anim_[kTrailCount];
};

} // namespace perseids
