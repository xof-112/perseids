#pragma once

#include "spectra_params.h"

#include "Effects/wavefolder.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace perseids
{

// Phase 4 — additive Spectra engine.
// FFT analysis runs in the main loop (ProcessAnalysis); oscillator bank runs
// in the audio callback (Process). Never call arm_rfft_* from AudioCallback.
//
// CMSIS-DSP classic F32 RFFT: arm_rfft_fast_f32(S, p, pOut, ifftFlag) — no tmpBuf.
class SpectraEngine
{
  public:
    // 512-point RFFT: enough for partial pick, half the CMSIS cost of 1024.
    static constexpr size_t kFftSize     = 512;
    static constexpr size_t kHopSize     = 256;
    static constexpr size_t kBinCount    = kFftSize / 2;
    // Hard cap for audio CPU — registry Partials still 4..64, clamped here.
    static constexpr size_t kMaxPartials = 32;
    static constexpr size_t kMinPartials = 4;
    static constexpr size_t kInputRing   = kFftSize * 4; // power-of-two

    void Init(float sample_rate);

    void SyncFromUi(const SpectraParamValues& params);

    // Audio thread — accumulate analysis input; no FFT here.
    void PushInput(const float* samples, size_t size);

    // Audio thread — oscillator bank only (must stay within block budget).
    void Process(float* out_l, float* out_r, size_t size);

    // Main loop — at most one FFT hop per call; drops backlog if behind.
    void ProcessAnalysis();

  private:
    struct PartialTarget
    {
        float freq_hz;
        float amp;
    };

    void  BuildWindow();
    void  ApplyUmbraAurora(float* mags, size_t bins, float f0_hz) const;
    void  PickPartials(const float* mags, size_t bins);
    void  PublishTargets();
    void  ConsumeTargets();
    float PitchRatio() const;
    static float FastSin(float phase01);

    float sample_rate_;
    float sample_rate_inv_;
    float bin_hz_;

    SpectraParamValues params_;

    float window_[kFftSize];
    float fft_time_[kFftSize];
    float fft_freq_[kFftSize];
    float magnitudes_[kBinCount];

    float                 input_ring_[kInputRing];
    std::atomic<uint32_t> input_write_;
    std::atomic<uint32_t> input_read_;

    // Analysis scratch (main loop only) → published snapshot for audio.
    PartialTarget         analysis_targets_[kMaxPartials];
    size_t                analysis_count_;
    PartialTarget         pending_targets_[kMaxPartials];
    size_t                pending_count_;
    // Previous published frame (analysis thread only) for peak continuity.
    PartialTarget         prev_targets_[kMaxPartials];
    size_t                prev_count_;
    std::atomic<uint32_t> targets_seq_;
    uint32_t              targets_seen_;

    // Shared wavefolder — morph applied once per block, not 32×.
    daisysp::Wavefolder folder_;
    float               phase_[kMaxPartials];
    float               osc_amp_[kMaxPartials];
    float               osc_freq_[kMaxPartials];
    float               target_amp_[kMaxPartials];
    float               target_freq_[kMaxPartials];
    float               phase_inc_[kMaxPartials];
    size_t              active_partials_;
    float               waveshape_morph_;
    float               fold_gain_;
};

} // namespace perseids
