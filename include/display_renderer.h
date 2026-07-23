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
                       float               cpu_load       = 0.f);

    void DrawCycleView(const ParameterRegistry& reg,
                       const CycleRow&          row,
                       size_t                   active_col,
                       float                    modulated_norm = -1.f,
                       bool                     show_cpu_meter = false,
                       float                    cpu_load       = 0.f);

    void Present();

  private:
    struct ColumnGeom
    {
        int x;
        int w;
        int cx;
    };

    ColumnGeom ColumnGeometry(size_t index, size_t count) const;

    void Clear();
    void DrawCeilingLine();
    void DrawColumnSides(const ColumnGeom& col, bool active);
    void DrawDashedCenterLine(const ColumnGeom& col, bool full_width);
    void DrawUnipolarBar(const ColumnGeom& col,
                         float             norm,
                         bool              active);
    void DrawBipolarBar(const ColumnGeom& col, float norm, bool active);
    void DrawToggle(const ColumnGeom&   col,
                    const ParameterDef& def,
                    bool                active);
    void DrawCountBar(const ColumnGeom&   col,
                      const ParameterDef& def,
                      bool                active);
    void DrawCountNum(const ColumnGeom&   col,
                      const ParameterDef& def,
                      bool                active);
    void DrawSegmentedRow(const ParameterRegistry& reg,
                          const CycleRow&          row,
                          size_t                   active_col);
    void DrawPickupLine(const ColumnGeom& col, float pot_norm);
    void DrawModDots(const ColumnGeom& col,
                     float             mod_norm,
                     ParamDisplayType  type);
    void DrawValueHeader(const ParameterRegistry& reg,
                         const CycleRow&          row,
                         size_t                   active_col,
                         bool                     show_cpu_meter,
                         float                    cpu_load);
    void DrawTrailLifeBar(int                x0,
                          int                y,
                          int                w,
                          int                h,
                          const TrailLifeUi& life);

    void FormatValue(const ParameterDef& def, char* out, size_t out_len) const;
    void FormatPosition(size_t index, size_t count, char* out, size_t out_len) const;

    daisy::OledDisplay<daisy::SSD130x4WireSpi128x64Driver> display_;
};

} // namespace perseids
