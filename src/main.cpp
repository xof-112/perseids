#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"

#include "capture_engine.h"
#include "capture_params.h"
#include "cycle_row.h"
#include "hw_pins.h"
#include "param_registry.h"
#include "ui_controller.h"

#include <atomic>

using namespace daisy;

namespace
{

perseids::CaptureParamValues g_params;
perseids::CaptureEngine      g_capture;
CpuLoadMeter                 g_cpu_meter;
std::atomic<float>           g_cpu_load{0.f};

const uint16_t kTrailsIds[]
    = {perseids::kTrailsCount,
       perseids::kTrailsThreshold,
       perseids::kTrailsContRec,
       perseids::kTrailsOnOff};

const uint16_t kTimeIds[] = {perseids::kTimeBuffer,
                             perseids::kTimeHold,
                             perseids::kTimeFadeIn,
                             perseids::kTimeFadeOut};

const uint16_t kSettingsIds[]
    = {perseids::kSettingsCpuMeter, perseids::kSettingsRamMeter};

const perseids::PotMapping kPotMappings[] = {
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA0}, // Pot 1 → Trails
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA1}, // Pot 2 → Time
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB0}, // Pot 3 → Settings
};

bool RegisterCaptureParams(perseids::ParameterRegistry& reg)
{
    using DT = perseids::ParamDisplayType;

    const perseids::ParameterDef defs[] = {
        {perseids::kTrailsCount,
         "Count",
         "CNT",
         1.f,
         5.f,
         3.f,
         &g_params.count,
         DT::CountNum,
         false},
        {perseids::kTrailsThreshold,
         "Threshold",
         "THR",
         0.f,
         1.f,
         0.12f,
         &g_params.threshold,
         DT::Unipolar,
         false},
        {perseids::kTrailsContRec,
         "Cont. Rec",
         "CRE",
         0.f,
         1.f,
         0.f,
         &g_params.cont_rec,
         DT::Toggle,
         false},
        {perseids::kTrailsOnOff,
         "On/Off",
         "ON",
         0.f,
         1.f,
         1.f,
         &g_params.on_off,
         DT::Toggle,
         false},

        {perseids::kTimeBuffer,
         "Buffer",
         "BUF",
         0.1f,
         static_cast<float>(perseids::CaptureEngine::kMaxBufferSeconds),
         2.f,
         &g_params.buffer_s,
         DT::Seconds,
         false},
        {perseids::kTimeHold,
         "Hold",
         "HLD",
         0.f,
         31.f,
         15.f,
         &g_params.hold_s,
         DT::HoldTime,
         false},
        {perseids::kTimeFadeIn,
         "Fade In",
         "FIN",
         0.001f,
         5.f,
         3.f,
         &g_params.fade_in_s,
         DT::Seconds,
         false},
        {perseids::kTimeFadeOut,
         "Fade Out",
         "FOUT",
         0.001f,
         5.f,
         3.f,
         &g_params.fade_out_s,
         DT::Seconds,
         false},

        {perseids::kSettingsCpuMeter,
         "CPU meter",
         "CPU",
         0.f,
         1.f,
         0.f,
         &g_params.cpu_meter,
         DT::Toggle,
         false},
        {perseids::kSettingsRamMeter,
         "RAM meter",
         "RAM",
         0.f,
         1.f,
         0.f,
         &g_params.ram_meter,
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
    g_capture.Process(in[0], in[1], out[0], out[1], size);
    g_cpu_meter.OnBlockEnd();
    g_cpu_load.store(g_cpu_meter.GetAvgCpuLoad(), std::memory_order_relaxed);
}

} // namespace

DaisySeed hw;

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(48);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetLed(true);

    g_capture.Init(hw.AudioSampleRate());
    g_cpu_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

    perseids::ParameterRegistry registry;
    RegisterCaptureParams(registry);

    perseids::CycleRow rows[] = {
        perseids::CycleRow("Trails", kTrailsIds, 4),
        perseids::CycleRow("Time", kTimeIds, 4),
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
            g_params,
            &g_cpu_load);

    hw.StartAudio(AudioCallback);

    while(true)
        ui.Process();
}
