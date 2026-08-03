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
    // 2048-point RFFT. 512 gave 93.75 Hz bins: a 100 Hz fundamental landed on
    // bin 1 (unresolvable), a semitone at 200 Hz was 0.13 bins, and ±1 bin of
    // peak jitter moved a partial by more than a fifth — that bin jitter *was*
    // the "flea/siren" character. 2048 → 23.4 Hz bins.
    static constexpr size_t kFftSize     = 2048;
    static constexpr size_t kHopSize     = 1024;
    static constexpr size_t kBinCount    = kFftSize / 2;
    // Hard cap for audio CPU — registry Partials still 4..64, clamped here.
    static constexpr size_t kMaxPartials = 32;
    static constexpr size_t kMinPartials = 4;
    static constexpr size_t kInputRing   = kFftSize * 4; // power-of-two

    void Init(float sample_rate);

    void SyncFromUi(const SpectraParamValues& params, float pitch_both = 0.f);

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
    float              pitch_both_; // Engines PB 0..1 → PSP octave span 1×…2×

    // FFT scratch stays in DTCM (zero wait states, hot inside arm_rfft).
    // Window / magnitudes / input ring live in SDRAM — see spectra_engine.cpp.
    float fft_time_[kFftSize];
    float fft_freq_[kFftSize];
    bool  mag_smooth_valid_;
    // Smoothed f0 across hops — stops multi-Trail sum from flipping the
    // harmonic grid every frame (that read as mild flea / “eiern”).
    float f0_smooth_hz_;
    bool  f0_smooth_valid_;
    // Mono/poly decision with hysteresis — flipping it reshuffles the whole
    // accept order, which is audible as the fundamental dropping out.
    int   poly_score_;
    bool  polyphonic_;

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
    // Continuous exp slew toward targets (no per-hop ramp restart — those
    // made a ~50 Hz AM staircase that read as level-proportional crackle).
    float               slew_amp_;
    float               slew_freq_;
    size_t              active_partials_;
    float               waveshape_morph_;
    float               fold_gain_;
};

} // namespace perseids
