#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"

#include "capture_engine.h"
#include "capture_params.h"
#include "cycle_row.h"
#include "dummy_params.h"
#include "filter_engine.h"
#include "filter_params.h"
#include "hw_pins.h"
#include "param_registry.h"
#include "reso_engine.h"
#include "reso_params.h"
#include "reverb_engine.h"
#include "reverb_params.h"
#include "spectra_engine.h"
#include "spectra_params.h"
#include "swarm_engine.h"
#include "swarm_params.h"
#include "multi_params.h"
#include "ui_controller.h"

#include <atomic>
#include <cmath>

using namespace daisy;

namespace
{

perseids::CaptureParamValues    g_capture_params;
perseids::SpectraParamValues    g_spectra_params;
perseids::SwarmParamValues      g_swarm_params;
perseids::ResoParamValues       g_reso_params;
perseids::ReverbParamValues     g_reverb_params;
perseids::FilterParamValues     g_filter_params;
perseids::MultiParamValues      g_multi_params;
perseids::DummyBlockParamValues g_dummy_params;
perseids::CaptureEngine      g_capture;
perseids::SpectraEngine      g_spectra;
perseids::SwarmEngine        g_swarm;
perseids::ResonatorEngine    g_reso;
// ReverbSc tank ~400 KB in SDRAM; engine (Chorus etc.) stays in internal RAM.
daisysp::ReverbSc DSY_SDRAM_BSS g_reverb_sc;
perseids::ReverbEngine       g_reverb;
perseids::FilterEngine       g_filter;
CpuLoadMeter                 g_cpu_meter;
std::atomic<float>           g_cpu_load{0.f};
std::atomic<float>           g_dry_wet{0.55f};

float g_trail_mix[256];
float g_spectra_out[256];
float g_swarm_out_l[256];
float g_swarm_out_r[256];
float g_reverb_send_l[256];
float g_reverb_send_r[256];
float g_reverb_wet_l[256];
float g_reverb_wet_r[256];
float g_eng_l[256];
float g_eng_r[256];
// Clean capture listen-through snapshot for Multi Dry/Wet dry side
// (Filter Dest=Input must not color the global dry tap).
float g_dry_l[256];
float g_dry_r[256];

// Filter Destination CountNum labels (1…5 → Off/Inp/Sp/Sw/Rv).
const char* const kFilterDestLabels[] = {"Off", "Inp", "Sp", "Sw", "Rv"};

// Multi Time Unit / Settings stub labels (Phase 11 interim dummies).
const char* const kTimeUnitLabels[]    = {"Sec", "Clk"};
const char* const kSettingsStubLabels[] = {"---"};

const uint16_t kTrailsIds[]
    = {perseids::kTrailsCount,
       perseids::kTrailsThreshold,
       perseids::kTrailsContRec,
       perseids::kTrailsOnOff};

const uint16_t kTimeIds[] = {perseids::kTimeBuffer,
                             perseids::kTimeHold,
                             perseids::kTimeFadeIn,
                             perseids::kTimeFadeOut};

// Block 3 — Phase 6: Blend (Spectra↔Swarm), Pitch Spectra, Pitch Swarm.
const uint16_t kEnginesIds[] = {perseids::kEnginesBlend,
                                perseids::kEnginesPitchSpectra,
                                perseids::kEnginesPitchSwarm};

const uint16_t kSpectraIds[]
    = {perseids::kSpectraPartials,
       perseids::kSpectraWaveshape,
       perseids::kSpectraUmbraAurora,
       perseids::kSpectraEnsemble};

const uint16_t kSwarmIds[] = {perseids::kSwarmSize,
                              perseids::kSwarmSpread,
                              perseids::kSwarmScan,
                              perseids::kSwarmAtmosphere};

const uint16_t kSettingsIds[]
    = {perseids::kSettingsCpuMeter,
       perseids::kSettingsRamMeter,
       perseids::kSettingsScale,
       perseids::kSettingsIntonation};

// Dummy cycle lists for Blocks 8–9 (see dummy_params.h). Blocks 6+10 live
// (reverb_params.h / filter_params.h). Block 7 Resonator: reso_params.h.
const uint16_t kReverbIds[] = {perseids::kReverbMix,
                               perseids::kReverbDecay,
                               perseids::kReverbDamping,
                               perseids::kReverbCharacter};

const uint16_t kResoIds[] = {perseids::kResoMix,
                             perseids::kResoDecay,
                             perseids::kResoPitch,
                             perseids::kResoQuantized};

const uint16_t kPanIds[] = {perseids::kPanPhase,
                            perseids::kPanAmplitude,
                            perseids::kPanVelocity};

const uint16_t kXfadeIds[]
    = {perseids::kXfadeAmplitude, perseids::kXfadeVelocity};

const uint16_t kFilterIds[] = {perseids::kFilterCutoff,
                               perseids::kFilterResonance,
                               perseids::kFilterFeedback,
                               perseids::kFilterDestination};

// Block 11 Multi — Dry/Wet first (default); rest dummy until Phase 11.
const uint16_t kMultiIds[] = {perseids::kMultiDryWet,
                              perseids::kMultiMacro1,
                              perseids::kMultiMacro2,
                              perseids::kMultiTimeUnit,
                              perseids::kMultiSettings};

// Index i pairs with rows[i] in main() — keep both lists in the same order.
const perseids::PotMapping kPotMappings[] = {
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA0}, // Pot 1  → Trails
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA1}, // Pot 2  → Time
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA2}, // Pot 3  → Engines
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA3}, // Pot 4  → Swarm
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA4}, // Pot 5  → Spectra
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB0}, // Pot 6  → Pan Drift*
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB1}, // Pot 7  → Resonator
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB2}, // Pot 8  → Reverb
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB3}, // Pot 9  → Crossfade*
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB4}, // Pot 10 → Filter
    // Settings row has no pot (Block 11 = Multi encoder, Phase 11);
    // CPU meter stays default-On for the bench (TODO(release)).
};

bool RegisterAllParams(perseids::ParameterRegistry& reg)
{
    using DT = perseids::ParamDisplayType;

    const perseids::ParameterDef defs[] = {
        {perseids::kTrailsCount,
         "Count",
         "CNT",
         1.f,
         5.f,
         3.f,
         &g_capture_params.count,
         DT::CountNum,
         false},
        {perseids::kTrailsThreshold,
         "Threshold",
         "THR",
         0.f,
         1.f,
         0.12f,
         &g_capture_params.threshold,
         DT::Unipolar,
         false},
        {perseids::kTrailsContRec,
         "Cont. Rec",
         "CRE",
         0.f,
         1.f,
         0.f,
         &g_capture_params.cont_rec,
         DT::Toggle,
         false},
        {perseids::kTrailsOnOff,
         "On/Off",
         "ON",
         0.f,
         1.f,
         1.f,
         &g_capture_params.on_off,
         DT::Toggle,
         false},

        {perseids::kTimeBuffer,
         "Buffer",
         "BUF",
         0.1f,
         static_cast<float>(perseids::CaptureEngine::kMaxBufferSeconds),
         2.f,
         &g_capture_params.buffer_s,
         DT::Seconds,
         false},
        {perseids::kTimeHold,
         "Hold",
         "HLD",
         0.f,
         31.f,
         15.f,
         &g_capture_params.hold_s,
         DT::HoldTime,
         false},
        {perseids::kTimeFadeIn,
         "Fade In",
         "FIN",
         0.001f,
         5.f,
         3.f,
         &g_capture_params.fade_in_s,
         DT::Seconds,
         false},
        {perseids::kTimeFadeOut,
         "Fade Out",
         "FOUT",
         0.001f,
         5.f,
         3.f,
         &g_capture_params.fade_out_s,
         DT::Seconds,
         false},

        // Engines: Blend first (default entry), then pitches.
        {perseids::kEnginesBlend,
         "Blend",
         "BLD",
         0.f,
         1.f,
         0.f,
         &g_swarm_params.blend,
         DT::Unipolar,
         false,
         true,   // center_mark: subtle 50% dots (equal Spectra/Swarm mix)
         "SP",   // seg row hint below 50% — Spectra side (Font_4x6, like CPU%)
         "SW"},  // seg row hint above 50% — Swarm side (nothing at exactly 50%)
        {perseids::kEnginesPitchSpectra,
         "Pitch Spectra",
         "PSP",
         -1.f,
         1.f,
         0.f,
         &g_spectra_params.pitch_spectra,
         DT::Bipolar,
         true},
        {perseids::kEnginesPitchSwarm,
         "Pitch Swarm",
         "PSW",
         -1.f,
         1.f,
         0.f,
         &g_swarm_params.pitch_swarm,
         DT::Bipolar,
         true},

        {perseids::kSpectraPartials,
         "Partials",
         "PAR",
         4.f,
         32.f,
         16.f,
         &g_spectra_params.partials,
         DT::CountBar,
         false},
        {perseids::kSpectraWaveshape,
         "Waveshape",
         "WSH",
         -1.f,
         1.f,
         0.f,
         &g_spectra_params.waveshape,
         DT::Bipolar,
         true},
        {perseids::kSpectraUmbraAurora,
         "Umbra/Aurora",
         "UMB",
         -1.f,
         1.f,
         0.f,
         &g_spectra_params.umbra_aurora,
         DT::Bipolar,
         true},
        {perseids::kSpectraEnsemble,
         "Ensemble",
         "ENS",
         0.f,
         1.f,
         0.f,
         &g_spectra_params.ensemble,
         DT::Unipolar,
         false},

        {perseids::kSwarmSize,
         "Size",
         "SIZ",
         0.f,
         1.f,
         0.45f,
         &g_swarm_params.size,
         DT::Unipolar,
         false},
        {perseids::kSwarmSpread,
         "Spread",
         "SPR",
         0.f,
         1.f,
         0.35f,
         &g_swarm_params.spread,
         DT::Unipolar,
         false},
        {perseids::kSwarmScan,
         "Scan",
         "SCN",
         0.f,
         1.f,
         0.2f,
         &g_swarm_params.scan,
         DT::Unipolar,
         false},
        {perseids::kSwarmAtmosphere,
         "Atmosphere",
         "ATM",
         -1.f,
         1.f,
         0.f,
         &g_swarm_params.atmosphere,
         DT::Bipolar,
         true},

        // --- Block 6 Reverb (Phase 8) ---
        {perseids::kReverbMix,
         "Mix",
         "MIX",
         0.f,
         1.f,
         0.25f,
         &g_reverb_params.mix,
         DT::Unipolar,
         false},
        {perseids::kReverbDecay,
         "Decay",
         "DEC",
         0.f,
         1.f,
         0.5f,
         &g_reverb_params.decay,
         DT::Unipolar,
         false},
        {perseids::kReverbDamping,
         "Damping",
         "DMP",
         0.f,
         1.f,
         0.5f,
         &g_reverb_params.damping,
         DT::Unipolar,
         false},
        {perseids::kReverbCharacter,
         "Character",
         "CHR",
         -1.f,
         1.f,
         0.f,
         &g_reverb_params.character,
         DT::Bipolar,
         true},

        {perseids::kResoMix,
         "Mix",
         "MIX",
         0.f,
         1.f,
         0.25f,
         &g_reso_params.mix,
         DT::Unipolar,
         false},
        {perseids::kResoDecay,
         "Decay",
         "DEC",
         0.f,
         1.f,
         0.5f,
         &g_reso_params.decay,
         DT::Unipolar,
         false},
        {perseids::kResoPitch,
         "Pitch",
         "PIT",
         -1.f,
         1.f,
         0.f,
         &g_reso_params.pitch,
         DT::Bipolar,
         true},
        {perseids::kResoQuantized,
         "Quantized",
         "QNT",
         0.f,
         1.f,
         0.f,
         &g_reso_params.quantized,
         DT::Toggle,
         false},

        {perseids::kPanPhase,
         "Phase",
         "PHS",
         0.f,
         1.f,
         0.f,
         &g_dummy_params.pan_phase,
         DT::Unipolar,
         false},
        {perseids::kPanAmplitude,
         "Amplitude",
         "AMP",
         0.f,
         1.f,
         0.3f,
         &g_dummy_params.pan_amplitude,
         DT::Unipolar,
         false},
        {perseids::kPanVelocity,
         "Velocity",
         "VEL",
         -1.f,
         1.f,
         0.f,
         &g_dummy_params.pan_velocity,
         DT::Bipolar,
         true},

        {perseids::kXfadeAmplitude,
         "Amplitude",
         "AMP",
         0.f,
         1.f,
         0.f,
         &g_dummy_params.xfade_amplitude,
         DT::Unipolar,
         false},
        {perseids::kXfadeVelocity,
         "Velocity",
         "VEL",
         -1.f,
         1.f,
         0.f,
         &g_dummy_params.xfade_velocity,
         DT::Bipolar,
         true},

        {perseids::kFilterCutoff,
         "Cutoff",
         "CUT",
         0.f,
         1.f,
         0.7f,
         &g_filter_params.cutoff,
         DT::Unipolar,
         false},
        {perseids::kFilterResonance,
         "Resonance",
         "RES",
         0.f,
         1.f,
         0.2f,
         &g_filter_params.resonance,
         DT::Unipolar,
         false},
        {perseids::kFilterFeedback,
         "Feedback",
         "FBK",
         0.f,
         1.f,
         0.f,
         &g_filter_params.feedback,
         DT::Unipolar,
         false},
        {perseids::kFilterDestination,
         "Destination",
         "DST",
         1.f,
         5.f,
         1.f, // boot Off — SVF skipped until a stage is chosen
         &g_filter_params.destination,
         DT::CountNum,
         false,
         false,
         nullptr,
         nullptr,
         kFilterDestLabels},

        {perseids::kSettingsCpuMeter,
         "CPU meter",
         "CPU",
         0.f,
         1.f,
         1.f, // TODO(release): default 0.f — On while Settings pot is out
         &g_capture_params.cpu_meter,
         DT::Toggle,
         false},
        {perseids::kSettingsRamMeter,
         "RAM meter",
         "RAM",
         0.f,
         1.f,
         0.f,
         &g_capture_params.ram_meter,
         DT::Toggle,
         false},
        // Scale / Intonation drive Block 7 Quantized (Phase 7). Named enum UI
        // lands with Phase 11 Multi — CountNum is the interim display.
        {perseids::kSettingsScale,
         "Scale",
         "SCL",
         0.f,
         2.f,
         0.f, // 0 Major, 1 Minor, 2 Pentatonic
         &g_capture_params.scale,
         DT::CountNum,
         false},
        {perseids::kSettingsIntonation,
         "Intonation",
         "INT",
         0.f,
         1.f,
         0.f, // 0 Equal Temperament, 1 Just Intonation
         &g_capture_params.intonation,
         DT::Toggle,
         false},

        // Phase 11 interim — Multi encoder cycle (Dry/Wet live; rest dummy UI).
        {perseids::kMultiDryWet,
         "Dry/Wet",
         "DW",
         0.f,
         1.f,
         0.55f,
         &g_multi_params.dry_wet,
         DT::Unipolar,
         false},
        {perseids::kMultiMacro1,
         "Macro1",
         "M1",
         0.f,
         1.f,
         0.50f,
         &g_multi_params.macro1,
         DT::Unipolar,
         false},
        {perseids::kMultiMacro2,
         "Macro2",
         "M2",
         0.f,
         1.f,
         0.50f,
         &g_multi_params.macro2,
         DT::Unipolar,
         false},
        {perseids::kMultiTimeUnit,
         "Time Unit",
         "TU",
         0.f,
         1.f,
         0.f,
         &g_multi_params.time_unit,
         DT::CountNum,
         false,
         false,
         nullptr,
         nullptr,
         kTimeUnitLabels},
        {perseids::kMultiSettings,
         "Settings",
         "SET",
         0.f,
         0.f,
         0.f,
         &g_multi_params.settings,
         DT::CountNum,
         false,
         false,
         nullptr,
         nullptr,
         kSettingsStubLabels},
    };

    for(const auto& def : defs)
    {
        if(!reg.Register(def))
            return false;
    }
    return true;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    g_cpu_meter.OnBlockStart();

    if(size > 256)
        size = 256;

    g_capture.Process(in[0], in[1], out[0], out[1], g_trail_mix, size);

    // Snapshot clean dry BEFORE any Block 10 Input insert — Multi Dry/Wet dry
    // side is always unprocessed listen-through (ARCHITECTURE: global blender).
    for(size_t i = 0; i < size; ++i)
    {
        g_dry_l[i] = out[0][i];
        g_dry_r[i] = out[1][i];
    }

    // Phase 6 — continuous pre-fader Blend (Block 3): equal-power crossfade
    // between the Spectra and Swarm outputs. At the extremes the silent
    // engine's synthesis is skipped entirely to save audio CPU.
    float blend = g_swarm_params.blend;
    if(blend < 0.f)
        blend = 0.f;
    else if(blend > 1.f)
        blend = 1.f;
    const float wet_spectra = std::cos(blend * 1.5707964f);
    const float wet_swarm   = std::sin(blend * 1.5707964f);
    const bool  run_spectra = wet_spectra > 0.001f;
    const bool  run_swarm   = wet_swarm > 0.001f;

    // Keep the analysis ring fed even at full Swarm so blending back toward
    // Spectra doesn't resume from an empty/stale FFT frame.
    g_spectra.PushInput(g_trail_mix, size);

    if(run_spectra)
        g_spectra.Process(g_spectra_out, g_spectra_out, size);
    if(run_swarm)
    {
        g_swarm.Process(g_swarm_out_l, g_swarm_out_r, size);
        // Spectral Resonator sits on the Swarm output (ARCHITECTURE 4.1 Block 7).
        g_reso.Process(g_swarm_out_l, g_swarm_out_r, size);
    }

    // Phase 8 — Filter Mix: inserts only on wet-chain stages (never on the
    // Multi dry tap / listen-through). Dest "Inp" = engine-sum bus (pre-reverb).
    const int  flt_dest = g_filter.Destination();
    const bool flt_on   = flt_dest != perseids::kFilterDestOff;
    if(flt_on && flt_dest == perseids::kFilterDestSpectra && run_spectra)
        g_filter.ProcessMono(g_spectra_out, size);
    if(flt_on && flt_dest == perseids::kFilterDestSwarm && run_swarm)
        g_filter.Process(g_swarm_out_l, g_swarm_out_r, size);

    // Multi Dry/Wet — final equal-power blender (ARCHITECTURE 2 / 5a):
    //   dry = clean listen-through (g_dry_*) — ONLY path for raw input to Out
    //   wet = engines (+ Resonator) + Filter + Reverb (+ future FX)
    // No effect may inject listen-through into wet (Multi@100% = cloud only).
    float dry_wet = g_dry_wet.load(std::memory_order_relaxed);
    if(dry_wet < 0.f)
        dry_wet = 0.f;
    else if(dry_wet > 1.f)
        dry_wet = 1.f;
    const float dry_g = std::cos(dry_wet * 1.5707964f);
    const float wet_g = std::sin(dry_wet * 1.5707964f) * 1.10f;

    // Cheap soft-limit (replaces per-sample tanh — major CPU at the bus).
    auto SoftLimit = [](float x) -> float {
        const float a = std::fabs(x);
        return x * (27.f + a * a) / (27.f + 9.f * a * a);
    };

    const float rev_mix = g_reverb.Mix();
    const float rev_wet_g
        = rev_mix > 0.001f ? std::sin(rev_mix * 1.5707964f) : 0.f;
    const bool run_reverb = rev_wet_g > 0.001f;

    for(size_t i = 0; i < size; ++i)
    {
        const float sp  = run_spectra ? g_spectra_out[i] * wet_spectra : 0.f;
        const float swl = run_swarm ? g_swarm_out_l[i] * wet_swarm : 0.f;
        const float swr = run_swarm ? g_swarm_out_r[i] * wet_swarm : 0.f;
        g_eng_l[i] = sp + swl;
        g_eng_r[i] = sp + swr;
    }

    // Dest Input → filter the engine sum (wet FX bus), not listen-through.
    if(flt_on && flt_dest == perseids::kFilterDestInput)
        g_filter.Process(g_eng_l, g_eng_r, size);

    if(run_reverb)
    {
        for(size_t i = 0; i < size; ++i)
        {
            g_reverb_send_l[i] = g_eng_l[i];
            g_reverb_send_r[i] = g_eng_r[i];
        }
        g_reverb.Process(g_reverb_send_l,
                         g_reverb_send_r,
                         g_reverb_wet_l,
                         g_reverb_wet_r,
                         size);
        if(flt_on && flt_dest == perseids::kFilterDestReverb)
            g_filter.Process(g_reverb_wet_l, g_reverb_wet_r, size);
    }

    for(size_t i = 0; i < size; ++i)
    {
        const float rl = run_reverb ? g_reverb_wet_l[i] * rev_wet_g : 0.f;
        const float rr = run_reverb ? g_reverb_wet_r[i] * rev_wet_g : 0.f;
        out[0][i]
            = SoftLimit(g_dry_l[i] * dry_g + (g_eng_l[i] + rl) * wet_g);
        out[1][i]
            = SoftLimit(g_dry_r[i] * dry_g + (g_eng_r[i] + rr) * wet_g);
    }

    g_cpu_meter.OnBlockEnd();
    g_cpu_load.store(g_cpu_meter.GetAvgCpuLoad(), std::memory_order_relaxed);
}

} // namespace

DaisySeed hw;

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(256);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetLed(true);

    g_capture.Init(hw.AudioSampleRate());
    g_spectra.Init(hw.AudioSampleRate());
    g_swarm.Init(hw.AudioSampleRate());
    g_reso.Init(hw.AudioSampleRate());
    g_reverb.Init(hw.AudioSampleRate(), g_reverb_sc);
    g_filter.Init(hw.AudioSampleRate());
    g_cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    perseids::ParameterRegistry registry;
    RegisterAllParams(registry);

    // Order pairs 1:1 with kPotMappings; rows past the pot count (Settings)
    // are pot-less. * = dummy blocks, display feedback only.
    perseids::CycleRow rows[] = {
        perseids::CycleRow("Trails", kTrailsIds, 4),
        perseids::CycleRow("Time", kTimeIds, 4),
        perseids::CycleRow("Engines", kEnginesIds, 3),
        perseids::CycleRow("Swarm", kSwarmIds, 4),
        perseids::CycleRow("Spectra", kSpectraIds, 4),
        perseids::CycleRow("Pan Drift", kPanIds, 3),    // *
        perseids::CycleRow("Resonator", kResoIds, 4),
        perseids::CycleRow("Reverb", kReverbIds, 4),
        perseids::CycleRow("Crossfade", kXfadeIds, 2),  // *
        perseids::CycleRow("Filter", kFilterIds, 4),
        perseids::CycleRow("Settings", kSettingsIds, 4),
    };

    perseids::CycleRow multi_row("Multi", kMultiIds, 5);

    perseids::UiController ui;
    ui.Init(hw,
            registry,
            rows,
            sizeof(rows) / sizeof(rows[0]),
            kPotMappings,
            sizeof(kPotMappings) / sizeof(kPotMappings[0]),
            g_capture,
            g_capture_params,
            g_spectra,
            g_spectra_params,
            g_swarm,
            g_swarm_params,
            multi_row,
            g_multi_params,
            &g_dry_wet,
            &g_cpu_load);

    hw.StartAudio(AudioCallback);

    uint32_t last_fft_ms = 0;
    while(true)
    {
        ui.Process();
        g_reso.SyncFromUi(g_reso_params,
                          g_capture_params.scale,
                          g_capture_params.intonation);
        g_reverb.SyncFromUi(g_reverb_params);
        g_filter.SyncFromUi(g_filter_params);
        // FFT after UI so pot/menu response stays snappy; 20 ms is enough tracking.
        // Skip only when Blend sits at (essentially) full Swarm — Spectra is
        // silent there and its synthesis is skipped in the callback anyway.
        if(g_swarm_params.blend < 0.98f)
        {
            const uint32_t now = daisy::System::GetNow();
            if(now - last_fft_ms >= 20)
            {
                g_spectra.ProcessAnalysis();
                last_fft_ms = now;
            }
        }
    }
}
