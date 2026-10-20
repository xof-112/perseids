// A small disting NT host for VCV Rack: runs one NT plug-in algorithm (here
// Perseids, perseids_nt.cpp unchanged) inside a Rack module.
//
// The host owns what the NT firmware owns: the algorithm's memory, the
// parameter values, the 28 busses, the screen, the controls (3 pots, 2
// encoders, 4 buttons, pot/encoder presses) and the parameter menu.
// Everything here runs on the Rack engine thread; the panel widget only reads
// a copy of the screen (CopyScreen).
//
// Portable C++ (no Rack headers), so test/host_test.cpp runs it natively.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "distingnt/api.h"

namespace nt_host
{
constexpr int kNumBusses   = kNT_lastBus; // 12 inputs, 8 outputs, 44 aux (API v14)
constexpr int kBlockFrames = 16; // frames per step() (multiple of 4)
constexpr int kScreenBytes = 128 * 64;

// Physical controls as the panel reports them.
struct Controls
{
    float pots[3]     = {0.5f, 0.5f, 0.5f};
    int   encoders[2] = {0, 0}; // detents since the last call
    // _NT_controls bits of the buttons held down: kNT_button1…4,
    // kNT_potButtonL/C/R, kNT_encoderButtonL/R.
    uint16_t buttons = 0;
    bool     menu    = false; // MENU button held
};

class Host
{
  public:
    Host();
    ~Host();
    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    // Builds the algorithm. `values` (by parameter name) restores a patch;
    // missing names keep their defaults. Returns false if construct failed.
    bool Create(const _NT_factory* factory, std::vector<int32_t> specs, uint32_t sample_rate,
                const std::vector<std::pair<std::string, int>>& values = {});
    void Destroy();
    bool Ok() const { return alg_ != nullptr; }

    // One frame of audio: inputs[0…11] are busses 1…12 (In L, In R, CV 1…10),
    // outputs[0…1] come from busses 13/14 (Output 1/2). One block of latency.
    void Frame(const float in[kNT_numInputBusses], float out[2]);

    // Controls, at any rate (the module calls it every 32 frames or so).
    void Ui(const Controls& c);

    // Redraws the screen (plug-in's draw() or the menu); call ~30×/s.
    void Draw();
    void CopyScreen(uint8_t dst[kScreenBytes]) const;

    // Parameters (index into the algorithm's own table).
    int         NumParams() const { return static_cast<int>(values_.size()); }
    const char* ParamName(int p) const; // stable name (as constructed)
    int         ParamValue(int p) const { return values_[p]; }
    void        SetParam(int p, int v); // clamped, calls parameterChanged()
    void        ResetParams();          // all to their defaults
    std::vector<std::pair<std::string, int>> Values() const;

    // Names of the busses in the menu (index 0 = bus 1); empty = "Input n" / "Output n".
    void SetBusNames(std::vector<std::string> names) { bus_names_ = std::move(names); }
    // The value as the menu shows it.
    std::string ValueString(int p) const
    {
        char b[kNT_parameterStringSize + 4];
        FormatValue(p, values_[p], b);
        return b;
    }

    const std::vector<int32_t>& Specs() const { return specs_; }
    bool                        MenuOpen() const { return menu_open_; }

    // --- called by the NT API functions (nt_api.cpp) ---
    _NT_algorithm* Alg() const { return alg_; }
    void           SetFromAudio(int p, int v);
    void           RequestSetupUi() { setup_ui_ = true; }
    uint8_t*       Screen() { return screen_; }

    // Parameter pages as the plug-in currently defines them (menu order).
    const _NT_parameterPages* Pages() const;
    int         NumPages() const;
    int         PageSize(int page) const;
    int         PageParam(int page, int i) const;
    const char* PageName(int page) const;
    // Live definition (min, max, unit …) and lookup by stable name (-1 if none).
    const _NT_parameter& ParamDef(int p) const { return alg_->parameters[p]; }
    int                  FindParam(const char* name) const;
    // The value v of parameter p as the menu would show it.
    std::string ValueString(int p, int v) const
    {
        char b[kNT_parameterStringSize + 4];
        FormatValue(p, v, b);
        return b;
    }

  private:
    void Step();
    void FlushAudioChanges();
    void CallCustomUi(uint16_t controls, const int8_t enc[2]);
    void MenuUi(const Controls& c, bool pot_moved[3]);
    void DrawMenu();
    int  FormatValue(int p, int v, char* buff) const;
    void ClampMenu();

    const _NT_factory*   factory_ = nullptr;
    _NT_algorithm*       alg_     = nullptr;
    std::vector<int32_t> specs_;
    void*                mem_[4]  = {nullptr, nullptr, nullptr, nullptr};
    std::vector<int16_t> values_;
    std::vector<std::string> names_;
    std::vector<std::string> bus_names_; // parameter names as constructed (persistence keys)
    std::vector<uint8_t> audio_changed_;
    uint32_t             custom_ = 0; // hasCustomUi()

    float bus_[kNumBusses * kBlockFrames] = {};
    float in_[kNT_numInputBusses][kBlockFrames]           = {};
    float out_[2][kBlockFrames]           = {};
    int   pos_                            = 0;

    // Controls as last seen.
    float    pots_[3]     = {-1.f, -1.f, -1.f};
    uint16_t last_buttons_ = 0;
    bool     last_menu_    = false;
    bool     setup_ui_     = true;

    // Parameter menu.
    bool menu_open_  = false;
    int  page_       = 0;
    int  item_       = 0;
    int  confirm_p_  = -1; // kNT_unitConfirm parameter waiting for Encoder R press
    int  confirm_v_  = 0;
    bool fast_       = false;

    uint8_t                  screen_[kScreenBytes] = {};
    uint8_t                  front_[kScreenBytes]  = {};
    mutable std::atomic_flag lock_                 = ATOMIC_FLAG_INIT;
    int                      slot_                 = -1; // index in the host registry
};

// An assignable knob (0…1, e.g. MIDI-mapped) bound to parameter p of a host:
// turned → p = min + x·(max − min), rounded; not turned → the knob follows p.
// Call at control rate in the audio thread; returns the knob position to show,
// or < 0 to leave the knob where it is.
struct KnobLink
{
    int   seen = -2;   // target handled last time (-2: none yet → sync first)
    float last = -1.f; // knob position handled last time
};
float Link(Host& h, KnobLink& l, int p, float x);

// The host whose algorithm is being called on this thread (for the NT API).
Host* Current();
// Host owning `alg`, or of index `i` (NT_algorithmIndex) — nullptr if none.
Host* ByAlgorithm(const _NT_algorithm* alg);
Host* ByIndex(int i);
int   IndexOf(const Host* h);
void  SetSampleRate(uint32_t sr);
} // namespace nt_host
