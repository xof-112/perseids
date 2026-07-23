#pragma once

#include "capture_params.h"
#include "record_source.h"

#include "dev/sdram.h"
#include "Filters/onepole.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace perseids
{

// Phase 3 capture engine — SDRAM ring buffers, round-robin, Cont.Rec, Hold/Fade.
// Audio callback only; UI communicates via atomics / lock-free flags (Section 2.1).
class CaptureEngine
{
  public:
    static constexpr size_t kTrailCount = perseids::kTrailCount;
    // Must match DaisySeed SAI sample rate configured in main (SAI_48KHZ).
    static constexpr size_t kSampleRate       = 48000;
    static constexpr size_t kMaxBufferSeconds = 30;
    static constexpr size_t kMaxBufferSamples = kMaxBufferSeconds * kSampleRate;
    static constexpr float  kHoldInfiniteAbove = 30.f;

    // 5 Trails × 30 s × 48 kHz × float ≈ 28.8 MB of 64 MB SDRAM.
    static constexpr size_t kTrailSdramBytes
        = kTrailCount * kMaxBufferSamples * sizeof(float);
    static_assert(kTrailSdramBytes < 64u * 1024u * 1024u,
                  "Trail buffers exceed SDRAM budget");

    void Init(float sample_rate);

    // Audio thread — non-blocking.
    void Process(const float* in_l,
                 const float* in_r,
                 float*       out_l,
                 float*       out_r,
                 size_t       size);

    // UI thread — copy registry params + mixer state before/after audio use.
    void SyncFromUi(const CaptureParamValues& params,
                    const TrailMixerState     mixer[kTrailCount],
                    bool                      playing);

    void RequestManualTrigger();
    void ClearAll(); // Delete-all confirmed

    // Dashboard / Rec indicator (UI-safe snapshots).
    float    InputLevel() const { return input_level_.load(std::memory_order_relaxed); }
    uint8_t  RecTrailSlot() const
    {
        return static_cast<uint8_t>(rec_slot_display_.load(std::memory_order_relaxed));
    }
    bool RecActive() const { return rec_active_.load(std::memory_order_relaxed); }
    float HoldRemainingNorm(size_t trail) const;
    void  GetTrailLifeUi(TrailLifeUi out[kTrailCount]) const;

  private:
    enum class TrailState : uint8_t
    {
        Empty,
        Recording,
        Playing,
        FadingOut,
    };

    struct TrailVoice
    {
        TrailState state          = TrailState::Empty;
        size_t     write_pos      = 0;
        size_t     read_pos       = 0;
        size_t     length         = 0; // samples recorded / loop length
        size_t     generation     = 0; // age for round-robin (higher = newer)
        float      hold_samples_left = 0.f;
        float      hold_samples_total = 0.f;
        float      fade_gain      = 0.f;
        float      fade_inc       = 0.f;
        bool       infinite_hold  = false;
        bool       just_finished_rec = false;
    };

    size_t BufferLengthSamples() const;
    int    ActiveCount() const;
    size_t PickRoundRobinTarget() const;
    void   StartRecording(size_t index);
    void   FinishRecording(size_t index);
    void   BeginHold(size_t index);
    void   StartFadeOut(size_t index);
    void   ApplyGlobalPlayFade(bool want_play, size_t size);
    float  FilterInput(float x);

    float sample_rate_;
    float sample_rate_inv_;

    CaptureParamValues params_;
    TrailMixerState    mixer_[kTrailCount];
    RecordSource       record_source_;

    TrailVoice voices_[kTrailCount];
    size_t     next_generation_;
    size_t     active_record_index_; // kTrailCount = none
    bool       gate_open_;           // above threshold
    bool       was_above_;
    float      envelope_follower_;

    // Global Play/Pause crossfade (ARCHITECTURE: over Fade In / Fade Out times).
    float play_gain_;
    bool  want_playing_;

    std::atomic<uint32_t> manual_trig_count_;
    uint32_t              manual_trig_seen_;
    std::atomic<uint32_t> clear_count_;
    uint32_t              clear_seen_;

    std::atomic<float>   input_level_;
    std::atomic<uint8_t> rec_slot_display_;
    std::atomic<bool>    rec_active_;
    std::atomic<float>   hold_remaining_norm_[kTrailCount];
    std::atomic<uint8_t> life_phase_[kTrailCount];
    std::atomic<float>   life_fill_[kTrailCount];
    std::atomic<int16_t> life_hold_sec_[kTrailCount];

    daisysp::OnePole hp_;
    daisysp::OnePole lp_;
};

// External SDRAM storage — Section 2 point 2.
extern float DSY_SDRAM_BSS trail_buffer[CaptureEngine::kTrailCount]
                                       [CaptureEngine::kMaxBufferSamples];

} // namespace perseids
