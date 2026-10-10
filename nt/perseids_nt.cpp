// Perseids-core for the Expert Sleepers disting NT.
//
// The core of Perseids as one NT algorithm: Capture (five Trails), Spectra
// (additive FFT resynthesis), Swarm (granular) and the Blend between them.
// Reverb, filter, Pan Drift, Crossfade, Multi Dry/Wet and the mod system are
// left to the NT itself (its own algorithms, Add/Replace routing, CV mapping).
//
// The engines are the firmware's own sources (src/*_engine.cpp), built with
// PERSEIDS_TRAIL_INT16. This file is the NT platform layer: memory, routing,
// parameters, controls and display.
//
// GPL-3.0, like the rest of Perseids.

#include <distingnt/api.h>

#include "capture_engine.h"
#include "spectra_engine.h"
#include "reso_engine.h"
#include "swarm_engine.h"

#include <cmath>
#include <cstring>
#include <new>

using namespace perseids;

namespace
{

// ---------------------------------------------------------------------------
// Parameters

enum
{
    // Routing
    kParamInL,
    kParamInR,
    kParamRecIn,
    kParamOutL,
    kParamOutLMode,
    kParamOutR,
    kParamOutRMode,

    // Trails
    kParamCount,
    kParamThreshold,
    kParamContRec,
    kParamOverwrite,
    kParamCapture,
    kParamPlay,
    kParamClear,

    // Time
    kParamBuffer,
    kParamHold,
    kParamFadeIn,
    kParamFadeOut,

    // Engines
    kParamBlend,
    kParamPitchSpectra,
    kParamPitchSwarm,
    kParamOutLevel,

    // Spectra
    kParamPartials,
    kParamWaveshape,
    kParamUmbra,
    kParamEnsemble,

    // Swarm
    kParamSize,
    kParamSpread,
    kParamScan,
    kParamScatter,
    kParamAtmosphere,
    kParamDirection,

    // Mixer
    kParamTrailLevel1,
    kParamLock1   = kParamTrailLevel1 + kTrailCount,
    kParamSolo1   = kParamLock1 + kTrailCount,

    // Display
    kParamRecStyle = kParamSolo1 + kTrailCount,

    // Mod slots 1–4 (ARCHITECTURE 4.3), five parameters each
    kParamMod1,

    // Added after the mod slots so their indices stay put.
    kParamDryWet = kParamMod1 + 4 * 5,

    // Resonator (Block 7), on the Swarm output like in the firmware
    kParamResoMix,
    kParamResoDecay,
    kParamResoDamping,
    kParamResoSpread,
    kParamResoPitch,
    kParamResoQuant,
    kParamResoScale,
    kParamResoTuning,
    kParamResoVoct,
    kParamLevelMatch,

    kNumParams,
};

// Mod slot layout: kParamMod1 + slot * kModParams + field.
constexpr int kNumModSlots = 4;
enum
{
    kModIn,     // CV input bus; none = internal LFO (jack normalling, 4.10 "OFF")
    kModDest,   // destination, index into kModTargets (0 = off)
    kModAmount, // attenuverter −100…+100 %
    kModOffset, // bias after the attenuverter −100…+100 %
    kModRate,   // internal LFO rate, 0.01–20 Hz
    kModParams,
};
static_assert(kParamDryWet == kParamMod1 + kNumModSlots * kModParams, "mod slot layout");

// Everything a slot can modulate: all sound and Trail parameters. Not routing,
// not Clear trails (a held mod would clear forever), not the display or the
// mod slots themselves. Names in kModTargetNames, same order (0 = Off).
const uint8_t kModTargets[] = {
    0, // Off
    kParamCount, kParamThreshold, kParamContRec, kParamOverwrite, kParamCapture, kParamPlay,
    kParamBuffer, kParamHold, kParamFadeIn, kParamFadeOut,
    kParamBlend, kParamPitchSpectra, kParamPitchSwarm, kParamOutLevel,
    kParamPartials, kParamWaveshape, kParamUmbra, kParamEnsemble,
    kParamSize, kParamSpread, kParamScan, kParamScatter, kParamAtmosphere, kParamDirection,
    kParamTrailLevel1, kParamTrailLevel1 + 1, kParamTrailLevel1 + 2, kParamTrailLevel1 + 3,
    kParamTrailLevel1 + 4,
    kParamLock1, kParamLock1 + 1, kParamLock1 + 2, kParamLock1 + 3, kParamLock1 + 4,
    kParamSolo1, kParamSolo1 + 1, kParamSolo1 + 2, kParamSolo1 + 3, kParamSolo1 + 4,
    kParamDryWet,
    kParamResoMix, kParamResoDecay, kParamResoDamping, kParamResoSpread, kParamResoPitch,
    kParamResoQuant, kParamResoScale, kParamResoTuning,
};
const char* const kModTargetNames[] = {
    "Off",
    "Count", "Threshold", "Cont. Rec", "Overwrite", "Capture", "Play",
    "Buffer", "Hold", "Fade in", "Fade out",
    "Blend", "Pitch Spectra", "Pitch Swarm", "Output level",
    "Partials", "Waveshape", "Umbra/Aurora", "Ensemble",
    "Size", "Spread", "Scan", "Scatter", "Atmosphere", "Direction",
    "Level 1", "Level 2", "Level 3", "Level 4", "Level 5",
    "Lock 1", "Lock 2", "Lock 3", "Lock 4", "Lock 5",
    "Solo 1", "Solo 2", "Solo 3", "Solo 4", "Solo 5",
    "Dry/Wet",
    "Reso mix", "Reso decay", "Reso damping", "Reso spread", "Reso pitch",
    "Reso quantize", "Reso scale", "Reso tuning",
    nullptr,
};
constexpr int kNumModTargets = static_cast<int>(sizeof(kModTargets));
static_assert(sizeof(kModTargetNames) / sizeof(kModTargetNames[0]) == sizeof(kModTargets) + 1,
              "every mod target needs a name");

// Plug-in version, shown in the display header and the algorithm description.
// History in README.md (Versionen).
#define PERSEIDS_NT_VERSION "0.13"
const char* const kVersion = "v" PERSEIDS_NT_VERSION;

// Level match. Swarm (overlapping grains) comes out louder than Spectra (one
// steady partial set), most of all on short, percussive takes: 4–8 dB in the
// simulation; and Spectra grows when several Trails hold the same notes. Both
// engines are fed by the same Trail sum, so each one is levelled against it:
// a loudness follower (50 ms short-term, rises in 150 ms, falls over 2.5 s,
// so it follows the loud parts) on the Trail sum and on each engine output,
// and a slow gain (≈1.5 s) that brings the engine to kLevelMatchTarget × the
// Trail sum. Learns only while that engine runs and there is signal; ±12 dB
// at most, so it evens out the balance, it does not squash the dynamics.
constexpr float kLevelMatchTarget = 0.35f; // engine / Trail-sum loudness (≈ −9 dB, typical unmatched level)
// Swarm's grains are spikier than Spectra's steady partials at the same
// follower reading; −2 dB on its target makes them sound equally loud.
constexpr float kLevelMatchSwarmTrim = 0.8f;
constexpr float kLevelMatchMax    = 4.f;   // ±12 dB
constexpr float kLevelMatchShortS = 0.05f;
constexpr float kLevelMatchAtkS   = 0.15f;
constexpr float kLevelMatchRelS   = 2.5f;
constexpr float kLevelMatchGainS  = 1.5f;
constexpr float kLevelMatchFloor  = 1e-6f; // below −60 dB: nothing to learn

// Hold: the top value means "infinite" (engine: > 30 s).
constexpr int kHoldInf = 31;

const char* const kOffOn[]     = {"Off", "On", nullptr};
const char* const kDirection[] = {"Fwd", "Rev", "Rnd", nullptr};
// Recording graphic, as in the firmware's Settings → REC (same order there:
// 0 PRS centre-out embers, 1 PLR left→right embers, 2 CTR solid centre-out).
const char* const kRecStyle[] = {"PRS", "PLR", "CTR", nullptr};
const char* const kResoScales[]  = {"Major", "Minor", "Pentatonic", nullptr};
const char* const kResoTunings[] = {"Equal", "Just", nullptr};

#define P_NUM(n, lo, hi, d, u, sc) \
    {.name = n, .min = lo, .max = hi, .def = d, .unit = u, .scaling = sc, .enumStrings = nullptr},
#define P_ENUM(n, hi, d, strs) \
    {.name = n, .min = 0, .max = hi, .def = d, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = strs},
#define P_TRAIL_LEVEL(n) P_NUM(n, 0, 100, 50, kNT_unitPercent, 0)
#define P_TRAIL_TOGGLE(n) P_ENUM(n, 1, 0, kOffOn)

const _NT_parameter kParameterDefs[kNumParams] = {
    NT_PARAMETER_AUDIO_INPUT("In L", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("In R", 0, 2)
    NT_PARAMETER_CV_INPUT("Rec trig in", 0, 0)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)

    P_NUM("Count", 1, 5, 3, kNT_unitNone, 0)
    P_NUM("Threshold", 0, 100, 12, kNT_unitPercent, 0)
    P_ENUM("Cont. Rec", 1, 0, kOffOn)
    P_ENUM("Overwrite", 1, 1, kOffOn)
    P_ENUM("Capture", 1, 1, kOffOn)
    P_ENUM("Play", 1, 1, kOffOn)
    {.name = "Clear trails", .min = 0, .max = 1, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr},

    P_NUM("Buffer", 1, 300, 20, kNT_unitSeconds, kNT_scaling10)
    {.name = "Hold", .min = 0, .max = kHoldInf, .def = 15, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = nullptr},
    P_NUM("Fade in", 0, 50, 30, kNT_unitSeconds, kNT_scaling10)
    P_NUM("Fade out", 0, 50, 30, kNT_unitSeconds, kNT_scaling10)

    P_NUM("Blend", 0, 100, 50, kNT_unitPercent, 0)
    P_NUM("Pitch Spectra", -24, 24, 0, kNT_unitSemitones, 0)
    P_NUM("Pitch Swarm", -24, 24, 0, kNT_unitSemitones, 0)
    // Perseids' wet bus sits well below a Eurorack line (Trail levels 50 %,
    // −3 dB cloud pan); +12 dB brings a 10 Vpp input back to a similar level.
    P_NUM("Output level", -24, 24, 12, kNT_unitDb, 0)

    P_NUM("Partials", 4, 32, 16, kNT_unitNone, 0)
    P_NUM("Waveshape", -100, 100, 0, kNT_unitPercent, 0)
    P_NUM("Umbra/Aurora", -100, 100, 0, kNT_unitPercent, 0)
    P_NUM("Ensemble", 0, 100, 0, kNT_unitPercent, 0)

    P_NUM("Size", 4, 24, 16, kNT_unitNone, 0)
    P_NUM("Spread", 0, 100, 35, kNT_unitPercent, 0)
    P_NUM("Scan", 0, 100, 20, kNT_unitPercent, 0)
    P_NUM("Scatter", 0, 100, 75, kNT_unitPercent, 0)
    P_NUM("Atmosphere", -100, 100, 0, kNT_unitPercent, 0)
    P_ENUM("Direction", 2, 0, kDirection)

    P_TRAIL_LEVEL("Level 1") P_TRAIL_LEVEL("Level 2") P_TRAIL_LEVEL("Level 3")
    P_TRAIL_LEVEL("Level 4") P_TRAIL_LEVEL("Level 5")
    P_TRAIL_TOGGLE("Lock 1") P_TRAIL_TOGGLE("Lock 2") P_TRAIL_TOGGLE("Lock 3")
    P_TRAIL_TOGGLE("Lock 4") P_TRAIL_TOGGLE("Lock 5")
    P_TRAIL_TOGGLE("Solo 1") P_TRAIL_TOGGLE("Solo 2") P_TRAIL_TOGGLE("Solo 3")
    P_TRAIL_TOGGLE("Solo 4") P_TRAIL_TOGGLE("Solo 5")

    P_ENUM("Rec style", 2, 1, kRecStyle)

#define P_MOD_SLOT(n)                                                  \
    NT_PARAMETER_CV_INPUT("Mod " n " in", 0, 0)                        \
    P_ENUM("Mod " n " dest", kNumModTargets - 1, 0, kModTargetNames)   \
    P_NUM("Mod " n " amount", -100, 100, 0, kNT_unitPercent, 0)        \
    P_NUM("Mod " n " offset", -100, 100, 0, kNT_unitPercent, 0)        \
    P_NUM("Mod " n " LFO rate", 1, 2000, 25, kNT_unitHz, kNT_scaling100)
    P_MOD_SLOT("1") P_MOD_SLOT("2") P_MOD_SLOT("3") P_MOD_SLOT("4")

    // Like the firmware's Multi Dry/Wet: clean input ↔ cloud, equal power.
    // 100 % = cloud only, which is what the plug-in did before this existed.
    P_NUM("Dry/Wet", 0, 100, 100, kNT_unitPercent, 0)

    // Resonator: same ranges and defaults as Block 7 / Settings in the firmware.
    // Pitch ±12 semitones = the firmware's ±1 octave; V/Oct adds 1 V per octave
    // to the bank root (C2 at 0 V), so the resonance can follow a melody.
    P_NUM("Reso mix", 0, 100, 25, kNT_unitPercent, 0)
    P_NUM("Reso decay", 0, 100, 50, kNT_unitPercent, 0)
    P_NUM("Reso damping", 0, 100, 50, kNT_unitPercent, 0)
    P_NUM("Reso spread", 0, 100, 35, kNT_unitPercent, 0)
    P_NUM("Reso pitch", -12, 12, 0, kNT_unitSemitones, 0)
    P_ENUM("Reso quantize", 1, 0, kOffOn)
    P_ENUM("Reso scale", 2, 0, kResoScales)
    P_ENUM("Reso tuning", 1, 0, kResoTunings)
    NT_PARAMETER_CV_INPUT("Reso V/Oct in", 0, 0)

    // Matches Spectra and Swarm in loudness while both are heard, so Blend
    // crossfades between equally loud engines (NT only, see step()).
    P_ENUM("Level match", 1, 1, kOffOn)
};

const uint8_t kPageTrails[]  = {kParamCount, kParamThreshold, kParamContRec,
                                kParamOverwrite, kParamCapture, kParamPlay,
                                kParamClear};
const uint8_t kPageTime[]    = {kParamBuffer, kParamHold, kParamFadeIn, kParamFadeOut};
const uint8_t kPageEngines[] = {kParamBlend, kParamDryWet, kParamLevelMatch, kParamPitchSpectra,
                                kParamPitchSwarm, kParamOutLevel};
const uint8_t kPageSpectra[] = {kParamPartials, kParamWaveshape, kParamUmbra,
                                kParamEnsemble};
const uint8_t kPageSwarm[]   = {kParamSize, kParamSpread, kParamScan, kParamScatter,
                                kParamAtmosphere, kParamDirection};
const uint8_t kPageMixer[]   = {
    kParamTrailLevel1, kParamTrailLevel1 + 1, kParamTrailLevel1 + 2,
    kParamTrailLevel1 + 3, kParamTrailLevel1 + 4,
    kParamLock1, kParamLock1 + 1, kParamLock1 + 2, kParamLock1 + 3, kParamLock1 + 4,
    kParamSolo1, kParamSolo1 + 1, kParamSolo1 + 2, kParamSolo1 + 3, kParamSolo1 + 4};
const uint8_t kPageDisplay[] = {kParamRecStyle};
const uint8_t kPageReso[]    = {kParamResoMix,   kParamResoDecay, kParamResoDamping,
                                kParamResoSpread, kParamResoPitch, kParamResoQuant,
                                kParamResoScale,  kParamResoTuning, kParamResoVoct};
#define MOD_PAGE(s) {kParamMod1 + (s) * kModParams, kParamMod1 + (s) * kModParams + 1,      \
                     kParamMod1 + (s) * kModParams + 2, kParamMod1 + (s) * kModParams + 3, \
                     kParamMod1 + (s) * kModParams + 4}
const uint8_t kPageMod1[] = MOD_PAGE(0);
const uint8_t kPageMod2[] = MOD_PAGE(1);
const uint8_t kPageMod3[] = MOD_PAGE(2);
const uint8_t kPageMod4[] = MOD_PAGE(3);
const uint8_t kPageRouting[] = {kParamInL, kParamInR, kParamRecIn, kParamOutL,
                                kParamOutLMode, kParamOutR, kParamOutRMode};

const _NT_parameterPage kPages[] = {
    {.name = "Trails", .numParams = ARRAY_SIZE(kPageTrails), .group = 1, .unused = {0, 0}, .params = kPageTrails},
    {.name = "Time", .numParams = ARRAY_SIZE(kPageTime), .group = 2, .unused = {0, 0}, .params = kPageTime},
    {.name = "Engines", .numParams = ARRAY_SIZE(kPageEngines), .group = 3, .unused = {0, 0}, .params = kPageEngines},
    {.name = "Spectra", .numParams = ARRAY_SIZE(kPageSpectra), .group = 4, .unused = {0, 0}, .params = kPageSpectra},
    {.name = "Swarm", .numParams = ARRAY_SIZE(kPageSwarm), .group = 5, .unused = {0, 0}, .params = kPageSwarm},
    // Resonator sits on the Swarm output, so its page follows Swarm.
    {.name = "Resonator", .numParams = ARRAY_SIZE(kPageReso), .group = 10, .unused = {0, 0}, .params = kPageReso},
    {.name = "Mixer", .numParams = ARRAY_SIZE(kPageMixer), .group = 6, .unused = {0, 0}, .params = kPageMixer},
    // Same group: the cursor keeps its row when stepping Mod 1 → Mod 4.
    {.name = "Mod 1", .numParams = kModParams, .group = 9, .unused = {0, 0}, .params = kPageMod1},
    {.name = "Mod 2", .numParams = kModParams, .group = 9, .unused = {0, 0}, .params = kPageMod2},
    {.name = "Mod 3", .numParams = kModParams, .group = 9, .unused = {0, 0}, .params = kPageMod3},
    {.name = "Mod 4", .numParams = kModParams, .group = 9, .unused = {0, 0}, .params = kPageMod4},
    {.name = "Display", .numParams = ARRAY_SIZE(kPageDisplay), .group = 8, .unused = {0, 0}, .params = kPageDisplay},
    {.name = "Routing", .numParams = ARRAY_SIZE(kPageRouting), .group = 7, .unused = {0, 0}, .params = kPageRouting},
};

const _NT_parameterPages kParameterPages = {
    .numPages = ARRAY_SIZE(kPages),
    .pages    = kPages,
};

// Specification: recording time each Trail can hold. Sets the DRAM request
// (16 bit: 5 Trails × 10 s ≈ 4.8 MB at 48 kHz).
enum
{
    kSpecTrailSeconds,
    kNumSpecs,
};
const _NT_specification kSpecs[kNumSpecs] = {
    {.name = "Trail seconds", .min = 1, .max = 30, .def = 10, .type = kNT_typeSeconds},
};

// ---------------------------------------------------------------------------
// Instance

// Audio is processed in chunks of at most this many frames, so the scratch
// buffers stay small whatever block size the host uses.
constexpr int kChunk = 64;

// Spectra analysis and the Swarm envelope rebuild advance one slice per this
// many samples (a 1024-sample hop needs 10 slices; here there are 32).
constexpr uint32_t kSliceSamples = 32;

// The Swarm governor and RecordSource are tuned for 256-sample blocks.
constexpr uint32_t kGovernorSamples = 256;

// NT audio is in volts; Perseids works at codec scale (±1 ≈ ±5 V).
constexpr float kVoltsToUnit = 0.2f;
constexpr float kUnitToVolts = 5.f;

struct Engines
{
    CaptureEngine capture;
    SpectraEngine spectra;
    SwarmEngine     swarm;
    ResonatorEngine reso;

    float in_l[kChunk];
    float in_r[kChunk];
    float dry_l[kChunk];
    float dry_r[kChunk];
    float trail_mix[kChunk];
    float sp_l[kChunk];
    float sp_r[kChunk];
    float sw_l[kChunk];
    float sw_r[kChunk];
};

// Encoder-button click, for the NT and for nt_emu (same scheme as Duett):
// NT:      press c=1 l=0, hold c=1 l=1, release c=0 l=1
// nt_emu:  press c=1 l=0, release c=1 l=1; dragging an encoder presses it,
//          so a press does not count when the encoder was turned meanwhile.
struct Click
{
    bool down;
    bool turned;
};

// Recording embers: soft appear / burn-out around a take (UI only; audio has
// no gap). Mirrors DisplayRenderer::LifeBarAnim in the firmware.
enum class RecSoft : uint8_t
{
    Idle,
    FadeIn,
    FadeOut,
};

struct LifeBarAnim
{
    TrailLifePhase last_phase;
    RecSoft        soft;
    uint32_t       soft_t0;
    float          grow_latch;
    bool           ltr_latch;
};

struct PerseidsAlgorithm : public _NT_algorithm
{
    Engines* eng;

    // Per instance, so Buffer's maximum can follow the specification.
    _NT_parameter params[kNumParams];

    float trail_seconds;

    // Parameters → engines: parameterChanged sets the flag, step() applies.
    volatile bool dirty;
    float         blend;
    float         out_gain;
    float         dry_wet;
    bool          playing;
    bool          level_match;

    // Level match: loudness of each engine (mean square, before any gain),
    // the Swarm/Spectra ratio and the gains applied right now.
    struct Loudness
    {
        float st  = 0.f; // short-term mean square
        float env = 0.f; // follower on it (fast up, slow down)
    };
    Loudness lm_in, lm_sp, lm_sw;
    float    lm_g_sp; // gains applied right now
    float    lm_g_sw;

    CaptureParamValues capture_p;
    SpatialParamValues spatial_p;
    TrailMixerState    mixer[kTrailCount];
    SpectraParamValues spectra_p;
    ResoParamValues    reso_p;
    float              reso_scale;
    float              reso_tuning;
    SwarmParamValues   swarm_p;

    // Rec trig input (Schmitt trigger, in volts).
    bool rec_gate;

    // Housekeeping counters (samples).
    uint64_t sample_clock; // also the display's animation clock
    uint32_t slice_acc;
    uint32_t gov_acc;
    uint32_t gov_cycles;

    // Controls
    int   selected;    // Trail 0…4
    bool  pot_l_mix;   // Pot L: false = Blend, true = Dry/Wet
    bool  pot_c_reso;  // Pot C: false = Scan, true = Reso mix
    bool  pot_r_atmo;  // Pot R: false = Size, true = Atmosphere
    // Catch-up per pot: after a switch (or a change from the menu) the pot only
    // takes over once it reaches or passes the stored value, so nothing jumps.
    bool    pot_caught[3];
    float   pot_last[3];    // last position seen, < 0 = unknown
    int16_t pot_written[3]; // value this pot wrote last (detects outside edits)
    int     pot_param[3];   // parameter the catch state belongs to
    int   hold_before; // Hold value to restore when Hold is toggled off
    Click click_l;
    Click click_r;

    // Life-bar animation per Trail (display thread only), as in the firmware.
    LifeBarAnim life_anim[kTrailCount];

    // Mod slots (audio thread): contribution per destination in units of the
    // destination's full travel, internal LFO phases, smoothed CV.
    float mod_sum[kNumParams];
    float lfo_phase[kNumModSlots];
    float cv_smooth[kNumModSlots];
    int   mod_active;  // slots currently doing something (display)
    bool  mod_applied; // modulation was applied in the last chunk
};

inline float Clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline int ParamValue(const PerseidsAlgorithm* a, int p)
{
    int v = a->v[p];
    if(v < a->params[p].min)
        v = a->params[p].min;
    if(v > a->params[p].max)
        v = a->params[p].max;
    return v;
}

// Value a parameter has right now: the stored (base) value plus all mod slot
// contributions, contrib × the parameter's full travel, clamped (ARCHITECTURE
// 4.3: dest = clamp(base + contrib × span)). The stored value never moves.
inline float Pv(const PerseidsAlgorithm* a, int p)
{
    const float lo   = a->params[p].min;
    const float hi   = a->params[p].max;
    const float base = static_cast<float>(ParamValue(a, p));
    return Clampf(base + a->mod_sum[p] * (hi - lo), lo, hi);
}

// Same, rounded — for counts, switches and enums.
inline int PvI(const PerseidsAlgorithm* a, int p)
{
    const float x = Pv(a, p);
    return static_cast<int>(x >= 0.f ? x + 0.5f : x - 0.5f);
}

// nt_emu (Windows build, NT_EMU_WIN): NT_setParameterFrom… always lands in the
// most recently created nt_emu instance. There the plug-in writes its own v[]
// directly (that is this instance's parameter table in nt_emu).
void parameterChanged(_NT_algorithm* self, int p);

void SetParamUi(PerseidsAlgorithm* a, int p, int value)
{
    if(value < a->params[p].min)
        value = a->params[p].min;
    if(value > a->params[p].max)
        value = a->params[p].max;
#ifdef NT_EMU_WIN
    if(a->v)
    {
        const_cast<int16_t*>(a->v)[p] = static_cast<int16_t>(value);
        parameterChanged(a, p);
        return;
    }
#endif
    NT_setParameterFromUi(NT_algorithmIndex(a), p + NT_parameterOffset(), value);
}

void SetParamAudio(PerseidsAlgorithm* a, int p, int value)
{
#ifdef NT_EMU_WIN
    if(a->v)
    {
        const_cast<int16_t*>(a->v)[p] = static_cast<int16_t>(value);
        a->dirty = true;
        return;
    }
#endif
    NT_setParameterFromAudio(NT_algorithmIndex(a), p + NT_parameterOffset(), value);
}

// Reads every parameter into the engine structs.
void ReadParams(PerseidsAlgorithm* a)
{
    CaptureParamValues& c = a->capture_p;
    c.count      = static_cast<float>(PvI(a, kParamCount));
    c.threshold  = Pv(a, kParamThreshold) * 0.01f;
    c.cont_rec   = static_cast<float>(PvI(a, kParamContRec));
    c.overwrite  = static_cast<float>(PvI(a, kParamOverwrite));
    c.on_off     = static_cast<float>(PvI(a, kParamCapture));
    c.buffer_s   = Pv(a, kParamBuffer) * 0.1f;
    c.hold_s     = Pv(a, kParamHold);
    c.fade_in_s  = Clampf(Pv(a, kParamFadeIn) * 0.1f, 0.001f, 5.f);
    c.fade_out_s = Clampf(Pv(a, kParamFadeOut) * 0.1f, 0.001f, 5.f);
    c.routing    = 0.f; // Stereo; the NT chooses the busses.
    a->playing   = PvI(a, kParamPlay) != 0;

    // Pan Drift / Crossfade are left to the NT: Trails stay centred.
    a->spatial_p                 = SpatialParamValues{};
    a->spatial_p.pan_amplitude   = 0.f;
    a->spatial_p.xfade_amplitude = 0.f;

    for(size_t t = 0; t < kTrailCount; ++t)
    {
        a->mixer[t].level  = Pv(a, static_cast<int>(kParamTrailLevel1 + t)) * 0.01f;
        a->mixer[t].locked = PvI(a, static_cast<int>(kParamLock1 + t)) != 0;
        a->mixer[t].solo   = PvI(a, static_cast<int>(kParamSolo1 + t)) != 0;
    }

    // Pitch: semitones. The engines map ±1 → ±span octaves; span 2 (Pitch
    // Both = 1) makes ±24 semitones exact.
    SpectraParamValues& s = a->spectra_p;
    s.pitch_spectra = Pv(a, kParamPitchSpectra) / 24.f;
    s.partials      = static_cast<float>(PvI(a, kParamPartials));
    s.waveshape     = Pv(a, kParamWaveshape) * 0.01f;
    s.umbra_aurora  = Pv(a, kParamUmbra) * 0.01f;
    s.ensemble      = Pv(a, kParamEnsemble) * 0.01f;

    SwarmParamValues& w = a->swarm_p;
    a->blend            = Pv(a, kParamBlend) * 0.01f;
    w.blend             = a->blend;
    w.pitch_swarm       = Pv(a, kParamPitchSwarm) / 24.f;
    w.pitch_both        = 1.f;
    w.size              = static_cast<float>(PvI(a, kParamSize));
    w.spread            = Pv(a, kParamSpread) * 0.01f;
    w.scan              = Pv(a, kParamScan) * 0.01f;
    w.scatter           = Pv(a, kParamScatter) * 0.01f;
    w.atmosphere        = Pv(a, kParamAtmosphere) * 0.01f;
    w.direction         = static_cast<float>(PvI(a, kParamDirection));

    a->out_gain = std::pow(10.f, Pv(a, kParamOutLevel) / 20.f);
    a->dry_wet  = Pv(a, kParamDryWet) * 0.01f;
    a->level_match = ParamValue(a, kParamLevelMatch) != 0;

    ResoParamValues& r = a->reso_p;
    r.mix              = Pv(a, kParamResoMix) * 0.01f;
    r.decay            = Pv(a, kParamResoDecay) * 0.01f;
    r.damping          = Pv(a, kParamResoDamping) * 0.01f;
    r.spread           = Pv(a, kParamResoSpread) * 0.01f;
    r.pitch            = Pv(a, kParamResoPitch) / 12.f;
    r.quantized        = static_cast<float>(PvI(a, kParamResoQuant));
    a->reso_scale      = static_cast<float>(PvI(a, kParamResoScale));
    a->reso_tuning     = static_cast<float>(PvI(a, kParamResoTuning));
}

// ---------------------------------------------------------------------------
// Memory

constexpr size_t Align16(size_t n) { return (n + 15u) & ~static_cast<size_t>(15u); }

size_t TrailCapacity(float seconds, float sample_rate)
{
    // Recording length plus room for the 40 ms loop seam.
    return static_cast<size_t>(seconds * sample_rate)
           + static_cast<size_t>(0.05f * sample_rate);
}

int SpecSeconds(const int32_t* specifications)
{
    int s = specifications ? specifications[kSpecTrailSeconds] : kSpecs[0].def;
    if(s < kSpecs[0].min)
        s = kSpecs[0].min;
    if(s > kSpecs[0].max)
        s = kSpecs[0].max;
    return s;
}

struct DramLayout
{
    size_t engines;
    size_t window;
    size_t mags;
    size_t smooth;
    size_t ring;
    size_t trails;
    size_t capacity;
    size_t total;
};

DramLayout Layout(int seconds)
{
    const float sr = static_cast<float>(NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000);
    DramLayout l;
    size_t     at = 0;
    l.engines     = at;
    at += Align16(sizeof(Engines));
    l.window = at;
    at += Align16(SpectraEngine::kFftSize * sizeof(float));
    l.mags = at;
    at += Align16(SpectraEngine::kBinCount * sizeof(float));
    l.smooth = at;
    at += Align16(SpectraEngine::kBinCount * sizeof(float));
    l.ring = at;
    at += Align16(SpectraEngine::kInputRing * sizeof(float));
    l.capacity = TrailCapacity(static_cast<float>(seconds), sr);
    l.trails   = at;
    at += Align16(kTrailCount * l.capacity * sizeof(TrailSample));
    l.total = at;
    return l;
}

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
    req.numParameters = kNumParams;
    req.sram          = sizeof(PerseidsAlgorithm);
    req.dram          = static_cast<uint32_t>(Layout(SpecSeconds(specifications)).total + 16);
    req.dtc           = 0;
    req.itc           = 0;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements& /*req*/,
                         const int32_t* specifications)
{
    PerseidsAlgorithm* a = new(ptrs.sram) PerseidsAlgorithm();

    const int        seconds = SpecSeconds(specifications);
    const DramLayout l       = Layout(seconds);
    uint8_t* const   base    = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(ptrs.dram) + 15u) & ~static_cast<uintptr_t>(15u));

    a->eng = new(base + l.engines) Engines();

    std::memcpy(a->params, kParameterDefs, sizeof(a->params));
    a->params[kParamBuffer].max = static_cast<int16_t>(seconds * 10);
    if(a->params[kParamBuffer].def > a->params[kParamBuffer].max)
        a->params[kParamBuffer].def = a->params[kParamBuffer].max;
    a->parameters     = a->params;
    a->parameterPages = &kParameterPages;

    const float sr = static_cast<float>(NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000);
    a->trail_seconds = static_cast<float>(seconds);

    CaptureEngine::TrailBank bank;
    TrailSample* trails = reinterpret_cast<TrailSample*>(base + l.trails);
    for(size_t t = 0; t < kTrailCount; ++t)
        bank.data[t] = trails + t * l.capacity;
    bank.capacity = l.capacity;
    a->eng->capture.Init(sr, bank);

    a->eng->spectra.Init(sr,
                         SpectraEngine::Buffers{reinterpret_cast<float*>(base + l.window),
                                                reinterpret_cast<float*>(base + l.mags),
                                                reinterpret_cast<float*>(base + l.smooth),
                                                reinterpret_cast<float*>(base + l.ring)});
    a->eng->swarm.Init(sr, &a->eng->capture);
    a->eng->reso.Init(sr);

    a->dirty        = true;
    a->blend        = 0.5f;
    a->level_match  = true;
    a->lm_in        = {};
    a->lm_sp        = {};
    a->lm_sw        = {};
    a->lm_g_sp      = 1.f;
    a->lm_g_sw      = 1.f;
    a->out_gain     = 1.f;
    a->playing      = true;
    a->rec_gate     = false;
    a->sample_clock = 0;
    a->slice_acc    = 0;
    a->gov_acc      = 0;
    a->gov_cycles   = 0;
    a->selected     = 0;
    a->pot_l_mix    = false;
    a->pot_c_reso   = false;
    a->pot_r_atmo   = false;
    for(int k = 0; k < 3; ++k)
    {
        a->pot_caught[k]  = false;
        a->pot_last[k]    = -1.f;
        a->pot_written[k] = 0;
        a->pot_param[k]   = -1;
    }
    a->dry_wet      = 1.f;
    a->hold_before  = 15;
    a->click_l      = Click{false, false};
    a->click_r      = Click{false, false};
    for(size_t t = 0; t < kTrailCount; ++t)
        a->life_anim[t] = LifeBarAnim{TrailLifePhase::Empty, RecSoft::Idle, 0, 1.f, false};
    for(int p = 0; p < kNumParams; ++p)
        a->mod_sum[p] = 0.f;
    for(int m = 0; m < kNumModSlots; ++m)
    {
        a->lfo_phase[m] = 0.25f * static_cast<float>(m); // slots start spread out
        a->cv_smooth[m] = 0.f;
    }
    a->mod_active  = 0;
    a->mod_applied = false;
    return a;
}

void parameterChanged(_NT_algorithm* self, int p)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    a->dirty             = true;
    // Changed from the menu (or a preset): the pot has to catch up again.
    for(int k = 0; k < 3; ++k)
        if(a->pot_param[k] == p && a->v && a->v[p] != a->pot_written[k])
            a->pot_caught[k] = false;
    if(p == kParamHold && a->v && a->v[kParamHold] != kHoldInf)
        a->hold_before = a->v[kParamHold];
}

// ---------------------------------------------------------------------------
// Audio

// Same rational soft limit as the firmware's Multi stage.
inline float SoftLimit(float x)
{
    const float a = x >= 0.f ? x : -x;
    return x * (27.f + a * a) / (27.f + 9.f * a * a);
}

// Internal source when a slot has no CV input: the firmware's triangle/sine
// blend (same shape as the Pan Drift LFO), −1…+1.
float ModLfo(float phase)
{
    float ph = phase - static_cast<float>(static_cast<int>(phase));
    if(ph < 0.f)
        ph += 1.f;
    float tri;
    if(ph < 0.25f)
        tri = ph * 4.f;
    else if(ph < 0.75f)
        tri = 2.f - ph * 4.f;
    else
        tri = ph * 4.f - 4.f;
    return tri * 0.55f + std::sin(ph * 6.2831853f) * 0.45f;
}

// Mod slots for one chunk: contrib = Offset + Amount × source, summed per
// destination (ARCHITECTURE 4.3). CV: ±5 V = ±1, smoothed over ~5 ms.
// Returns true if any slot is doing something.
bool UpdateMod(PerseidsAlgorithm* a, const float* busFrames, int numFrames, int at, int n)
{
    for(int p = 0; p < kNumParams; ++p)
        a->mod_sum[p] = 0.f;

    const float sr     = static_cast<float>(NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000);
    int         active = 0;
    for(int m = 0; m < kNumModSlots; ++m)
    {
        const int   base   = kParamMod1 + m * kModParams;
        const int   dest   = kModTargets[ParamValue(a, base + kModDest)];
        const float amount = ParamValue(a, base + kModAmount) * 0.01f;
        const float offset = ParamValue(a, base + kModOffset) * 0.01f;
        const int   bus    = ParamValue(a, base + kModIn);

        float source;
        if(bus > 0)
        {
            const float* cv  = busFrames + (bus - 1) * numFrames + at;
            float        sum = 0.f;
            for(int i = 0; i < n; ++i)
                sum += cv[i];
            const float target = Clampf(sum / static_cast<float>(n) * 0.2f, -1.f, 1.f);
            a->cv_smooth[m] += (target - a->cv_smooth[m]) * static_cast<float>(n)
                               / (static_cast<float>(n) + 0.005f * sr);
            source = a->cv_smooth[m];
        }
        else
        {
            a->lfo_phase[m] += ParamValue(a, base + kModRate) * 0.01f * static_cast<float>(n) / sr;
            a->lfo_phase[m] -= static_cast<float>(static_cast<int>(a->lfo_phase[m]));
            source = ModLfo(a->lfo_phase[m]);
        }

        if(!dest || (amount == 0.f && offset == 0.f))
            continue;
        a->mod_sum[dest] += offset + amount * source;
        ++active;
    }
    a->mod_active = active;
    return active > 0;
}

void ApplyParams(PerseidsAlgorithm* a)
{
    a->dirty = false; // clear first: a change during the read sets it again
    ReadParams(a);
    Engines& e = *a->eng;
    e.capture.SyncFromUi(a->capture_p, a->mixer, a->playing, a->spatial_p);
    e.spectra.SyncFromUi(a->spectra_p, a->swarm_p.pitch_both);
    e.swarm.SetParams(a->swarm_p);
    e.reso.SyncFromUi(a->reso_p, a->reso_scale, a->reso_tuning);
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    if(!a->v)
        return;
#if defined(__arm__)
    const uint32_t cycles_start = NT_getCpuCycleCount();
#endif

    if(a->dirty)
        ApplyParams(a);
    const float sr = static_cast<float>(NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000);

    // "Clear trails" acts like a button: clear once, then fall back to 0.
    if(a->v[kParamClear])
    {
        a->eng->capture.ClearAll();
        SetParamAudio(a, kParamClear, 0);
    }

    Engines&  e         = *a->eng;
    const int numFrames = numFramesBy4 * 4;

    const int in_l_bus  = ParamValue(a, kParamInL);
    const int in_r_bus  = ParamValue(a, kParamInR);
    const int rec_bus   = ParamValue(a, kParamRecIn);
    const int out_l_bus = ParamValue(a, kParamOutL);
    const int out_r_bus = ParamValue(a, kParamOutR);
    const bool replace_l = a->v[kParamOutLMode] != 0;
    const bool replace_r = a->v[kParamOutRMode] != 0;

    const float* in_l  = in_l_bus > 0 ? busFrames + (in_l_bus - 1) * numFrames : nullptr;
    const float* in_r  = in_r_bus > 0 ? busFrames + (in_r_bus - 1) * numFrames : nullptr;
    const float* rec   = rec_bus > 0 ? busFrames + (rec_bus - 1) * numFrames : nullptr;
    float*       out_l = out_l_bus > 0 ? busFrames + (out_l_bus - 1) * numFrames : nullptr;
    float*       out_r = out_r_bus > 0 ? busFrames + (out_r_bus - 1) * numFrames : nullptr;

    for(int done = 0; done < numFrames; done += kChunk)
    {
        const int n = numFrames - done < kChunk ? numFrames - done : kChunk;

        // Mod slots: re-read every parameter while any slot is active, and
        // once more after the last one stops (back to the stored values).
        const bool modulating = UpdateMod(a, busFrames, numFrames, done, n);

        // Resonator V/Oct: chunk average, held to whole cents so CV noise does
        // not retune the bank every chunk. No cable = no offset.
        {
            const int voct_bus = ParamValue(a, kParamResoVoct);
            float     oct      = 0.f;
            if(voct_bus > 0)
            {
                const float* cv  = busFrames + (voct_bus - 1) * numFrames + done;
                float        sum = 0.f;
                for(int i = 0; i < n; ++i)
                    sum += cv[i];
                const float cents = Clampf(sum / static_cast<float>(n), -5.f, 5.f) * 1200.f;
                oct = static_cast<float>(static_cast<int>(cents >= 0.f ? cents + 0.5f : cents - 0.5f))
                      / 1200.f;
            }
            e.reso.SetRootOffset(oct);
        }
        if(modulating || a->mod_applied || a->dirty)
        {
            if(!modulating)
                for(int p = 0; p < kNumParams; ++p)
                    a->mod_sum[p] = 0.f;
            ApplyParams(a);
            a->mod_applied = modulating;
        }

        // Blend: equal-power, the silent engine is skipped (as in the firmware).
        const float blend       = Clampf(a->blend, 0.f, 1.f);
        const float wet_spectra = std::cos(blend * 1.5707964f);
        const float wet_swarm   = std::sin(blend * 1.5707964f);
        const bool  run_spectra = wet_spectra > 0.001f;
        const bool  run_swarm   = wet_swarm > 0.001f;

        for(int i = 0; i < n; ++i)
        {
            e.in_l[i] = in_l ? in_l[done + i] * kVoltsToUnit : 0.f;
            e.in_r[i] = in_r ? in_r[done + i] * kVoltsToUnit : 0.f;
        }

        // Rec trig: rising edge above 1 V, re-armed below 0.5 V.
        if(rec)
        {
            for(int i = 0; i < n; ++i)
            {
                const float x = rec[done + i];
                if(!a->rec_gate && x > 1.f)
                {
                    a->rec_gate = true;
                    e.capture.RequestManualTrigger();
                }
                else if(a->rec_gate && x < 0.5f)
                    a->rec_gate = false;
            }
        }

        e.capture.Process(e.in_l, e.in_r, e.dry_l, e.dry_r, e.trail_mix, n);
        e.spectra.PushInput(e.trail_mix, n);

        float sp_ms = 0.f; // mean squares before any gain, for Level match
        float sw_ms = 0.f;
        float g_sp0 = 1.f; // gains at the start of this chunk
        float g_sw0 = 1.f;
        if(run_spectra)
        {
            e.spectra.Process(e.sp_l, e.sp_l, n);
            for(int i = 0; i < n; ++i)
                sp_ms += e.sp_l[i] * e.sp_l[i];
            sp_ms /= static_cast<float>(n);
        }
        if(run_swarm)
        {
            e.swarm.Process(e.sw_l, e.sw_r, n);
            // Spectral Resonator sits on the Swarm output (ARCHITECTURE 4.1
            // Block 7), so at Blend 0 % (Spectra only) it is not heard.
            e.reso.Process(e.sw_l, e.sw_r, n);
            for(int i = 0; i < n; ++i)
                sw_ms += 0.5f * (e.sw_l[i] * e.sw_l[i] + e.sw_r[i] * e.sw_r[i]);
            sw_ms /= static_cast<float>(n);
        }

        // Level match (see kLevelMatch…).
        {
            const float fn    = static_cast<float>(n);
            const float k_st  = 1.f - std::exp(-fn / (kLevelMatchShortS * sr));
            const float k_atk = 1.f - std::exp(-fn / (kLevelMatchAtkS * sr));
            const float k_rel = 1.f - std::exp(-fn / (kLevelMatchRelS * sr));
            const float k_g   = 1.f - std::exp(-fn / (kLevelMatchGainS * sr));
            auto Follow = [&](PerseidsAlgorithm::Loudness& L, float ms) {
                L.st += k_st * (ms - L.st);
                L.env += (L.st > L.env ? k_atk : k_rel) * (L.st - L.env);
            };
            float in_ms = 0.f;
            for(int i = 0; i < n; ++i)
                in_ms += e.trail_mix[i] * e.trail_mix[i];
            Follow(a->lm_in, in_ms / fn);
            auto Learn = [&](PerseidsAlgorithm::Loudness& L, float ms, float& g, float target) {
                Follow(L, ms);
                float to = g;
                if(!a->level_match)
                    to = 1.f;
                else if(a->lm_in.env > kLevelMatchFloor && L.env > kLevelMatchFloor * 1e-2f)
                    to = Clampf(target * std::sqrt(a->lm_in.env / L.env),
                                1.f / kLevelMatchMax, kLevelMatchMax);
                g += k_g * (to - g);
            };
            g_sp0 = a->lm_g_sp;
            g_sw0 = a->lm_g_sw;
            if(run_spectra)
                Learn(a->lm_sp, sp_ms, a->lm_g_sp, kLevelMatchTarget);
            if(run_swarm)
                Learn(a->lm_sw, sw_ms, a->lm_g_sw, kLevelMatchTarget * kLevelMatchSwarmTrim);
        }
        const float inv_n    = 1.f / static_cast<float>(n);

        if(run_spectra)
        {
            const CaptureEngine::CloudPan cp = e.capture.LastCloudPan();
            for(int i = 0; i < n; ++i)
            {
                const float g = (g_sp0 + (a->lm_g_sp - g_sp0) * (i + 1) * inv_n) * wet_spectra;
                const float s = e.sp_l[i];
                e.sp_l[i]     = s * cp.l * g;
                e.sp_r[i]     = s * cp.r * g;
            }
        }
        if(run_swarm)
        {
            for(int i = 0; i < n; ++i)
            {
                const float g = g_sw0 + (a->lm_g_sw - g_sw0) * (i + 1) * inv_n;
                e.sw_l[i] *= g;
                e.sw_r[i] *= g;
            }
        }

        // Dry/Wet as in the firmware's Multi stage: equal power, dry trimmed
        // to 0.85 so the clean signal does not dominate at the middle. The
        // dry side is the clean stereo input (In R unpatched → In L on both).
        const float dw    = Clampf(a->dry_wet, 0.f, 1.f);
        const float dry_g = dw >= 0.999f ? 0.f : std::cos(dw * 1.5707964f) * 0.85f;
        const float wet_g = (dw >= 0.999f ? 1.f : std::sin(dw * 1.5707964f)) * a->out_gain;
        const bool  dry_r = in_r != nullptr;

        for(int i = 0; i < n; ++i)
        {
            float l = 0.f;
            float r = 0.f;
            if(run_spectra)
            {
                l += e.sp_l[i];
                r += e.sp_r[i];
            }
            if(run_swarm)
            {
                l += e.sw_l[i] * wet_swarm;
                r += e.sw_r[i] * wet_swarm;
            }
            const float dl = e.in_l[i];
            const float dr = dry_r ? e.in_r[i] : dl;
            l = SoftLimit(l * wet_g + dl * dry_g) * kUnitToVolts;
            r = SoftLimit(r * wet_g + dr * dry_g) * kUnitToVolts;
            if(out_l)
            {
                if(replace_l)
                    out_l[done + i] = l;
                else
                    out_l[done + i] += l;
            }
            if(out_r)
            {
                if(replace_r)
                    out_r[done + i] = r;
                else
                    out_r[done + i] += r;
            }
        }

        // Background work, a slice at a time: Spectra analysis (skipped at
        // full Swarm, like the firmware) and the Swarm envelope rebuild.
        a->slice_acc += static_cast<uint32_t>(n);
        while(a->slice_acc >= kSliceSamples)
        {
            a->slice_acc -= kSliceSamples;
            if(a->blend < 0.98f)
                e.spectra.AnalysisSlice();
            e.swarm.WindowSlice();
        }
    }

    a->sample_clock += static_cast<uint64_t>(numFrames);

    // Swarm load governor, once per 256 samples. On the NT the load is this
    // algorithm's own share of the CPU; elsewhere (nt_emu) it stays idle.
#if defined(__arm__)
    a->gov_cycles += NT_getCpuCycleCount() - cycles_start;
#endif
    a->gov_acc += static_cast<uint32_t>(numFrames);
    if(a->gov_acc >= kGovernorSamples)
    {
        float load = 0.f;
#if defined(__arm__)
        // 600 MHz core; Perseids may take up to half of it before thinning.
        const float budget = 0.5f * 600e6f
                             * (static_cast<float>(a->gov_acc)
                                / static_cast<float>(NT_globals.sampleRate));
        load = static_cast<float>(a->gov_cycles) / budget;
#endif
        e.swarm.UpdateGovernor(load);
        a->gov_acc    = 0;
        a->gov_cycles = 0;
    }
}

// ---------------------------------------------------------------------------
// Controls
//
// Pot L Blend (press: Dry/Wet) · Pot C Scan (press: Reso mix) · Pot R Size (press: Atmosphere)
// Encoder L: choose Trail, click: Solo · Encoder R: Trail level, click: Lock
// Button 3: Rec · Button 4: Hold (infinite on/off)
//
// nt_emu (NT_EMU_WIN) hands a custom-UI plug-in only pot turns, encoder turns
// and buttons 1–4 — pot and encoder presses never arrive. There buttons 1 and
// 2 (which stay with the NT on the hardware) switch the pot targets instead:
// Button 1 = Pot L Blend ↔ Dry/Wet, Button 2 = Pots C and R together
// (Scan/Size ↔ Reso mix/Atmosphere).
//
// nt_emu also reports a pot "press" whenever a pot starts being dragged, so a
// turn would flip the target straight back. The nt_emu build therefore
// ignores pot presses completely and switches only with buttons 1/2.
#ifdef NT_EMU_WIN
constexpr uint32_t kPotLSwitch = kNT_button1;
constexpr uint32_t kPotCSwitch = 0;
constexpr uint32_t kPotRSwitch = kNT_button2;
#else
constexpr uint32_t kPotLSwitch = kNT_potButtonL;
constexpr uint32_t kPotCSwitch = kNT_potButtonC;
constexpr uint32_t kPotRSwitch = kNT_potButtonR;
#endif

constexpr uint32_t kCustomControls = kNT_potL | kNT_potC | kNT_potR | kPotLSwitch | kPotCSwitch | kPotRSwitch
                                     | kNT_encoderL | kNT_encoderR | kNT_encoderButtonL
                                     | kNT_encoderButtonR | kNT_button3 | kNT_button4;

uint32_t hasCustomUi(_NT_algorithm* self)
{
    if(!self->v)
        return 0; // nt_emu asks before it has connected the parameters
    return kCustomControls;
}

bool ClickEdge(Click& c, const _NT_uiData& d, uint32_t bit, bool turn)
{
    const bool cb = d.controls & bit;
    const bool lb = d.lastButtons & bit;
    if(cb && !lb)
    {
        c.down   = true;
        c.turned = false;
        return false;
    }
    if(!c.down)
        return false;
    if(turn)
        c.turned = true;
    if(!lb)
        return false;
    c.down = false;
    return !c.turned;
}

// A press of any control in `bits`: changed now and not down before. In nt_emu
// a button reports its press with the bit already in lastButtons and its
// release without it, so there this fires once, on release.
inline bool Pressed(const _NT_uiData& d, uint32_t bits)
{
    return (d.controls & bits & ~d.lastButtons) != 0;
}

int Count(const PerseidsAlgorithm* a) { return ParamValue(a, kParamCount); }

// Current target of each pot and its value mapped to the pot's 0…1 travel.
int PotParam(const PerseidsAlgorithm* a, int k)
{
    if(k == 0)
        return a->pot_l_mix ? kParamDryWet : kParamBlend;
    if(k == 1)
        return a->pot_c_reso ? kParamResoMix : kParamScan;
    return a->pot_r_atmo ? kParamAtmosphere : kParamSize;
}

float PotNorm(const PerseidsAlgorithm* a, int k)
{
    const int p = PotParam(a, k);
    const int v = ParamValue(a, p);
    if(p == kParamAtmosphere)
        return (v + 100) * 0.005f;
    if(p == kParamSize)
        return (v - 4) * 0.05f;
    return v * 0.01f;
}

int PotValue(int p, float pos)
{
    if(p == kParamAtmosphere)
        return static_cast<int>(pos * 200.f + 0.5f) - 100;
    if(p == kParamSize)
        return 4 + static_cast<int>(pos * 20.f + 0.5f);
    return static_cast<int>(pos * 100.f + 0.5f);
}

// True while the pot has not yet picked up its current target.
bool PotWaiting(const PerseidsAlgorithm* a, int k)
{
    const int p = PotParam(a, k);
    return !(a->pot_caught[k] && a->pot_param[k] == p);
}

constexpr float kCatchWindow = 0.02f; // ±2 % of the travel counts as "there"

void PotMoved(PerseidsAlgorithm* a, int k, float pos)
{
    const int p = PotParam(a, k);
    if(PotWaiting(a, k))
    {
        // New target, or the value was changed elsewhere: wait for the pot.
        a->pot_caught[k] = false;
        a->pot_param[k]  = p;
        const float target = PotNorm(a, k);
        const float last   = a->pot_last[k];
        const bool  near   = std::fabs(pos - target) <= kCatchWindow;
        const bool  passed = last >= 0.f && (last - target) * (pos - target) <= 0.f;
        a->pot_last[k]     = pos;
        if(!near && !passed)
            return;
        a->pot_caught[k] = true;
    }
    a->pot_last[k] = pos;
    int v          = PotValue(p, pos);
    v              = v < a->params[p].min ? a->params[p].min : (v > a->params[p].max ? a->params[p].max : v);
    a->pot_written[k] = static_cast<int16_t>(v); // before: nt_emu calls parameterChanged at once
    SetParamUi(a, p, v);
}

void customUi(_NT_algorithm* self, const _NT_uiData& data)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    if(!a->v)
        return;

    if(data.controls & kNT_potL)
        PotMoved(a, 0, data.pots[0]);
    if(Pressed(data, kPotLSwitch))
    {
        a->pot_l_mix = !a->pot_l_mix;
        NT_requestSetupUi(); // re-sync soft takeover for the new target
    }
    if(data.controls & kNT_potC)
        PotMoved(a, 1, data.pots[1]);
    if(kPotCSwitch && Pressed(data, kPotCSwitch))
    {
        a->pot_c_reso = !a->pot_c_reso;
        NT_requestSetupUi();
    }
    if(data.controls & kNT_potR)
        PotMoved(a, 2, data.pots[2]);
    if(Pressed(data, kPotRSwitch))
    {
        a->pot_r_atmo = !a->pot_r_atmo;
#ifdef NT_EMU_WIN
        // nt_emu has no pot presses: button 2 switches Pot C along with Pot R.
        if(Pressed(data, kNT_button2))
            a->pot_c_reso = a->pot_r_atmo;
#endif
        NT_requestSetupUi(); // re-sync soft takeover for the new target
    }

    // Keep the selection inside Count (Count may have been lowered).
    if(a->selected > Count(a) - 1)
        a->selected = Count(a) - 1;

    const bool turned = data.encoders[0] || data.encoders[1];
    if(data.encoders[0])
    {
        int s = a->selected + data.encoders[0];
        const int n = Count(a);
        if(s < 0)
            s = 0;
        if(s > n - 1)
            s = n - 1;
        a->selected = s;
    }
    if(data.encoders[1])
    {
        const int p = kParamTrailLevel1 + a->selected;
        SetParamUi(a, p, ParamValue(a, p) + 2 * data.encoders[1]);
    }
    if(ClickEdge(a->click_l, data, kNT_encoderButtonL, turned))
    {
        const int p = kParamSolo1 + a->selected;
        SetParamUi(a, p, ParamValue(a, p) ? 0 : 1);
    }
    if(ClickEdge(a->click_r, data, kNT_encoderButtonR, turned))
    {
        const int p = kParamLock1 + a->selected;
        SetParamUi(a, p, ParamValue(a, p) ? 0 : 1);
    }

    if(Pressed(data, kNT_button3))
        a->eng->capture.RequestManualTrigger();
    if(Pressed(data, kNT_button4))
    {
        if(ParamValue(a, kParamHold) == kHoldInf)
            SetParamUi(a, kParamHold, a->hold_before >= kHoldInf ? 15 : a->hold_before);
        else
            SetParamUi(a, kParamHold, kHoldInf);
    }
}

void setupUi(_NT_algorithm* self, _NT_float3& pots)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    if(!a->v)
        return;
    for(int k = 0; k < 3; ++k)
        pots[k] = PotNorm(a, k);
}

int parameterString(_NT_algorithm* /*self*/, int p, int v, char* buff)
{
    if(p != kParamHold)
        return 0;
    if(v >= kHoldInf)
    {
        std::strcpy(buff, "INF");
        return 3;
    }
    int n = NT_intToString(buff, v);
    std::strcpy(buff + n, " s");
    return n + 2;
}

// ---------------------------------------------------------------------------
// Display (256 × 64, 4 bit)

void DrawBar(int x0, int y0, int w, int h, float fill, int colour)
{
    NT_drawShapeI(kNT_box, x0, y0, x0 + w - 1, y0 + h - 1, 3);
    const int fw = static_cast<int>(Clampf(fill, 0.f, 1.f) * (w - 2) + 0.5f);
    if(fw > 0)
        NT_drawShapeI(kNT_rectangle, x0 + 1, y0 + 1, x0 + fw, y0 + h - 2, colour);
}

void FormatPercent(char* buff, int v, bool sign)
{
    int n = 0;
    if(sign && v > 0)
        buff[n++] = '+';
    n += NT_intToString(buff + n, v);
    buff[n++] = '%';
    buff[n]   = 0;
}

// --- Recording embers (port of the firmware's DrawRecSparkleFill) ----------
//
// The NT screen has 16 grey levels. Each ember is drawn like on the hardware
// as one full-brightness pixel; on the NT it also leaves a faint pixel behind
// it in its direction of travel (level kEmberTail). Where the grey levels are
// hard to see, it simply looks like the hardware.

constexpr uint32_t kRecSoftMs  = 200; // ember appear / burn-out
constexpr int      kEmbers     = 13;  // firmware: 18 on a 58 px bar → same density
constexpr int      kEmberHead  = 15;
constexpr int      kEmberTail  = 5;

uint32_t SparkleHash(int x, int y, uint32_t seed)
{
    uint32_t n = static_cast<uint32_t>(x) * 374761393u
                 ^ static_cast<uint32_t>(y) * 668265263u ^ seed;
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

inline void Pixel(int x, int y, int colour)
{
    NT_drawShapeI(kNT_point, x, y, x, y, colour);
}

void DrawRecSparkleFill(int      x0,
                        int      y,
                        int      w,
                        int      h,
                        float    grow,
                        uint32_t seed,
                        bool     left_to_right,
                        float    visibility,
                        uint32_t now_ms)
{
    if(grow <= 0.f || visibility <= 0.f)
        return;
    grow       = grow > 1.f ? 1.f : grow;
    visibility = visibility > 1.f ? 1.f : visibility;

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

    const int      y_span  = y_hi - y_lo + 1;
    const float    t_sec   = static_cast<float>(now_ms) * 0.001f;
    const uint32_t vis_thr = static_cast<uint32_t>(visibility * 255.f);
    const float    edge_x  = static_cast<float>(x_lo) + grow * inner;

    for(int i = 0; i < kEmbers; ++i)
    {
        if((SparkleHash(i, 3, seed) & 255u) >= vis_thr)
            continue;

        const uint32_t hh  = SparkleHash(i, 7, seed);
        const float    spd = 0.45f + static_cast<float>((hh >> 1) & 255u) * (0.70f / 255.f);
        const float    ph0 = static_cast<float>((hh >> 9) & 255u) * (1.f / 255.f);
        float          u   = ph0 + t_sec * spd;
        u -= std::floor(u);

        int x;
        int dir; // travel direction, for the tail
        if(left_to_right)
        {
            const float x_f = static_cast<float>(x_lo) + u * inner;
            if(x_f > edge_x + 0.5f)
                continue;
            x = static_cast<int>(x_f + 0.5f);
            // Densest at mid-bar, thinner toward the ends.
            const float xn  = (x_f - static_cast<float>(x_lo)) / (inner + 0.01f);
            const float mid = 1.f - 2.f * std::fabs(xn - 0.5f);
            if(mid < 0.f)
                continue;
            const float dens = 0.65f + 0.35f * mid;
            if((SparkleHash(i, 11, seed) & 255u) >= static_cast<uint32_t>(dens * 255.f))
                continue;
            dir = 1;
        }
        else
        {
            const float side = (hh & 1u) ? 1.f : -1.f;
            x                = static_cast<int>(cx + side * u * radius + 0.5f);
            dir              = side > 0.f ? 1 : -1;
        }
        if(x < x_lo || x > x_hi)
            continue;

        const int py = y_lo + static_cast<int>((hh >> 17) % static_cast<uint32_t>(y_span));
        const int tx = x - dir;
        if(tx >= x_lo && tx <= x_hi)
            Pixel(tx, py, kEmberTail);
        Pixel(x, py, kEmberHead);
    }
}

void DrawRecCenterSolid(int x0, int y, int w, int h, float grow, int colour)
{
    if(grow <= 0.f)
        return;
    grow              = grow > 1.f ? 1.f : grow;
    const int inner_w = w - 2;
    const int span    = static_cast<int>(grow * static_cast<float>(inner_w) + 0.5f);
    if(span <= 0)
        return;
    const int mid   = x0 + 1 + inner_w / 2;
    int       left  = mid - span / 2;
    int       right = left + span - 1;
    left            = left < x0 + 1 ? x0 + 1 : left;
    right           = right > x0 + inner_w ? x0 + inner_w : right;
    NT_drawShapeI(kNT_rectangle, left, y + 1, right, y + h - 2, colour);
}

// Port of DisplayRenderer::DrawTrailLifeBar: embers (PRS/PLR) or solid (CTR)
// while recording, Fade In fills left→right, Hold full, Fade Out empties
// left→right.
void DrawTrailLifeBar(LifeBarAnim&       anim,
                      int                x0,
                      int                y,
                      int                w,
                      int                h,
                      size_t             trail_index,
                      const TrailLifeUi& life,
                      int                rec_style,
                      uint32_t           now,
                      int                colour)
{
    const int x1 = x0 + w - 1;
    const int y1 = y + h - 1;
    NT_drawShapeI(kNT_box, x0, y, x1, y1, 3);

    if(life.phase == TrailLifePhase::Empty)
    {
        anim.last_phase = TrailLifePhase::Empty;
        anim.soft       = RecSoft::Idle;
        anim.grow_latch = 1.f;
        return;
    }

    const float    fill     = Clampf(life.fill, 0.f, 1.f);
    const int      inner_w  = w - 2;
    const int      fill_w   = static_cast<int>(fill * static_cast<float>(inner_w) + 0.5f);
    const uint32_t seed     = 0xA5u + static_cast<uint32_t>(trail_index) * 97u;
    const bool     perseids = rec_style <= 1;
    const bool     ltr      = rec_style == 1;

    if(perseids)
    {
        if(life.phase == TrailLifePhase::Recording
           && anim.last_phase != TrailLifePhase::Recording && anim.soft != RecSoft::FadeOut)
        {
            // Soft appear only for PRS; PLR starts clean left→right.
            if(!ltr)
            {
                anim.soft      = RecSoft::FadeIn;
                anim.soft_t0   = now;
                anim.ltr_latch = false;
            }
        }
        if(anim.last_phase == TrailLifePhase::Recording
           && (life.phase == TrailLifePhase::FadeIn || life.phase == TrailLifePhase::Hold)
           && anim.soft != RecSoft::FadeOut)
        {
            anim.soft      = RecSoft::FadeOut;
            anim.soft_t0   = now;
            anim.ltr_latch = ltr;
            if(anim.grow_latch < 0.15f)
                anim.grow_latch = 1.f;
        }
        if(life.phase == TrailLifePhase::Recording)
        {
            if(fill > 0.05f)
                anim.grow_latch = fill;
            anim.ltr_latch = ltr;
        }

        if(anim.soft != RecSoft::Idle)
        {
            const uint32_t dt = now - anim.soft_t0;
            if(dt >= kRecSoftMs)
            {
                const RecSoft done = anim.soft;
                anim.soft          = RecSoft::Idle;
                if(done != RecSoft::FadeOut && life.phase == TrailLifePhase::Recording)
                {
                    DrawRecSparkleFill(x0, y, w, h, fill, seed, ltr, 1.f, now);
                    anim.last_phase = life.phase;
                    return;
                }
            }
            else
            {
                const float t = static_cast<float>(dt) / static_cast<float>(kRecSoftMs);
                float       vis;
                float       grow_draw;
                bool        use_ltr = anim.ltr_latch;
                if(anim.soft == RecSoft::FadeIn)
                {
                    vis       = t * (2.f - t);
                    grow_draw = fill;
                    use_ltr   = ltr;
                }
                else
                {
                    const float u = 1.f - t;
                    vis           = u * u;
                    grow_draw     = anim.grow_latch;
                }
                DrawRecSparkleFill(x0, y, w, h, grow_draw, seed, use_ltr, vis, now);
                anim.last_phase = life.phase;
                return; // solid Fade In waits until the embers have burnt out
            }
        }
        anim.last_phase = life.phase;
    }
    else
    {
        anim.soft       = RecSoft::Idle;
        anim.last_phase = life.phase;
    }

    if(life.phase == TrailLifePhase::Recording)
    {
        if(rec_style >= 2)
            DrawRecCenterSolid(x0, y, w, h, fill, 15);
        else
            DrawRecSparkleFill(x0, y, w, h, fill, seed, ltr, 1.f, now);
    }
    else if(life.phase == TrailLifePhase::FadeOut)
    {
        if(fill_w > 0)
            NT_drawShapeI(kNT_rectangle, x0 + 1 + (inner_w - fill_w), y + 1, x0 + inner_w, y1 - 1, colour);
    }
    else
    {
        const int fw = life.phase == TrailLifePhase::Hold ? inner_w : fill_w;
        if(fw > 0)
            NT_drawShapeI(kNT_rectangle, x0 + 1, y + 1, x0 + fw, y1 - 1, colour);
    }
}

bool draw(_NT_algorithm* self)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    if(!a->v)
        return false;
    const Engines& e = *a->eng;
    char           buff[24];

    // Header: name, recording state, input meter.
    NT_drawText(0, 8, "PERSEIDS", 15);
    // Plug-in version, so it is obvious which build is loaded.
    NT_drawText(48, 8, kVersion, 10, kNT_textLeft, kNT_textTiny);
    const int count = Count(a);
    if(e.capture.RecActive())
    {
        std::strcpy(buff, "REC ");
        NT_intToString(buff + 4, e.capture.RecTrailSlot());
        NT_drawShapeI(kNT_rectangle, 69, 0, 104, 9, 15); // clear of the version label
        NT_drawText(72, 8, buff, 0);
    }
    else
    {
        std::strcpy(buff, "next ");
        NT_intToString(buff + 5, e.capture.RecTrailSlot());
        NT_drawText(72, 8, buff, 5);
    }
    NT_drawText(108, 7, "IN", 6, kNT_textLeft, kNT_textTiny);
    DrawBar(118, 1, 50, 4, e.capture.InputLevel(), 10);
    DrawBar(118, 5, 50, 4, e.capture.InputLevelR(), 10);
    // Threshold mark on the meters (same 1.5× display boost as the engine).
    const int thr_x = 119 + static_cast<int>(Clampf(a->capture_p.threshold * 1.5f, 0.f, 1.f) * 47.f);
    NT_drawShapeI(kNT_line, thr_x, 0, thr_x, 9, 15);

    // Blend: SP ─●─ SW
    NT_drawText(176, 7, "SP", 8, kNT_textLeft, kNT_textTiny);
    NT_drawShapeI(kNT_line, 187, 4, 239, 4, 4);
    const int bx = 187 + static_cast<int>(Clampf(a->blend, 0.f, 1.f) * 52.f);
    NT_drawShapeI(kNT_rectangle, bx - 1, 2, bx + 1, 6, 15);
    NT_drawText(244, 7, "SW", 8, kNT_textLeft, kNT_textTiny);

    // Trails: one column each.
    TrailLifeUi life[kTrailCount];
    e.capture.GetTrailLifeUi(life);
    const uint32_t now_ms = static_cast<uint32_t>(
        a->sample_clock * 1000u / (NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000u));
    for(int t = 0; t < static_cast<int>(kTrailCount); ++t)
    {
        const int  x0     = 1 + t * 51;
        const bool active = t < count;
        const bool sel    = t == a->selected;
        const int  dim    = active ? 15 : 3;
        if(sel && active)
            NT_drawShapeI(kNT_box, x0, 12, x0 + 48, 50, 8);

        NT_intToString(buff, t + 1);
        NT_drawText(x0 + 3, 22, buff, dim);

        const char* phase = "--";
        int         pc    = active ? 5 : 2;
        switch(life[t].phase)
        {
            case TrailLifePhase::Recording: phase = "REC"; pc = 15; break;
            case TrailLifePhase::FadeIn: phase = "IN"; pc = 12; break;
            case TrailLifePhase::FadeOut: phase = "OUT"; pc = 9; break;
            case TrailLifePhase::Hold:
                if(life[t].hold_sec < 0)
                    phase = "INF";
                else
                {
                    NT_intToString(buff, life[t].hold_sec);
                    std::strcat(buff, "s");
                    phase = buff;
                }
                pc = 12;
                break;
            case TrailLifePhase::Empty: break;
        }
        NT_drawText(x0 + 46, 22, phase, active ? pc : 2, kNT_textRight);

        // Life bar: recording embers, Fade In, Hold, Fade Out (7 px like the
        // hardware, so the embers have five rows to travel in).
        DrawTrailLifeBar(a->life_anim[t], x0 + 3, 25, 43, 7, static_cast<size_t>(t),
                         life[t], ParamValue(a, kParamRecStyle), now_ms, active ? 8 : 3);

        // Level bar.
        const int level = ParamValue(a, kParamTrailLevel1 + t);
        DrawBar(x0 + 3, 35, 43, 4, level * 0.01f, active ? 12 : 3);

        // Lock / Solo.
        const bool locked = ParamValue(a, kParamLock1 + t) != 0;
        const bool solo   = ParamValue(a, kParamSolo1 + t) != 0;
        NT_drawText(x0 + 4, 48, "L", locked ? 15 : 2, kNT_textLeft, kNT_textTiny);
        NT_drawText(x0 + 12, 48, "S", solo ? 15 : 2, kNT_textLeft, kNT_textTiny);
        NT_intToString(buff, level);
        std::strcat(buff, "%");
        NT_drawText(x0 + 46, 48, buff, active ? 8 : 2, kNT_textRight, kNT_textTiny);
    }

    // Footer: what the pots do, and the current values. In nt_emu, where pot
    // presses never arrive, each label carries the button that switches it.
#ifdef NT_EMU_WIN
    const char* const lbl_l = a->pot_l_mix ? "1 DRY/WET" : "1 BLEND";
    const char* const lbl_c = a->pot_c_reso ? "2 RESO" : "2 SCAN";
    const char* const lbl_r = a->pot_r_atmo ? "2 ATMO" : "2 SIZE";
#else
    const char* const lbl_l = a->pot_l_mix ? "DRY/WET" : "BLEND";
    const char* const lbl_c = a->pot_c_reso ? "RESO" : "SCAN";
    const char* const lbl_r = a->pot_r_atmo ? "ATMO" : "SIZE";
#endif
    // Tiny font: 4 px per character; the value follows one blank later.
    auto ValueX = [](int x, const char* label) {
        return x + 4 * static_cast<int>(std::strlen(label)) + 4;
    };

    FormatPercent(buff, ParamValue(a, a->pot_l_mix ? kParamDryWet : kParamBlend), false);
    NT_drawText(2, 61, lbl_l, 6, kNT_textLeft, kNT_textTiny);
    // Value dim while the pot still has to catch up with it.
    auto ValColour = [a](int k) { return PotWaiting(a, k) && a->pot_last[k] >= 0.f ? 5 : 15; };
    NT_drawText(ValueX(2, lbl_l), 61, buff, ValColour(0), kNT_textLeft, kNT_textTiny);

    FormatPercent(buff, ParamValue(a, a->pot_c_reso ? kParamResoMix : kParamScan), false);
    NT_drawText(100, 61, lbl_c, 6, kNT_textLeft, kNT_textTiny);
    NT_drawText(ValueX(100, lbl_c), 61, buff, ValColour(1), kNT_textLeft, kNT_textTiny);

    if(a->pot_r_atmo)
        FormatPercent(buff, ParamValue(a, kParamAtmosphere), true);
    else
        NT_intToString(buff, ParamValue(a, kParamSize));
    NT_drawText(196, 61, lbl_r, 6, kNT_textLeft, kNT_textTiny);
    NT_drawText(254, 61, buff, ValColour(2), kNT_textRight, kNT_textTiny);

    if(ParamValue(a, kParamHold) == kHoldInf)
        NT_drawText(160, 61, "HOLD", 15, kNT_textLeft, kNT_textTiny);
    if(e.swarm.GovernorActive())
        NT_drawText(60, 61, "CPU", 15, kNT_textLeft, kNT_textTiny);
    if(a->mod_active > 0)
    {
        std::strcpy(buff, "MOD");
        NT_intToString(buff + 3, a->mod_active);
        NT_drawText(76, 61, buff, 12, kNT_textLeft, kNT_textTiny);
    }
    return true;
}

// ---------------------------------------------------------------------------

const _NT_factory kFactory = {
    .guid                        = NT_MULTICHAR('X', 'o', 'P', 's'),
    .name                        = "Perseids",
    .description                 = "Ambient resynthesizer: 5 Trails, Spectra + Swarm (v" PERSEIDS_NT_VERSION ")",
    .numSpecifications           = kNumSpecs,
    .specifications              = kSpecs,
    .calculateStaticRequirements = nullptr,
    .initialise                  = nullptr,
    .calculateRequirements       = calculateRequirements,
    .construct                   = construct,
    .parameterChanged            = parameterChanged,
    .step                        = step,
    .draw                        = draw,
    .midiRealtime                = nullptr,
    .midiMessage                 = nullptr,
    .tags                        = kNT_tagEffect,
    .hasCustomUi                 = hasCustomUi,
    .customUi                    = customUi,
    .setupUi                     = setupUi,
    .serialise                   = nullptr,
    .deserialise                 = nullptr,
    .midiSysEx                   = nullptr,
    .parameterUiPrefix           = nullptr,
    .parameterString             = parameterString,
    .stringParameterBuffer       = nullptr,
};

} // namespace

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data)
{
    switch(selector)
    {
        case kNT_selector_version: return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo:
            return reinterpret_cast<uintptr_t>(data == 0 ? &kFactory : nullptr);
    }
    return 0;
}
