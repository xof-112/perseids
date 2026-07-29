#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"

#include "capture_engine.h"
#include "capture_params.h"
#include "cycle_row.h"
#include "dummy_params.h"
#include "hw_pins.h"
#include "param_registry.h"
#include "reso_engine.h"
#include "reso_params.h"
#include "spectra_engine.h"
#include "spectra_params.h"
#include "swarm_engine.h"
#include "swarm_params.h"
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
perseids::DummyBlockParamValues g_dummy_params;
perseids::CaptureEngine      g_capture;
perseids::SpectraEngine      g_spectra;
perseids::SwarmEngine        g_swarm;
perseids::ResonatorEngine    g_reso;
CpuLoadMeter                 g_cpu_meter;
std::atomic<float>           g_cpu_load{0.f};

float g_trail_mix[128];
float g_spectra_out[128];
float g_swarm_out_l[128];
float g_swarm_out_r[128];

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

// Dummy cycle lists for Blocks 6, 8–10 (see dummy_params.h). Block 7 Resonator
// is live (reso_params.h).
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

// Index i pairs with rows[i] in main() — keep both lists in the same order.
const perseids::PotMapping kPotMappings[] = {
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA0}, // Pot 1  → Trails
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA1}, // Pot 2  → Time
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA2}, // Pot 3  → Engines
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA3}, // Pot 4  → Swarm
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA4}, // Pot 5  → Spectra
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB0}, // Pot 6  → Pan Drift*
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB1}, // Pot 7  → Resonator
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB2}, // Pot 8  → Reverb*
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB3}, // Pot 9  → Crossfade*
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB4}, // Pot 10 → Filter*
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

        // --- Dummy Blocks 6–10 (no DSP yet, display feedback only) ---
        {perseids::kReverbMix,
         "Mix",
         "MIX",
         0.f,
         1.f,
         0.25f,
         &g_dummy_params.rev_mix,
         DT::Unipolar,
         false},
        {perseids::kReverbDecay,
         "Decay",
         "DEC",
         0.f,
         1.f,
         0.5f,
         &g_dummy_params.rev_decay,
         DT::Unipolar,
         false},
        {perseids::kReverbDamping,
         "Damping",
         "DMP",
         0.f,
         1.f,
         0.5f,
         &g_dummy_params.rev_damping,
         DT::Unipolar,
         false},
        {perseids::kReverbCharacter,
         "Character",
         "CHR",
         -1.f,
         1.f,
         0.f,
         &g_dummy_params.rev_character,
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
         &g_dummy_params.flt_cutoff,
         DT::Unipolar,
         false},
        {perseids::kFilterResonance,
         "Resonance",
         "RES",
         0.f,
         1.f,
         0.2f,
         &g_dummy_params.flt_resonance,
         DT::Unipolar,
         false},
        {perseids::kFilterFeedback,
         "Feedback",
         "FBK",
         0.f,
         1.f,
         0.f,
         &g_dummy_params.flt_feedback,
         DT::Unipolar,
         false},
        {perseids::kFilterDestination,
         "Destination",
         "DST",
         1.f,
         4.f,
         1.f,
         &g_dummy_params.flt_destination,
         DT::CountNum,
         false},

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

    if(size > 128)
        size = 128;

    g_capture.Process(in[0], in[1], out[0], out[1], g_trail_mix, size);

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

    // Bench scaffolding until Multi Dry/Wet (Phase 11):
    // dry listen-through + blended engines (trail_mix = analysis only).
    constexpr float kDryGain = 0.40f;
    constexpr float kWetGain = 1.10f;

    for(size_t i = 0; i < size; ++i)
    {
        const float sp  = run_spectra ? g_spectra_out[i] * wet_spectra : 0.f;
        const float swl = run_swarm ? g_swarm_out_l[i] * wet_swarm : 0.f;
        const float swr = run_swarm ? g_swarm_out_r[i] * wet_swarm : 0.f;
        out[0][i] = std::tanh(out[0][i] * kDryGain + (sp + swl) * kWetGain);
        out[1][i] = std::tanh(out[1][i] * kDryGain + (sp + swr) * kWetGain);
    }

    g_cpu_meter.OnBlockEnd();
    g_cpu_load.store(g_cpu_meter.GetAvgCpuLoad(), std::memory_order_relaxed);
}

} // namespace

DaisySeed hw;

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(128);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetLed(true);

    g_capture.Init(hw.AudioSampleRate());
    g_spectra.Init(hw.AudioSampleRate());
    g_swarm.Init(hw.AudioSampleRate());
    g_reso.Init(hw.AudioSampleRate());
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
        perseids::CycleRow("Reverb", kReverbIds, 4),    // *
        perseids::CycleRow("Crossfade", kXfadeIds, 2),  // *
        perseids::CycleRow("Filter", kFilterIds, 4),    // *
        perseids::CycleRow("Settings", kSettingsIds, 4),
    };

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
            &g_cpu_load);

    hw.StartAudio(AudioCallback);

    uint32_t last_fft_ms = 0;
    while(true)
    {
        ui.Process();
        g_reso.SyncFromUi(g_reso_params,
                          g_capture_params.scale,
                          g_capture_params.intonation);
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
