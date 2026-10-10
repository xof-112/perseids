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
    kParamMatchSpeed,
    kParamClockIn,
    kParamModSync1, // one per mod slot: internal LFO free or locked to Clock in
    kParamModSync4 = kParamModSync1 + 3,
    // Mod slots 5–12, added later: six parameters each (the five + sync).
    kParamMod5,
    kParamModReset = kParamMod5 + 8 * 6, // Mod overview: all slots back to off
    kParamModMode1,                      // per slot: around the base value or override it
    kParamModView = kParamModMode1 + 12, // Display: Mod view as graphics or numbers
    kParamModShape1,                     // per slot: internal LFO waveform
    kParamModLine1 = kParamModShape1 + 12, // per slot: the Mod overview line (mirrors the amount)

    kNumParams = kParamModLine1 + 12,
};

// Mod slot layout: slots 1–4 at kParamMod1 + slot × 5 (+ sync at
// kParamModSync1 + slot), slots 5–12 at kParamMod5 + (slot − 4) × 6.
// Use ModParam() / ModSyncParam(), never the raw layout.
constexpr int kNumModSlots = 12;
enum
{
    kModIn,     // CV input bus; none = internal LFO (jack normalling, 4.10 "OFF")
    kModDest,   // destination, index into kModTargets (0 = off)
    kModAmount, // attenuverter −100…+100 %
    kModOffset, // bias after the attenuverter −100…+100 %
    kModRate,   // internal LFO rate, 0.01–20 Hz
    kModParams,
};
static_assert(kParamDryWet == kParamMod1 + 4 * kModParams, "mod slot layout");
constexpr int ModParam(int m, int field)
{
    return m < 4 ? kParamMod1 + m * kModParams + field : kParamMod5 + (m - 4) * (kModParams + 1) + field;
}
constexpr int ModSyncParam(int m)
{
    return m < 4 ? kParamModSync1 + m : kParamMod5 + (m - 4) * (kModParams + 1) + kModParams;
}
static_assert(ModSyncParam(kNumModSlots - 1) == kParamModReset - 1, "mod slot layout");
constexpr int ModModeParam(int m) { return kParamModMode1 + m; }
constexpr int ModShapeParam(int m) { return kParamModShape1 + m; }
constexpr int ModLineParam(int m) { return kParamModLine1 + m; }

// Switch-like slot settings (input, destination, mode, sync) take effect only
// once they have stood still for this long, or as soon as the plug-in's own
// screen is used again: scrolling through the destinations in the menu does
// not modulate every parameter on the way. Amount, offset and rate act at once.
constexpr float kModArmS = 1.5f;
enum { kArmIn, kArmDest, kArmMode, kArmSync, kArmFields };
constexpr int ArmParam(int m, int f)
{
    return f == kArmIn ? ModParam(m, kModIn)
           : f == kArmDest ? ModParam(m, kModDest)
           : f == kArmMode ? ModModeParam(m)
                           : ModSyncParam(m);
}

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
    // Mod the mod: amount, offset and LFO rate of every slot (applied one
    // chunk later, ~1 ms, so a slot may even modulate itself).
    ModParam(0, kModAmount), ModParam(0, kModOffset), ModParam(0, kModRate),
    ModParam(1, kModAmount), ModParam(1, kModOffset), ModParam(1, kModRate),
    ModParam(2, kModAmount), ModParam(2, kModOffset), ModParam(2, kModRate),
    ModParam(3, kModAmount), ModParam(3, kModOffset), ModParam(3, kModRate),
    ModParam(4, kModAmount), ModParam(4, kModOffset), ModParam(4, kModRate),
    ModParam(5, kModAmount), ModParam(5, kModOffset), ModParam(5, kModRate),
    ModParam(6, kModAmount), ModParam(6, kModOffset), ModParam(6, kModRate),
    ModParam(7, kModAmount), ModParam(7, kModOffset), ModParam(7, kModRate),
    ModParam(8, kModAmount), ModParam(8, kModOffset), ModParam(8, kModRate),
    ModParam(9, kModAmount), ModParam(9, kModOffset), ModParam(9, kModRate),
    ModParam(10, kModAmount), ModParam(10, kModOffset), ModParam(10, kModRate),
    ModParam(11, kModAmount), ModParam(11, kModOffset), ModParam(11, kModRate),
};
const char* const kModTargetNames[] = {
    "None", // first entry: turn the destination fully left to switch a slot off
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
    "Mod 1 amount", "Mod 1 offset", "Mod 1 LFO rate",
    "Mod 2 amount", "Mod 2 offset", "Mod 2 LFO rate",
    "Mod 3 amount", "Mod 3 offset", "Mod 3 LFO rate",
    "Mod 4 amount", "Mod 4 offset", "Mod 4 LFO rate",
    "Mod 5 amount", "Mod 5 offset", "Mod 5 LFO rate",
    "Mod 6 amount", "Mod 6 offset", "Mod 6 LFO rate",
    "Mod 7 amount", "Mod 7 offset", "Mod 7 LFO rate",
    "Mod 8 amount", "Mod 8 offset", "Mod 8 LFO rate",
    "Mod 9 amount", "Mod 9 offset", "Mod 9 LFO rate",
    "Mod 10 amount", "Mod 10 offset", "Mod 10 LFO rate",
    "Mod 11 amount", "Mod 11 offset", "Mod 11 LFO rate",
    "Mod 12 amount", "Mod 12 offset", "Mod 12 LFO rate",
    nullptr,
};
constexpr int kNumModTargets = static_cast<int>(sizeof(kModTargets));
// The same names with " ~": shown in the parameter menus while a target is
// modulated (index = target index, 0 = Off has no marked form).
const char* const kModTargetMarked[] = {
    nullptr, "Count ~", "Threshold ~", "Cont. Rec ~", "Overwrite ~", "Capture ~", "Play ~", "Buffer ~", "Hold ~", "Fade in ~", "Fade out ~", "Blend ~", "Pitch Spectra ~", "Pitch Swarm ~", "Output level ~", "Partials ~", "Waveshape ~", "Umbra/Aurora ~", "Ensemble ~", "Size ~", "Spread ~", "Scan ~", "Scatter ~", "Atmosphere ~", "Direction ~", "Level 1 ~", "Level 2 ~", "Level 3 ~", "Level 4 ~", "Level 5 ~", "Lock 1 ~", "Lock 2 ~", "Lock 3 ~", "Lock 4 ~", "Lock 5 ~", "Solo 1 ~", "Solo 2 ~", "Solo 3 ~", "Solo 4 ~", "Solo 5 ~", "Dry/Wet ~", "Reso mix ~", "Reso decay ~", "Reso damping ~", "Reso spread ~", "Reso pitch ~", "Reso quantize ~", "Reso scale ~", "Reso tuning ~", "Mod 1 amount ~", "Mod 1 offset ~", "Mod 1 LFO rate ~", "Mod 2 amount ~", "Mod 2 offset ~", "Mod 2 LFO rate ~", "Mod 3 amount ~", "Mod 3 offset ~", "Mod 3 LFO rate ~", "Mod 4 amount ~", "Mod 4 offset ~", "Mod 4 LFO rate ~", "Mod 5 amount ~", "Mod 5 offset ~", "Mod 5 LFO rate ~", "Mod 6 amount ~", "Mod 6 offset ~", "Mod 6 LFO rate ~", "Mod 7 amount ~", "Mod 7 offset ~", "Mod 7 LFO rate ~", "Mod 8 amount ~", "Mod 8 offset ~", "Mod 8 LFO rate ~", "Mod 9 amount ~", "Mod 9 offset ~", "Mod 9 LFO rate ~", "Mod 10 amount ~", "Mod 10 offset ~", "Mod 10 LFO rate ~", "Mod 11 amount ~", "Mod 11 offset ~", "Mod 11 LFO rate ~", "Mod 12 amount ~", "Mod 12 offset ~", "Mod 12 LFO rate ~"};
static_assert(sizeof(kModTargetMarked) / sizeof(kModTargetMarked[0]) == sizeof(kModTargets),
              "every mod target needs a marked name");
const char* const kModModes[] = {"Around base", "Override", nullptr};
const char* const kModViews[] = {"Graphic", "Numbers", nullptr};
// Internal LFO waveforms. Classic = the module's triangle/sine blend (4.10).
enum
{
    kShapeClassic,
    kShapeSine,
    kShapeTriangle,
    kShapeSawUp,
    kShapeSawDown,
    kShapeSquare,
    kShapeShark,
    kShapeSharkRev,
    kShapeExp,
    kShapeLog,
    kShapeRandom,
    kNumShapes
};
const char* const kModShapes[] = {"Classic", "Sine",    "Triangle", "Saw up", "Saw down", "Square",
                                  "Shark",   "Shark rev", "Exp",    "Log",    "Random steps", nullptr};
static_assert(sizeof(kModTargetNames) / sizeof(kModTargetNames[0]) == sizeof(kModTargets) + 1,
              "every mod target needs a name");

// Plug-in version, shown in the display header and the algorithm description.
// History in README.md (Versionen).
#define PERSEIDS_NT_VERSION "0.29"
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
// The measurement is the same at every speed (otherwise the balance itself
// would shift with the speed); Match speed Slow / Medium / Fast only sets
// how fast the gain follows. The first seconds with signal (after loading
// or Clear trails) always run at Fast, so the match locks in at once.
constexpr float kLevelMatchShortS   = 0.05f;
constexpr float kLevelMatchAtkS     = 0.15f;
constexpr float kLevelMatchRelS     = 1.2f;
constexpr float kLevelMatchGainS[3] = {4.f, 1.5f, 0.4f};
constexpr float kLevelMatchLockS    = 3.f; // first lock-in at Fast (≈ a Fade in)
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
const char* const kMatchSpeeds[] = {"Slow", "Medium", "Fast", nullptr};
// Mod LFO sync to Clock in: LFO cycles per clock pulse (÷ = one cycle over N
// pulses, × = N cycles per pulse). Free = the slot's own LFO rate.
// "Rst …" = the slot's own LFO rate, but restarted on that grid (each pulse,
// every Nth pulse, or N times per pulse from the measured period).
const char* const kModSyncNames[] = {"Free",    "/16",     "/8",     "/4",     "/3",     "/2",
                                     "x1",      "x2",      "x3",     "x4",     "x8",     "Rst /16",
                                     "Rst /8",  "Rst /4",  "Rst /3", "Rst /2", "Rst x1", "Rst x2",
                                     "Rst x3",  "Rst x4",  "Rst x8", nullptr};
constexpr int kModSyncLocked = 10; // 1…10 locked to the clock, 11…20 free + reset
constexpr int kModSyncDiv[] = {0, 16, 8, 4, 3, 2, 1, 1, 1, 1, 1}; // pulses per reset
constexpr int kModSyncMul[] = {0, 1, 1, 1, 1, 1, 1, 2, 3, 4, 8};  // cycles per reset

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
    P_NUM("Mod " n " amount", -100, 100, 0, kNT_unitHasStrings, 0)     \
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
    P_ENUM("Match speed", 2, 1, kMatchSpeeds)

    // Clock for the mod LFOs (ARCHITECTURE 4.3 Divider / 4.10 OFF: rate =
    // clock period × divider). Rising edge above 1 V. No pulse for 4 periods
    // (or 3 s) = clock lost: synced slots fall back to their free rate.
    NT_PARAMETER_CV_INPUT("Clock in", 0, 3) // In 3 ab Werk
    P_ENUM("Mod 1 sync", 20, 0, kModSyncNames)
    P_ENUM("Mod 2 sync", 20, 0, kModSyncNames)
    P_ENUM("Mod 3 sync", 20, 0, kModSyncNames)
    P_ENUM("Mod 4 sync", 20, 0, kModSyncNames)

    // Mod slots 5–12: same as 1–4, sync included.
#define P_MOD_SLOT_SYNC(n) P_MOD_SLOT(n) P_ENUM("Mod " n " sync", 20, 0, kModSyncNames)
    P_MOD_SLOT_SYNC("5") P_MOD_SLOT_SYNC("6") P_MOD_SLOT_SYNC("7") P_MOD_SLOT_SYNC("8")
    P_MOD_SLOT_SYNC("9") P_MOD_SLOT_SYNC("10") P_MOD_SLOT_SYNC("11") P_MOD_SLOT_SYNC("12")

    // Mod overview: one confirm resets every slot to its defaults (input
    // None, dest None, amount and offset 0, LFO rate 0.25 Hz, sync Free,
    // mode Around base).
    {.name = "Reset all mods", .min = 0, .max = 1, .def = 0, .unit = kNT_unitConfirm, .scaling = 0, .enumStrings = nullptr},
    // Around base: the stored value is the centre, the slot swings around it
    // (dest = base + contrib × travel). Override: the slot ignores the stored
    // value and sweeps from the middle of the range (Amount 100 % = full range).
    P_ENUM("Mod 1 mode", 1, 0, kModModes) P_ENUM("Mod 2 mode", 1, 0, kModModes)
    P_ENUM("Mod 3 mode", 1, 0, kModModes) P_ENUM("Mod 4 mode", 1, 0, kModModes)
    P_ENUM("Mod 5 mode", 1, 0, kModModes) P_ENUM("Mod 6 mode", 1, 0, kModModes)
    P_ENUM("Mod 7 mode", 1, 0, kModModes) P_ENUM("Mod 8 mode", 1, 0, kModModes)
    P_ENUM("Mod 9 mode", 1, 0, kModModes) P_ENUM("Mod 10 mode", 1, 0, kModModes)
    P_ENUM("Mod 11 mode", 1, 0, kModModes) P_ENUM("Mod 12 mode", 1, 0, kModModes)

    // Mod view (Encoder L past the last Trail): tiles with a small scope
    // trace of each destination, or the same as numbers.
    P_ENUM("Mod view", 1, 0, kModViews)

    // Waveform of each slot's internal LFO.
    P_ENUM("Mod 1 shape", kNumShapes - 1, 0, kModShapes) P_ENUM("Mod 2 shape", kNumShapes - 1, 0, kModShapes)
    P_ENUM("Mod 3 shape", kNumShapes - 1, 0, kModShapes) P_ENUM("Mod 4 shape", kNumShapes - 1, 0, kModShapes)
    P_ENUM("Mod 5 shape", kNumShapes - 1, 0, kModShapes) P_ENUM("Mod 6 shape", kNumShapes - 1, 0, kModShapes)
    P_ENUM("Mod 7 shape", kNumShapes - 1, 0, kModShapes) P_ENUM("Mod 8 shape", kNumShapes - 1, 0, kModShapes)
    P_ENUM("Mod 9 shape", kNumShapes - 1, 0, kModShapes) P_ENUM("Mod 10 shape", kNumShapes - 1, 0, kModShapes)
    P_ENUM("Mod 11 shape", kNumShapes - 1, 0, kModShapes) P_ENUM("Mod 12 shape", kNumShapes - 1, 0, kModShapes)

    // Mod overview lines: "Mod n   Blend 40 % > +12 %". Each mirrors its
    // slot's amount both ways (edit here = edit the amount), see SyncModLines.
#define P_MOD_LINE(n) P_NUM("Mod " n, -100, 100, 0, kNT_unitHasStrings, 0)
    P_MOD_LINE("1") P_MOD_LINE("2") P_MOD_LINE("3") P_MOD_LINE("4") P_MOD_LINE("5") P_MOD_LINE("6")
    P_MOD_LINE("7") P_MOD_LINE("8") P_MOD_LINE("9") P_MOD_LINE("10") P_MOD_LINE("11") P_MOD_LINE("12")
};

const uint8_t kPageTrails[]  = {kParamCount, kParamThreshold, kParamContRec,
                                kParamOverwrite, kParamCapture, kParamPlay,
                                kParamClear};
const uint8_t kPageTime[]    = {kParamBuffer, kParamHold, kParamFadeIn, kParamFadeOut};
const uint8_t kPageEngines[] = {kParamBlend, kParamDryWet, kParamLevelMatch, kParamMatchSpeed, kParamPitchSpectra,
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
const uint8_t kPageDisplay[] = {kParamRecStyle, kParamModView};
const uint8_t kPageReso[]    = {kParamResoMix,   kParamResoDecay, kParamResoDamping,
                                kParamResoSpread, kParamResoPitch, kParamResoQuant,
                                kParamResoScale,  kParamResoTuning, kParamResoVoct};
#define MOD_PAGE(s) {ModParam(s, 0), ModParam(s, 1), ModParam(s, 2), ModParam(s, 3), \
                     ModModeParam(s), ModShapeParam(s), ModParam(s, 4), ModSyncParam(s)}
constexpr int kModPageParams = kModParams + 3;
// Overview: built per instance (BuildModOverview) — the reset, then the
// destination and amount of every slot in use (named "Mod n dest / amount").
// New slots are set up, and everything else changed, on the slot's own page.
constexpr int kModOverviewMax = 1 + kNumModSlots;
const uint8_t kPageModOverview[] = {kParamModReset, ModParam(0, kModDest)}; // initial
const uint8_t kPageMod[kNumModSlots][kModPageParams] = {
    MOD_PAGE(0), MOD_PAGE(1), MOD_PAGE(2), MOD_PAGE(3), MOD_PAGE(4),  MOD_PAGE(5),
    MOD_PAGE(6), MOD_PAGE(7), MOD_PAGE(8), MOD_PAGE(9), MOD_PAGE(10), MOD_PAGE(11)};
const uint8_t kPageRouting[] = {kParamInL, kParamInR, kParamRecIn, kParamClockIn, kParamOutL,
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
    {.name = "Display", .numParams = ARRAY_SIZE(kPageDisplay), .group = 8, .unused = {0, 0}, .params = kPageDisplay},
    {.name = "Routing", .numParams = ARRAY_SIZE(kPageRouting), .group = 7, .unused = {0, 0}, .params = kPageRouting},
    // Mod slots at the end, the overview first. Same group for the slots: the
    // cursor keeps its row from slot to slot.
    {.name = "Mod overview", .numParams = ARRAY_SIZE(kPageModOverview), .group = 11, .unused = {0, 0}, .params = kPageModOverview},
    {.name = "Mod 1", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[0]},
    {.name = "Mod 2", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[1]},
    {.name = "Mod 3", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[2]},
    {.name = "Mod 4", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[3]},
    {.name = "Mod 5", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[4]},
    {.name = "Mod 6", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[5]},
    {.name = "Mod 7", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[6]},
    {.name = "Mod 8", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[7]},
    {.name = "Mod 9", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[8]},
    {.name = "Mod 10", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[9]},
    {.name = "Mod 11", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[10]},
    {.name = "Mod 12", .numParams = kModPageParams, .group = 9, .unused = {0, 0}, .params = kPageMod[11]},
};

constexpr int kNumPages        = static_cast<int>(ARRAY_SIZE(kPages));
constexpr int kModOverviewPage = 9;
// (kPages[kModOverviewPage] is "Mod overview"; checked in the tests.)

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

    // Mod view: recent destination values per slot (0…255 of the travel),
    // written by draw() about 25 times a second, ~2 s shown.
    static constexpr int kModHist = 48;
    uint8_t  mod_hist[kNumModSlots][kModHist];
    uint8_t  mod_hist_pos;
    uint32_t mod_hist_ms;
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
    int      match_speed;   // 0 Slow, 1 Medium, 2 Fast
    float    lm_sp_lock;    // seconds of signal learned so far (lock-in)
    float    lm_sw_lock;
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
    bool  mod_view;    // Encoder L turned past the last Trail: the Mod view
    int   mod_sel;     // selected tile there (index among the active slots)
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
    // Per-instance parameter pages: the Mod overview changes with the slots.
    _NT_parameterPage  pages[kNumPages];
    _NT_parameterPages pages_desc;
    uint8_t            overview[kModOverviewMax];
    uint8_t            overview_count;
    bool               pages_mod_first; // Mod view open: Mod overview is the first page

    float   mod_sum[kNumParams];
    uint8_t mod_over[kNumParams];  // an Override slot aims at this parameter
    uint8_t mod_marked[kNumParams]; // name shown with " ~" (display thread)
    // "Mod n <destination>": name of each slot's amount while it has a
    // destination (one line in the Mod overview: name, destination, amount).
    char    amount_name[kNumModSlots][28];
    int16_t amount_name_dest[kNumModSlots]; // destination the name was made for
    float lfo_phase[kNumModSlots];
    int      lfo_sub[kNumModSlots]; // "Rst xN": last sub-pulse a reset fired on
    float    lfo_last[kNumModSlots]; // phase of the last chunk (cycle start = wrap)
    float    lfo_rand[kNumModSlots]; // Random steps: value of this cycle
    uint32_t lfo_rng;

    // Armed (in effect) input / dest / mode / sync per slot, see kModArmS.
    int16_t  mod_armed[kNumModSlots][kArmFields];
    int16_t  mod_seen[kNumModSlots][kArmFields]; // last values seen in v[] (change detection)
    int16_t  line_seen[kNumModSlots];   // overview line / amount as last synced
    int16_t  amount_seen[kNumModSlots];
    uint64_t mod_pending_since[kNumModSlots]; // 0 = nothing pending
    volatile bool mod_arm_now;                // own screen used: arm at once
    volatile bool mod_reset_ui;               // after Reset all mods

    // Clock in: edge detector and period (samples), for synced mod LFOs.
    bool     clock_gate;
    bool     clock_valid;
    bool     clock_seen;   // at least one edge since the clock was lost
    uint32_t clock_since;  // samples since the last rising edge
    float    clock_period; // samples between edges, 0 = not measured yet
    uint32_t clock_count;  // edges since the clock (re)appeared
    float cv_smooth[kNumModSlots];
    float   mod_prev[kNumModSlots][3]; // last chunk's mod on amount / offset / rate
    float   mod_out[kNumModSlots];     // what each slot puts out right now (offset + amount × source)
    uint8_t mod_prev_over[kNumModSlots][3];
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
// With an Override slot on it, the stored value is replaced by the middle of
// the range (the slot sweeps from there).
inline float ModValue(float base, float lo, float hi, float sum, bool over)
{
    const float centre = over ? 0.5f * (lo + hi) : base;
    return Clampf(centre + sum * (hi - lo), lo, hi);
}

inline float Pv(const PerseidsAlgorithm* a, int p)
{
    return ModValue(static_cast<float>(ParamValue(a, p)), a->params[p].min, a->params[p].max,
                    a->mod_sum[p], a->mod_over[p] != 0);
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
    a->match_speed = ParamValue(a, kParamMatchSpeed);

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

bool BuildModOverview(struct PerseidsAlgorithm* a);
void UpdateModMarks(struct PerseidsAlgorithm* a);
void LayoutPages(struct PerseidsAlgorithm* a, bool mod_first);

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
    // Every mod target shows its value through parameterString(), so a
    // modulated parameter can carry " ~" right in its value (all hosts draw
    // that; renamed parameters are not shown by nt_emu).
    for(int k = 1; k < kNumModTargets; ++k)
    {
        _NT_parameter& d = a->params[kModTargets[k]];
        if(d.unit != kNT_unitHasStrings && d.unit != kNT_unitConfirm)
            d.unit = kNT_unitHasStrings;
    }
    a->parameters     = a->params;
    a->pages_desc.numPages = kNumPages;
    a->pages_desc.pages    = a->pages;
    a->overview_count      = 0;
    a->pages_mod_first     = false;
    LayoutPages(a, false);
    a->parameterPages = &a->pages_desc;
    BuildModOverview(a);

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
    a->match_speed  = 1;
    a->lm_sp_lock   = 0.f;
    a->lm_sw_lock   = 0.f;
    a->out_gain     = 1.f;
    a->playing      = true;
    a->rec_gate     = false;
    a->clock_gate   = false;
    a->clock_valid  = false;
    a->clock_seen   = false;
    a->clock_since  = 0;
    a->clock_period = 0.f;
    a->clock_count  = 0;
    a->sample_clock = 0;
    a->slice_acc    = 0;
    a->gov_acc      = 0;
    a->gov_cycles   = 0;
    a->selected     = 0;
    a->mod_view     = false;
    a->mod_sel      = 0;
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
    {
        a->mod_sum[p]    = 0.f;
        a->mod_over[p]   = 0;
        a->mod_marked[p] = 0;
    }
    for(int m = 0; m < kNumModSlots; ++m)
    {
        a->amount_name[m][0]   = 0;
        a->amount_name_dest[m] = 0;
    }
    for(int m = 0; m < kNumModSlots; ++m)
    {
        a->lfo_phase[m] = 0.25f * static_cast<float>(m % 4) + 0.0833f * static_cast<float>(m / 4); // spread out
        a->cv_smooth[m] = 0.f;
        a->lfo_sub[m]   = 0;
        a->lfo_last[m]  = 0.f;
        a->lfo_rand[m]  = 0.f;
        a->mod_out[m]   = 0.f;
        for(int f = 0; f < kArmFields; ++f)
            a->mod_armed[m][f] = a->mod_seen[m][f] = 0;
        a->line_seen[m] = a->amount_seen[m] = 0;
        a->mod_pending_since[m] = 0;
        a->mod_prev[m][0] = a->mod_prev[m][1] = a->mod_prev[m][2] = 0.f;
        a->mod_prev_over[m][0] = a->mod_prev_over[m][1] = a->mod_prev_over[m][2] = 0;
    }
    a->mod_active  = 0;
    a->mod_arm_now = true; // the stored settings count from the start
    a->mod_reset_ui = false;
    a->lfo_rng      = 0x2545F491u;
    a->mod_applied = false;
    return a;
}

void parameterChanged(_NT_algorithm* self, int p)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    a->dirty             = true;
    // A destination changed: rebuild the Mod overview now. The host's menu
    // is on screen then, and draw() (which also does it) may not run.
    for(int m = 0; m < kNumModSlots; ++m)
        if(p == ModParam(m, kModDest) && a->v && BuildModOverview(a))
        {
            NT_updateParameterPages(NT_algorithmIndex(a));
            break;
        }
    // A slot's input / dest / mode / sync: arm it later (kModArmS).
    for(int m = 0; m < kNumModSlots; ++m)
        for(int f = 0; f < kArmFields; ++f)
            if(p == ArmParam(m, f))
                a->mod_pending_since[m] = a->sample_clock + 1;
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

// One LFO waveform at phase 0…1, output −1…+1.
float ModShape(int shape, float ph)
{
    constexpr float kCurve = 4.f; // bend of Shark / Exp / Log
    const float     norm   = 1.f / (1.f - std::exp(-kCurve));
    auto Charge = [&](float t) { return (1.f - std::exp(-kCurve * t)) * norm; };      // fast, then slow
    auto Rise   = [&](float t) { return (std::exp(kCurve * t) - 1.f) / (std::exp(kCurve) - 1.f); }; // slow, then fast
    switch(shape)
    {
        case kShapeSine: return std::sin(6.2831853f * ph);
        case kShapeTriangle: return 1.f - 4.f * std::fabs(ph - 0.5f);
        case kShapeSawUp: return 2.f * ph - 1.f;
        case kShapeSawDown: return 1.f - 2.f * ph;
        case kShapeSquare: return ph < 0.5f ? 1.f : -1.f;
        case kShapeShark: // curved rise, curved fall (capacitor charge / discharge)
            return 2.f * (ph < 0.5f ? Charge(2.f * ph) : 1.f - Charge(2.f * ph - 1.f)) - 1.f;
        case kShapeSharkRev: // the same, back to front
        {
            const float q = 1.f - ph;
            return 2.f * (q < 0.5f ? Charge(2.f * q) : 1.f - Charge(2.f * q - 1.f)) - 1.f;
        }
        case kShapeExp: return 2.f * Rise(ph) - 1.f;
        case kShapeLog: return 2.f * Charge(ph) - 1.f;
        default: return ModLfo(ph);
    }
}

// Mod slots for one chunk: contrib = Offset + Amount × source, summed per
// destination (ARCHITECTURE 4.3). CV: ±5 V = ±1, smoothed over ~5 ms.
// Returns true if any slot is doing something.
bool UpdateMod(PerseidsAlgorithm* a, const float* busFrames, int numFrames, int at, int n)
{
    // Overview lines mirror the amounts: whichever changed is copied to the
    // other (the "seen" values hold the target, so a write that lands a
    // little later is not taken for a new change).
    for(int m = 0; m < kNumModSlots; ++m)
    {
        const int16_t line = static_cast<int16_t>(ParamValue(a, ModLineParam(m)));
        const int16_t amt  = static_cast<int16_t>(ParamValue(a, ModParam(m, kModAmount)));
        if(line != a->line_seen[m])
        {
            if(amt != line)
                SetParamAudio(a, ModParam(m, kModAmount), line);
            a->line_seen[m] = a->amount_seen[m] = line;
        }
        else if(amt != a->amount_seen[m])
        {
            if(line != amt)
                SetParamAudio(a, ModLineParam(m), amt);
            a->line_seen[m] = a->amount_seen[m] = amt;
        }
    }
    // Arm pending slot settings that stood still long enough (or at once).
    // Changes are found by comparing with the values seen last time, so this
    // does not depend on the host calling parameterChanged() (nt_emu's menu
    // does not always).
    for(int m = 0; m < kNumModSlots; ++m)
        for(int f = 0; f < kArmFields; ++f)
        {
            const int16_t v = static_cast<int16_t>(ParamValue(a, ArmParam(m, f)));
            if(v != a->mod_seen[m][f])
            {
                a->mod_seen[m][f]       = v;
                a->mod_pending_since[m] = a->sample_clock + 1;
            }
        }
    {
        const bool     now  = a->mod_arm_now;
        const float    rate = static_cast<float>(NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000);
        const uint64_t wait = static_cast<uint64_t>(kModArmS * rate);
        for(int m = 0; m < kNumModSlots; ++m)
        {
            const uint64_t since = a->mod_pending_since[m];
            if(now || (since && a->sample_clock + 1 >= since + wait))
            {
                for(int f = 0; f < kArmFields; ++f)
                    a->mod_armed[m][f] = static_cast<int16_t>(ParamValue(a, ArmParam(m, f)));
                a->mod_pending_since[m] = 0;
            }
        }
        a->mod_arm_now = false;
    }
    const int mod_fields[3] = {kModAmount, kModOffset, kModRate};
    for(int m = 0; m < kNumModSlots; ++m)
        for(int k = 0; k < 3; ++k)
        {
            a->mod_prev[m][k]      = a->mod_sum[ModParam(m, mod_fields[k])];
            a->mod_prev_over[m][k] = a->mod_over[ModParam(m, mod_fields[k])];
        }
    for(int p = 0; p < kNumParams; ++p)
    {
        a->mod_sum[p]  = 0.f;
        a->mod_over[p] = 0;
    }
    // A slot's own amount / offset / rate with last chunk's modulation on it.
    auto Eff = [a](int m, int field, int k) {
        const int p = ModParam(m, field);
        return ModValue(static_cast<float>(ParamValue(a, p)), a->params[p].min, a->params[p].max,
                        a->mod_prev[m][k], a->mod_prev_over[m][k] != 0);
    };

    const float sr     = static_cast<float>(NT_globals.sampleRate > 0 ? NT_globals.sampleRate : 48000);
    int         active = 0;

    // Clock in: rising edge above 1 V (re-armed below 0.5 V), at most one edge
    // per chunk counts (chunks are ≤ 1.3 ms; faster clocks are not musical here).
    bool       edge      = false;
    const int  clock_bus = ParamValue(a, kParamClockIn);
    if(clock_bus > 0)
    {
        const float* ck = busFrames + (clock_bus - 1) * numFrames + at;
        for(int i = 0; i < n; ++i)
        {
            ++a->clock_since;
            if(!a->clock_gate && ck[i] > 1.f)
            {
                a->clock_gate = true;
                if(a->clock_since >= static_cast<uint32_t>(0.002f * sr)) // 2 ms debounce
                {
                    if(a->clock_seen)
                    {
                        // Second edge on: the period is known, the clock runs.
                        a->clock_period = static_cast<float>(a->clock_since);
                        if(!a->clock_valid)
                        {
                            a->clock_valid = true;
                            a->clock_count = 0;
                        }
                        else
                            ++a->clock_count;
                        edge = true;
                    }
                    a->clock_seen  = true;
                    a->clock_since = 0;
                }
            }
            else if(a->clock_gate && ck[i] < 0.5f)
                a->clock_gate = false;
        }
    }
    else
        a->clock_since += static_cast<uint32_t>(n);
    {
        const float lost = a->clock_period > 1.f ? Clampf(4.f * a->clock_period, 0.f, 3.f * sr) : 3.f * sr;
        if(clock_bus <= 0 || static_cast<float>(a->clock_since) > lost)
        {
            a->clock_valid  = false;
            a->clock_seen   = false;
            a->clock_period = 0.f;
        }
    }
    for(int m = 0; m < kNumModSlots; ++m)
    {
        const int   dest   = kModTargets[a->mod_armed[m][kArmDest]];
        const float amount = Eff(m, kModAmount, 0) * 0.01f;
        const float offset = Eff(m, kModOffset, 1) * 0.01f;
        const int   bus    = a->mod_armed[m][kArmIn];

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
            const int sync = a->mod_armed[m][kArmSync];
            if(sync > kModSyncLocked)
            {
                // Free rate, restarted on the clock grid.
                const int r   = sync - kModSyncLocked;
                const int div = kModSyncDiv[r];
                const int mul = kModSyncMul[r];
                a->lfo_phase[m] += Eff(m, kModRate, 2) * 0.01f * static_cast<float>(n) / sr;
                if(a->clock_valid && a->clock_period > 1.f)
                {
                    if(edge)
                    {
                        if(a->clock_count % static_cast<uint32_t>(div) == 0)
                            a->lfo_phase[m] = 0.f;
                        a->lfo_sub[m] = 0;
                    }
                    else if(mul > 1)
                    {
                        int k = static_cast<int>(static_cast<float>(a->clock_since) * mul / a->clock_period);
                        k     = k > mul - 1 ? mul - 1 : k;
                        if(k > a->lfo_sub[m])
                        {
                            a->lfo_phase[m] = 0.f;
                            a->lfo_sub[m]   = k;
                        }
                    }
                }
            }
            else if(sync > 0 && a->clock_valid && a->clock_period > 1.f)
            {
                // Locked: Mul cycles per Div pulses, phase reset on the bar.
                const float cycles = static_cast<float>(kModSyncMul[sync]) / static_cast<float>(kModSyncDiv[sync]);
                if(edge && a->clock_count % static_cast<uint32_t>(kModSyncDiv[sync]) == 0)
                    a->lfo_phase[m] = 0.f;
                else
                    a->lfo_phase[m] += cycles * static_cast<float>(n) / a->clock_period;
            }
            else
                a->lfo_phase[m] += Eff(m, kModRate, 2) * 0.01f * static_cast<float>(n) / sr;
            a->lfo_phase[m] -= static_cast<float>(static_cast<int>(a->lfo_phase[m]));
            // New random step at every cycle start (phase wrapped or reset).
            if(a->lfo_phase[m] < a->lfo_last[m])
            {
                a->lfo_rng ^= a->lfo_rng << 13;
                a->lfo_rng ^= a->lfo_rng >> 17;
                a->lfo_rng ^= a->lfo_rng << 5;
                a->lfo_rand[m] = static_cast<float>(a->lfo_rng >> 8) * (2.f / 16777216.f) - 1.f;
            }
            a->lfo_last[m]  = a->lfo_phase[m];
            const int shape = ParamValue(a, ModShapeParam(m));
            source          = shape == kShapeRandom ? a->lfo_rand[m] : ModShape(shape, a->lfo_phase[m]);
        }

        a->mod_out[m] = dest && (amount != 0.f || offset != 0.f) ? offset + amount * source : 0.f;
        if(!dest || (amount == 0.f && offset == 0.f))
            continue;
        if(a->mod_armed[m][kArmMode])
        {
            // Override: half the travel per 100 %, so Amount 100 % spans the range.
            a->mod_sum[dest] += 0.5f * (offset + amount * source);
            a->mod_over[dest] = 1;
        }
        else
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
        a->lm_sp_lock = a->lm_sw_lock = 0.f; // new material: lock in again
        SetParamAudio(a, kParamClear, 0);
    }
    // "Reset all mods": every slot back to off, then the switch falls back.
    if(a->v[kParamModReset])
    {
        for(int m = 0; m < kNumModSlots; ++m)
        {
            // Everything back to its default: input, destination, amount,
            // offset, LFO rate, sync, mode.
            SetParamAudio(a, ModParam(m, kModIn), 0);
            SetParamAudio(a, ModParam(m, kModRate), kParameterDefs[ModParam(m, kModRate)].def);
            SetParamAudio(a, ModParam(m, kModDest), 0);
            SetParamAudio(a, ModParam(m, kModAmount), 0);
            SetParamAudio(a, ModParam(m, kModOffset), 0);
            SetParamAudio(a, ModSyncParam(m), 0);
            SetParamAudio(a, ModModeParam(m), 0);
            SetParamAudio(a, ModShapeParam(m), 0);
            SetParamAudio(a, ModLineParam(m), 0);
        }
        SetParamAudio(a, kParamModReset, 0);
        a->mod_arm_now   = true; // a reset acts at once
        a->mod_reset_ui  = true; // draw(): rebuild pages, host redraws the menus
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
                {
                    a->mod_sum[p]  = 0.f;
                    a->mod_over[p] = 0;
                }
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
            const int   spd   = a->match_speed < 0 ? 0 : (a->match_speed > 2 ? 2 : a->match_speed);
            auto K = [&](float secs) { return 1.f - std::exp(-fn / (secs * sr)); };
            const float k_atk = K(kLevelMatchAtkS);
            const float k_rel = K(kLevelMatchRelS);
            auto Follow = [&](PerseidsAlgorithm::Loudness& L, float ms) {
                L.st += k_st * (ms - L.st);
                L.env += (L.st > L.env ? k_atk : k_rel) * (L.st - L.env);
            };
            float in_ms = 0.f;
            for(int i = 0; i < n; ++i)
                in_ms += e.trail_mix[i] * e.trail_mix[i];
            Follow(a->lm_in, in_ms / fn);
            auto Learn = [&](PerseidsAlgorithm::Loudness& L, float ms, float& g, float target, float& lock) {
                const int sp = lock < kLevelMatchLockS ? 2 : spd;
                Follow(L, ms);
                float to = g;
                if(!a->level_match)
                    to = 1.f;
                else if(a->lm_in.env > kLevelMatchFloor && L.env > kLevelMatchFloor * 1e-2f)
                {
                    to = Clampf(target * std::sqrt(a->lm_in.env / L.env),
                                1.f / kLevelMatchMax, kLevelMatchMax);
                    lock += fn / sr;
                }
                g += K(kLevelMatchGainS[sp]) * (to - g);
            };
            g_sp0 = a->lm_g_sp;
            g_sw0 = a->lm_g_sw;
            if(run_spectra)
                Learn(a->lm_sp, sp_ms, a->lm_g_sp, kLevelMatchTarget, a->lm_sp_lock);
            if(run_swarm)
                Learn(a->lm_sw, sw_ms, a->lm_g_sw, kLevelMatchTarget * kLevelMatchSwarmTrim, a->lm_sw_lock);
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
    // Menu marks ("Name ~") follow the armed slots also while the host's
    // menu is open (draw() is not called then), ~20 times a second.
    if((a->sample_clock & 2047u) < static_cast<uint64_t>(numFrames))
    {
        UpdateModMarks(a);
        // Same for the Mod overview, in case the host's menu changed a
        // destination without calling parameterChanged().
        if(BuildModOverview(a))
            NT_updateParameterPages(NT_algorithmIndex(a));
    }

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

// Slots aimed at something (same rule as the Mod overview), in slot order.
int ActiveModSlots(const PerseidsAlgorithm* a, int out[kNumModSlots])
{
    int n = 0;
    for(int m = 0; m < kNumModSlots; ++m)
        if(a->v && a->v[ModParam(m, kModDest)] != 0)
            out[n++] = m;
    return n;
}

void customUi(_NT_algorithm* self, const _NT_uiData& data)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    if(!a->v)
        return;
    // Back on Perseids' own screen: pending slot settings count now.
    for(int m = 0; m < kNumModSlots; ++m)
        if(a->mod_pending_since[m])
            a->mod_arm_now = true;

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
    int        active[kNumModSlots];
    const int  n_active = ActiveModSlots(a, active);
    if(data.encoders[0])
    {
        if(!a->mod_view)
        {
            // Trails 1…Count; one step past the last Trail opens the Mod view.
            int       s = a->selected + data.encoders[0];
            const int n = Count(a);
            if(s > n - 1)
            {
                a->mod_view = true;
                a->mod_sel  = 0;
                s           = n - 1;
            }
            a->selected = s < 0 ? 0 : s;
        }
        else
        {
            // Through the tiles; left of the first one back to the Trails.
            int m = a->mod_sel + data.encoders[0];
            if(m < 0)
                a->mod_view = false;
            else
                a->mod_sel = n_active > 0 ? (m > n_active - 1 ? n_active - 1 : m) : 0;
        }
    }
    if(a->mod_view && a->mod_sel > (n_active > 0 ? n_active - 1 : 0))
        a->mod_sel = n_active > 0 ? n_active - 1 : 0;
    if(data.encoders[1])
    {
        if(!a->mod_view)
        {
            const int p = kParamTrailLevel1 + a->selected;
            SetParamUi(a, p, ParamValue(a, p) + 2 * data.encoders[1]);
        }
        else if(n_active > 0)
        {
            // Mod view: Encoder R sets the selected slot's amount, 1 % a step.
            const int p = ModParam(active[a->mod_sel], kModAmount);
            SetParamUi(a, p, ParamValue(a, p) + data.encoders[1]);
        }
    }
    // Encoder clicks: Solo / Lock in the Trail view (none in the Mod view).
    if(ClickEdge(a->click_l, data, kNT_encoderButtonL, turned) && !a->mod_view)
    {
        const int p = kParamSolo1 + a->selected;
        SetParamUi(a, p, ParamValue(a, p) ? 0 : 1);
    }
    if(ClickEdge(a->click_r, data, kNT_encoderButtonR, turned) && !a->mod_view)
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

// The value in the parameter's own unit, as the host would show it.
int FormatParam(const _NT_parameter& d, int v, char* buff)
{
    const int div = d.scaling == kNT_scaling10 ? 10 : (d.scaling == kNT_scaling100 ? 100 : (d.scaling == kNT_scaling1000 ? 1000 : 1));
    int       n   = 0;
    if(d.unit == kNT_unitEnum && d.enumStrings)
    {
        std::strcpy(buff, d.enumStrings[v]);
        return static_cast<int>(std::strlen(buff));
    }
    const bool sign = d.unit == kNT_unitSemitones || d.unit == kNT_unitDb || d.unit == kNT_unitCents;
    int        iv   = v;
    if(iv < 0)
    {
        buff[n++] = '-';
        iv        = -iv;
    }
    else if(sign)
        buff[n++] = '+';
    n += NT_intToString(buff + n, iv / div);
    if(div > 1)
    {
        buff[n++] = '.';
        int frac = iv % div;
        for(int k = div / 10; k > 0; k /= 10)
        {
            buff[n++] = static_cast<char>('0' + frac / k);
            frac %= k;
        }
    }
    const char* u = "";
    switch(d.unit)
    {
        case kNT_unitPercent: u = "%"; break;
        case kNT_unitSemitones: u = " st"; break;
        case kNT_unitDb: u = " dB"; break;
        case kNT_unitHz: u = " Hz"; break;
        case kNT_unitSeconds: u = " s"; break;
        case kNT_unitCents: u = " c"; break;
        default: break;
    }
    std::strcpy(buff + n, u);
    return n + static_cast<int>(std::strlen(u));
}

// Is a working slot aimed at parameter p? (armed destination, amount or offset set)
bool ParamModulated(const PerseidsAlgorithm* a, int p)
{
    if(!a || !a->v)
        return false;
    for(int m = 0; m < kNumModSlots; ++m)
    {
        const int d = a->mod_armed[m][kArmDest];
        if(d > 0 && kModTargets[d] == p
           && (a->v[ModParam(m, kModAmount)] != 0 || a->v[ModParam(m, kModOffset)] != 0))
            return true;
    }
    return false;
}

int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
    const PerseidsAlgorithm* a = static_cast<const PerseidsAlgorithm*>(self);
    int                      n = -1;
    // Mod amount: the setting, and while the slot works what it puts out now
    // ("40 % > +23 %"). The overview line of the slot shows the same with the
    // destination in front ("Blend 40 % > +23 %").
    for(int m = 0; m < kNumModSlots && n < 0; ++m)
    {
        const bool line = p == ModLineParam(m);
        if(p != ModParam(m, kModAmount) && !line)
            continue;
        n = 0;
        if(line && a && a->v && a->v[ModParam(m, kModDest)] != 0)
        {
            const char* d = kModTargetNames[a->v[ModParam(m, kModDest)]];
            std::strcpy(buff, d);
            n = static_cast<int>(std::strlen(d));
            buff[n++] = ' ';
        }
        n += NT_intToString(buff + n, v);
        std::strcpy(buff + n, " %");
        n += 2;
        if(a && a->v && a->mod_armed[m][kArmDest] != 0 && (v != 0 || a->v[ModParam(m, kModOffset)] != 0))
        {
            const float o  = a->mod_out[m] * 100.f;
            const int   oi = static_cast<int>(o >= 0.f ? o + 0.5f : o - 0.5f);
            std::strcpy(buff + n, " > ");
            n += 3;
            if(oi >= 0)
                buff[n++] = '+';
            n += NT_intToString(buff + n, oi);
            std::strcpy(buff + n, " %");
            n += 2;
        }
    }
    if(n < 0 && p == kParamHold)
    {
        if(v >= kHoldInf)
        {
            std::strcpy(buff, "INF");
            n = 3;
        }
        else
        {
            n = NT_intToString(buff, v);
            std::strcpy(buff + n, " s");
            n += 2;
        }
    }
    if(n < 0)
    {
        // Every other mod target: its usual value text (see construct()).
        bool target = false;
        for(int k = 1; k < kNumModTargets && !target; ++k)
            target = kModTargets[k] == p;
        if(!target)
            return 0;
        n = FormatParam(kParameterDefs[p], v, buff);
    }
    // Modulated right now: " ~" after the value, in every menu.
    if(ParamModulated(a, p))
    {
        std::strcpy(buff + n, " ~");
        n += 2;
    }
    return n;
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

// Mod view -----------------------------------------------------------------

// Where a slot's destination stands right now, 0…1 of its travel; and where
// it would stand without modulation (the stored value, or the middle with an
// Override slot on it).
float ModDestNow(const PerseidsAlgorithm* a, int dest)
{
    const float lo = a->params[dest].min;
    const float hi = a->params[dest].max;
    return hi > lo ? (Pv(a, dest) - lo) / (hi - lo) : 0.f;
}
float ModDestBase(const PerseidsAlgorithm* a, int dest)
{
    const float lo = a->params[dest].min;
    const float hi = a->params[dest].max;
    const float b  = ModValue(static_cast<float>(ParamValue(a, dest)), lo, hi, 0.f, a->mod_over[dest] != 0);
    return hi > lo ? (b - lo) / (hi - lo) : 0.f;
}

// ~25 samples a second into each slot's scope trace.
void SampleModHistory(PerseidsAlgorithm* a, uint32_t now_ms)
{
    Engines& e = *a->eng;
    if(now_ms - e.mod_hist_ms < 40u && e.mod_hist_ms != 0)
        return;
    e.mod_hist_ms  = now_ms ? now_ms : 1;
    e.mod_hist_pos = static_cast<uint8_t>((e.mod_hist_pos + 1) % Engines::kModHist);
    for(int m = 0; m < kNumModSlots; ++m)
    {
        const int d = kModTargets[a->mod_armed[m][kArmDest]];
        const float x = d ? ModDestNow(a, d) : 0.f;
        e.mod_hist[m][e.mod_hist_pos] = static_cast<uint8_t>(Clampf(x, 0.f, 1.f) * 255.f + 0.5f);
    }
}

// Value of a parameter as text in its own units (scaling 10 / 100 shown as
// decimals), for the numeric Mod view.
void FormatParamValue(const PerseidsAlgorithm* a, int p, float v, char* buff)
{
    const int sc  = a->params[p].scaling;
    const int div = sc == kNT_scaling10 ? 10 : (sc == kNT_scaling100 ? 100 : 1);
    int       iv  = static_cast<int>(v >= 0.f ? v + 0.5f : v - 0.5f);
    int       n   = 0;
    if(iv < 0)
    {
        buff[n++] = '-';
        iv        = -iv;
    }
    n += NT_intToString(buff + n, iv / div);
    if(div > 1)
    {
        buff[n++] = '.';
        n += NT_intToString(buff + n, (iv % div) / (div / 10));
    }
    if(a->params[p].unit == kNT_unitPercent || a->params[p].unit == kNT_unitHasStrings)
        buff[n++] = '%';
    buff[n] = 0;
}

// Tiles, 4 × 2, one per active slot: number, destination, amount, and a
// small scope trace of the destination over ~2 s with the base value dotted
// (graphic), or base / now / offset as numbers. More than 8: pages of 8.
void DrawModView(PerseidsAlgorithm* a)
{
    int       active[kNumModSlots];
    const int n = ActiveModSlots(a, active);
    char      buff[40];
    if(n == 0)
    {
        NT_drawText(128, 30, "No active mods", 12, kNT_textCentre);
        NT_drawText(128, 42, "set one up on a Mod page (Mod n dest)", 6, kNT_textCentre, kNT_textTiny);
        NT_drawText(128, 50, "Encoder L left: back to the Trails", 6, kNT_textCentre, kNT_textTiny);
        return;
    }
    const Engines& e       = *a->eng;
    const bool     numbers = ParamValue(a, kParamModView) != 0;
    const int      page    = a->mod_sel / 8;
    for(int k = page * 8; k < n && k < page * 8 + 8; ++k)
    {
        const int m   = active[k];
        const int col = (k % 8) % 4;
        const int row = (k % 8) / 4;
        const int x0  = 4 + col * 62; // room for the arrows at both edges
        const int y0  = 11 + row * 22;
        const int x1  = x0 + 58;
        const int y1  = y0 + 20;
        const bool sel = k == a->mod_sel;
        NT_drawShapeI(kNT_box, x0, y0, x1, y1, sel ? 12 : 3);

        // Line 1: slot, destination (cut to fit), amount.
        const int  di   = a->v[ModParam(m, kModDest)];
        const int  dest = kModTargets[di];
        const bool armed = a->mod_armed[m][kArmDest] == di;
        NT_intToString(buff, m + 1);
        NT_drawText(x0 + 2, y0 + 7, buff, 15, kNT_textLeft, kNT_textTiny);
        const int amt = ParamValue(a, ModParam(m, kModAmount));
        NT_intToString(buff, amt);
        std::strcat(buff, "%");
        NT_drawText(x1 - 2, y0 + 7, buff, sel ? 15 : 12, kNT_textRight, kNT_textTiny);
        // Destination name, cut to the room left of the amount (4 px a character).
        const int room = (x1 - 2 - 4 * static_cast<int>(std::strlen(buff)) - 4 - (x0 + 11)) / 4;
        char      name[16];
        std::strncpy(name, kModTargetNames[di], sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        if(room >= 0 && room < static_cast<int>(sizeof(name)))
            name[room] = 0;
        NT_drawText(x0 + 11, y0 + 7, name, armed ? 10 : 5, kNT_textLeft, kNT_textTiny);

        if(!numbers)
        {
            // Scope: 0…100 % of the destination's travel, bottom to top.
            const int gx0 = x0 + 2, gx1 = x1 - 2, gy0 = y0 + 9, gy1 = y1 - 2;
            const int gh  = gy1 - gy0;
            const int by  = gy1 - static_cast<int>(ModDestBase(a, dest) * gh + 0.5f);
            for(int x = gx0; x <= gx1; x += 3)
                NT_drawShapeI(kNT_point, x, by, x, by, 5);
            const int w   = gx1 - gx0;
            int       px  = -1, py = 0;
            for(int i = 0; i <= w; ++i)
            {
                // Newest sample at the right edge.
                const int h   = (Engines::kModHist - 1) * i / (w > 0 ? w : 1);
                const int idx = (e.mod_hist_pos + 1 + h) % Engines::kModHist;
                const int y   = gy1 - (e.mod_hist[m][idx] * gh + 127) / 255;
                const int x   = gx0 + i;
                if(px >= 0)
                    NT_drawShapeI(kNT_line, px, py, x, y, armed ? 15 : 6);
                px = x;
                py = y;
            }
        }
        else
        {
            // Numbers: base → now, offset and mode.
            const float lo = a->params[dest].min, hi = a->params[dest].max;
            char        b1[16], b2[16];
            FormatParamValue(a, dest, lo + ModDestBase(a, dest) * (hi - lo), b1);
            FormatParamValue(a, dest, lo + ModDestNow(a, dest) * (hi - lo), b2);
            std::strcpy(buff, b1);
            std::strcat(buff, " > ");
            std::strcat(buff, b2);
            NT_drawText(x0 + 2, y0 + 13, buff, armed ? 15 : 6, kNT_textLeft, kNT_textTiny);
            std::strcpy(buff, "off ");
            NT_intToString(buff + 4, ParamValue(a, ModParam(m, kModOffset)));
            std::strcat(buff, ParamValue(a, ModModeParam(m)) ? "% ovr" : "%");
            NT_drawText(x0 + 2, y0 + 19, buff, 8, kNT_textLeft, kNT_textTiny);
        }
    }
    // Arrows: left of the first tile = back (to the Trails, or the previous
    // page); right of the fourth tile = more than eight active slots, the
    // next page follows.
    NT_drawShapeI(kNT_line, 2, 18, 0, 21, 8);
    NT_drawShapeI(kNT_line, 2, 24, 0, 21, 8);
    if(n > page * 8 + 8)
    {
        NT_drawShapeI(kNT_line, 252, 18, 255, 21, 12);
        NT_drawShapeI(kNT_line, 252, 24, 255, 21, 12);
    }
}

// Where the Mod overview sits in the page list right now.
int OverviewPageIndex(const PerseidsAlgorithm* a) { return a->pages_mod_first ? 0 : kModOverviewPage; }

// Page order: the usual one, or — while the Mod view is on screen — the Mod
// overview first, so the menu opens there (nt_emu and, as far as known, the
// NT start a menu visit on the first page).
void LayoutPages(PerseidsAlgorithm* a, bool mod_first)
{
    int k = 0;
    if(mod_first)
        a->pages[k++] = kPages[kModOverviewPage];
    for(int i = 0; i < kNumPages; ++i)
        if(!(mod_first && i == kModOverviewPage))
            a->pages[k++] = kPages[i];
    a->pages_mod_first = mod_first;
    _NT_parameterPage& ov = a->pages[OverviewPageIndex(a)];
    ov.params             = a->overview;
    ov.numParams          = a->overview_count;
}

// Rebuilds the Mod overview page from the slots in use; tells the host only
// when it changed. Returns true then.
bool BuildModOverview(PerseidsAlgorithm* a)
{
    uint8_t list[kModOverviewMax];
    int     n    = 0;
    list[n++]    = kParamModReset;
    for(int m = 0; m < kNumModSlots; ++m)
    {
        if(a->v && a->v[ModParam(m, kModDest)] != 0)
            list[n++] = static_cast<uint8_t>(ModLineParam(m)); // one line: "Mod n   Blend 40 % > +12 %"
    }
    if(a->overview_count == n && !std::memcmp(a->overview, list, static_cast<size_t>(n)))
        return false;
    std::memcpy(a->overview, list, static_cast<size_t>(n));
    a->overview_count = static_cast<uint8_t>(n);
    a->pages[OverviewPageIndex(a)].numParams = static_cast<uint8_t>(n);
    return true;
}

// Marks modulated parameters in the menus: their name gets " ~" while a slot
// is aimed at them (and back when it stops). Display thread, only on change.
// Marks and destination names live in the value strings now (parameterString):
// renaming parameters needs the host to take NT_updateParameterDefinition,
// and nt_emu does not show renamed parameters. Kept as a hook.
void UpdateModMarks(PerseidsAlgorithm* /*a*/) {}

bool draw(_NT_algorithm* self)
{
    PerseidsAlgorithm* a = static_cast<PerseidsAlgorithm*>(self);
    if(!a->v)
        return false;
    UpdateModMarks(a);
    const bool reset = a->mod_reset_ui;
    a->mod_reset_ui  = false;
    // After a reset the host is told even if the page looks the same, so
    // the menus show the cleared slots (nt_emu redraws on this).
    bool pages_changed = BuildModOverview(a) || reset;
    if(a->mod_view != a->pages_mod_first)
    {
        LayoutPages(a, a->mod_view);
        pages_changed = true;
    }
    if(pages_changed)
        NT_updateParameterPages(NT_algorithmIndex(a));
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
    SampleModHistory(a, now_ms);
    if(a->mod_view)
        DrawModView(a);
    else
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

    // Hint at the right edge: Encoder L further right opens the Mod view
    // (bright when the next step gets there, i.e. on the last active Trail).
    if(!a->mod_view)
    {
        const int c = a->selected >= count - 1 ? 12 : 4;
        NT_drawShapeI(kNT_line, 253, 26, 255, 29, c);
        NT_drawShapeI(kNT_line, 253, 32, 255, 29, c);
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
        NT_drawText(148, 61, "HOLD", 15, kNT_textLeft, kNT_textTiny);
    if(e.swarm.GovernorActive())
        NT_drawText(60, 61, "CPU", 15, kNT_textLeft, kNT_textTiny);
    bool mod_pending = false;
    for(int m = 0; m < kNumModSlots; ++m)
        mod_pending = mod_pending || a->mod_pending_since[m] != 0;
    if(a->mod_active > 0 || mod_pending)
    {
        // "MOD n", with "*" while a changed slot setting waits to take effect.
        std::strcpy(buff, "MOD");
        int len = 3;
        if(a->mod_active > 0)
            len += NT_intToString(buff + 3, a->mod_active);
        if(mod_pending)
            buff[len++] = '*';
        buff[len] = 0;
        NT_drawText(76, 61, buff, 12, kNT_textLeft, kNT_textTiny);
    }
    bool wants_clock = false;
    for(int m = 0; m < kNumModSlots; ++m)
        wants_clock = wants_clock || (a->mod_armed[m][kArmSync] != 0 && a->mod_armed[m][kArmDest] != 0);
    if(a->clock_valid)
        NT_drawText(168, 61, "CLK", 12, kNT_textLeft, kNT_textTiny);
    else if(wants_clock)
        NT_drawText(168, 61, "CLK?", 6, kNT_textLeft, kNT_textTiny); // synced slot, no clock arriving
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
