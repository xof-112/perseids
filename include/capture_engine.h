#pragma once

#include "capture_params.h"
#include "record_source.h"
#include "spatial_params.h"

#include "platform/trail_sample.h"

#include "Filters/onepole.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace perseids
{

// Phase 3 capture engine — SDRAM ring buffers, round-robin, Cont.Rec, Hold/Fade.
// Phase 9 — Pan Drift LFOs + Crossfade focus wave on the Trail VCA stage
// (after pre-fader taps; SwarmViews / trail_mix see the wave; Multi dry does not).
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

    // Daisy: 5 Trails × 30 s × 48 kHz × float ≈ 28.8 MB of 64 MB SDRAM.
    // Other platforms size the bank themselves (see TrailBank).
    static constexpr size_t kTrailSdramBytes
        = kTrailCount * kMaxBufferSamples * sizeof(float);
    static_assert(kTrailSdramBytes < 64u * 1024u * 1024u,
                  "Trail buffers exceed SDRAM budget");

    // Per-trail snapshot for Swarm grain reads (written each audio block in Process).
    struct SwarmTrailView
    {
        uint32_t length; // 0 = not playable
        float    gain;   // level × fade × xfade × play_gain
        float    pan_l;  // constant-power Pan Drift
        float    pan_r;
    };

    // Spectra (mono) → stereo weight from post-VCA Trail pans (same callback).
    struct CloudPan
    {
        float l = 0.70710678f;
        float r = 0.70710678f;
    };

    // Trail storage is owned by the platform layer and handed in at Init:
    // the Daisy firmware passes its SDRAM array, the disting NT plug-in a
    // DRAM block sized by its specification. `capacity` is the number of
    // samples per Trail (recording length plus loop-seam overflow).
    struct TrailBank
    {
        TrailSample* data[kTrailCount];
        size_t       capacity;
    };

    void Init(float sample_rate, const TrailBank& bank);

    // Audio thread — non-blocking.
    // Writes dry monitor to out_l/out_r and the Trail VCA sum (× play_gain)
    // into trail_mix (may be null if unused). Spectra takes trail_mix; Swarm reads
    // trail_buffer via SwarmViews() filled in the same Process call.
    void Process(const float* in_l,
                 const float* in_r,
                 float*       out_l,
                 float*       out_r,
                 float*       trail_mix,
                 size_t       size);

    // Same-callback reader for Swarm (after Capture::Process).
    const SwarmTrailView* SwarmViews() const { return swarm_views_; }
    CloudPan              LastCloudPan() const { return cloud_pan_; }

    float PlayGain() const { return play_gain_; }

    // UI thread — copy registry params + mixer state before/after audio use.
    void SyncFromUi(const CaptureParamValues& params,
                    const TrailMixerState     mixer[kTrailCount],
                    bool                      playing,
                    const SpatialParamValues& spatial);

    void RequestManualTrigger();
    // Overwrite OFF and INF Hold: true = automatic triggers (Threshold,
    // Cont. Rec) leave INF Trails alone too; only Rec/Trig replaces the
    // oldest one. false (default, ARCHITECTURE 4.8) = INF stays stealable.
    void SetProtectInfiniteHold(bool on) { protect_inf_hold_ = on; }
    void ClearAll(); // Delete-all confirmed

    // Dashboard / Rec indicator (UI-safe snapshots).
    float InputLevel() const
    {
        return input_level_.load(std::memory_order_relaxed);
    }
    float InputLevelR() const
    {
        return input_level_r_.load(std::memory_order_relaxed);
    }
    uint8_t  RecTrailSlot() const
    {
        return static_cast<uint8_t>(rec_slot_display_.load(std::memory_order_relaxed));
    }
    bool RecActive() const { return rec_active_.load(std::memory_order_relaxed); }
    float HoldRemainingNorm(size_t trail) const;
    void  GetTrailLifeUi(TrailLifeUi out[kTrailCount]) const;

    // Dashboard Crossfade focus marker (UI thread).
    float CrossfadeFocus() const
    {
        return xfade_focus_ui_.load(std::memory_order_relaxed);
    }
    float CrossfadeAmplitudeUi() const
    {
        return xfade_amp_ui_.load(std::memory_order_relaxed);
    }

    // Seamless loop read — used by Capture playback and Swarm grain reads.
    size_t        LoopXfadeSamples(size_t length) const;
    static size_t LoopPlayLength(size_t length);
    float         ReadLooped(size_t trail, size_t pos, size_t length) const;

    // Raw Trail storage for Swarm's interpolating grain reads.
    const TrailSample* TrailData(size_t trail) const { return bank_.data[trail]; }
    size_t             TrailCapacity() const { return bank_.capacity; }
    // Longest recording the bank can hold, in seconds (Buffer upper bound).
    float MaxBufferSeconds() const;

  private:
    enum class TrailState : uint8_t
    {
        Empty,
        Recording,
        Playing,
        FadingOut,
        ArmingRecord, // BBD-slew fade before overwriting (anti-click)
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
        float      hold_elapsed   = 0.f; // samples of Hold already played
        float      fade_gain      = 0.f;
        float      fade_inc       = 0.f; // linear fades (Fade In/Out); Arming uses slew
        bool       infinite_hold  = false;
        bool       just_finished_rec = false;
    };

    size_t BufferLengthSamples() const;
    int    ActiveCount() const;
    size_t PickRoundRobinTarget(bool manual) const;
    void   StartRecording(size_t index);
    void   BeginRecordWrites(size_t index);
    void   FinishRecording(size_t index);
    void   BeginHold(size_t index);
    // Re-derive Hold of every playing Trail from a changed Hold value.
    void   ApplyHoldChange();
    void   StartFadeOut(size_t index);
    void   ApplyGlobalPlayFade(bool want_play, size_t size);
    float  FilterInput(float x);
    bool   RecordSlotBusy() const;
    // Drop a stale / inconsistent write or arming head so RecordSlotBusy cannot
    // swallow every later Rec / Threshold / Cont.Rec trigger forever.
    void   AbortRecordHeads();
    void   SanitizeRecordHeads();
    float  CrossfadeGain(size_t trail, int count, bool soloed) const;
    void   AdvanceSpatial(size_t samples);
    void   ComputeTrailPan(size_t trail, float& pan_l, float& pan_r) const;
    float  PanLfo(float phase) const;

    // BBD-style one-pole slew τ before round-robin overwrite (~60 ms).
    static constexpr float kReplaceSlewSec = 0.060f;
    static constexpr float kReplaceDoneEps = 0.002f;
    // Elapsed Hold stops counting past the longest finite Hold (no float creep).
    static constexpr float kHoldElapsedCap = (kHoldInfiniteAbove + 1.f) * 192000.f;

    float sample_rate_;
    float sample_rate_inv_;

    TrailBank bank_;

    CaptureParamValues params_;
    SpatialParamValues spatial_;
    TrailMixerState    mixer_[kTrailCount];
    RecordSource       record_source_;

    TrailVoice voices_[kTrailCount];
    size_t     next_generation_;
    size_t     active_record_index_; // kTrailCount = none
    size_t     arming_record_index_; // kTrailCount = none
    bool       gate_open_;           // above threshold
    bool       was_above_;
    float      envelope_follower_;
    float      applied_hold_s_;   // Hold value the voices were last set up with
    bool       protect_inf_hold_;

    // Global Play/Pause crossfade (ARCHITECTURE: over Fade In / Fade Out times).
    float play_gain_;
    bool  want_playing_;

    // Phase 9 spatial state (audio thread only).
    float    pan_phase_master_;
    float    pan_jitter_[kTrailCount];
    uint32_t pan_rng_;
    float    xfade_focus_; // 0…count (wraps)

    std::atomic<uint32_t> manual_trig_count_;
    uint32_t              manual_trig_seen_;
    std::atomic<uint32_t> clear_count_;
    uint32_t              clear_seen_;

    std::atomic<float>   input_level_;
    std::atomic<float>   input_level_r_;
    std::atomic<uint8_t> rec_slot_display_;
    std::atomic<bool>    rec_active_;
    std::atomic<float>   hold_remaining_norm_[kTrailCount];
    std::atomic<uint8_t> life_phase_[kTrailCount];
    std::atomic<float>   life_fill_[kTrailCount];
    std::atomic<int16_t> life_hold_sec_[kTrailCount];
    std::atomic<float>   xfade_focus_ui_; // 0…Count (wraps)
    std::atomic<float>   xfade_amp_ui_;

    daisysp::OnePole hp_;
    daisysp::OnePole lp_;

    // Per instance, so several plug-in instances never share Trail state.
    SwarmTrailView swarm_views_[kTrailCount];
    CloudPan       cloud_pan_;
};

} // namespace perseids
