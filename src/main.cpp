#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"

#include "capture_engine.h"
#include "capture_params.h"
#include "cycle_row.h"
#include "hw_pins.h"
#include "param_registry.h"
#include "spectra_engine.h"
#include "spectra_params.h"
#include "swarm_engine.h"
#include "swarm_params.h"
#include "ui_controller.h"

#include <atomic>

using namespace daisy;

namespace
{

perseids::CaptureParamValues g_capture_params;
perseids::SpectraParamValues g_spectra_params;
perseids::SwarmParamValues   g_swarm_params;
perseids::CaptureEngine      g_capture;
perseids::SpectraEngine      g_spectra;
perseids::SwarmEngine        g_swarm;
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

// Block 3 — Pitch Spectra/Swarm + temporary A/B (Blend = Phase 6).
const uint16_t kEnginesIds[] = {perseids::kEnginesSelect,
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
    = {perseids::kSettingsCpuMeter, perseids::kSettingsRamMeter};

const perseids::PotMapping kPotMappings[] = {
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA0}, // Pot 1 → Trails
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA1}, // Pot 2 → Time
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA2}, // Pot 3 → Engines
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB0}, // Pot 4 → Spectra
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB1}, // Pot 5 → Swarm
    // Settings (B2) unmapped until a 6th pot is wired — floating B2 thrashs UI.
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

        // Engines: A/B first (default Spectra), then pitches.
        {perseids::kEnginesSelect,
         "Swarm",
         "SWM",
         0.f,
         1.f,
         0.f,
         &g_swarm_params.engine_swarm,
         DT::Toggle,
         false},
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

    const bool use_swarm = g_swarm_params.engine_swarm >= 0.5f;

    // Bench scaffolding until Multi Dry/Wet (Phase 11):
    //   dry input listen-through + direct Trail tap + selected engine.
    // Without the Trail tap, playback is silent whenever engines are quiet /
    // Spectra silence-gated and no live input is present.
    constexpr float kDryGain   = 0.70f;
    constexpr float kTrailGain = 0.55f;
    constexpr float kWetGain   = 1.15f;

    if(use_swarm)
    {
        g_swarm.Process(g_swarm_out_l, g_swarm_out_r, size);
        for(size_t i = 0; i < size; ++i)
        {
            float l = out[0][i] * kDryGain + g_trail_mix[i] * kTrailGain
                      + g_swarm_out_l[i] * kWetGain;
            float r = out[1][i] * kDryGain + g_trail_mix[i] * kTrailGain
                      + g_swarm_out_r[i] * kWetGain;
            if(l > 1.2f)
                l = 1.2f;
            else if(l < -1.2f)
                l = -1.2f;
            if(r > 1.2f)
                r = 1.2f;
            else if(r < -1.2f)
                r = -1.2f;
            out[0][i] = l;
            out[1][i] = r;
        }
    }
    else
    {
        g_spectra.PushInput(g_trail_mix, size);
        g_spectra.Process(g_spectra_out, g_spectra_out, size);
        for(size_t i = 0; i < size; ++i)
        {
            float sample = out[0][i] * kDryGain + g_trail_mix[i] * kTrailGain
                           + g_spectra_out[i] * kWetGain;
            if(sample > 1.2f)
                sample = 1.2f;
            else if(sample < -1.2f)
                sample = -1.2f;
            out[0][i] = sample;
            out[1][i] = sample;
        }
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
    g_cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    perseids::ParameterRegistry registry;
    RegisterAllParams(registry);

    perseids::CycleRow rows[] = {
        perseids::CycleRow("Trails", kTrailsIds, 4),
        perseids::CycleRow("Time", kTimeIds, 4),
        perseids::CycleRow("Engines", kEnginesIds, 3),
        perseids::CycleRow("Spectra", kSpectraIds, 4),
        perseids::CycleRow("Swarm", kSwarmIds, 4),
        perseids::CycleRow("Settings", kSettingsIds, 2),
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
        // FFT after UI so pot/menu response stays snappy; 20 ms is enough tracking.
        if(g_swarm_params.engine_swarm < 0.5f)
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
