#include "capture_engine.h"

#include <cmath>

namespace perseids
{

float DSY_SDRAM_BSS trail_buffer[CaptureEngine::kTrailCount]
                                [CaptureEngine::kMaxBufferSamples];

CaptureEngine::SwarmTrailView
    CaptureEngine::swarm_views_[CaptureEngine::kTrailCount];
CaptureEngine::CloudPan CaptureEngine::cloud_pan_;

namespace
{
constexpr float kPi  = 3.14159265f;
constexpr float kHalfPi = 1.57079633f;

inline float Clampf(float x, float lo, float hi)
{
    if(x < lo)
        return lo;
    if(x > hi)
        return hi;
    return x;
}

inline bool ToggleOn(float v) { return v >= 0.5f; }

inline float FastSin(float x)
{
    // x in radians; wrap to -pi…pi then polynomial approx.
    const float twopi = 6.2831853f;
    x = x - twopi * static_cast<float>(static_cast<int>(x / twopi));
    if(x > kPi)
        x -= twopi;
    if(x < -kPi)
        x += twopi;
    const float x2 = x * x;
    return x * (1.f - x2 * (1.f / 6.f - x2 * (1.f / 120.f)));
}

inline float FastCos(float x) { return FastSin(x + kHalfPi); }
} // namespace

void CaptureEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    next_generation_ = 1;
    active_record_index_ = kTrailCount;
    arming_record_index_ = kTrailCount;
    gate_open_       = false;
    was_above_       = false;
    envelope_follower_ = 0.f;
    play_gain_       = 1.f;
    want_playing_    = true;
    manual_trig_count_.store(0, std::memory_order_relaxed);
    manual_trig_seen_ = 0;
    clear_count_.store(0, std::memory_order_relaxed);
    clear_seen_ = 0;
    input_level_.store(0.f, std::memory_order_relaxed);
    rec_slot_display_.store(1, std::memory_order_relaxed);
    rec_active_.store(false, std::memory_order_relaxed);

    params_  = CaptureParamValues{};
    spatial_ = SpatialParamValues{};
    pan_phase_master_ = 0.f;
    pan_rng_          = 0xA5F1523u;
    xfade_focus_      = 0.f;
    xfade_focus_ui_.store(0.f, std::memory_order_relaxed);
    xfade_amp_ui_.store(0.f, std::memory_order_relaxed);
    cloud_pan_        = CloudPan{};
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        mixer_[i] = TrailMixerState{};
        voices_[i] = TrailVoice{};
        pan_jitter_[i] = 0.f;
        swarm_views_[i].length = 0;
        swarm_views_[i].gain   = 0.f;
        swarm_views_[i].pan_l  = 0.70710678f;
        swarm_views_[i].pan_r  = 0.70710678f;
        hold_remaining_norm_[i].store(0.f, std::memory_order_relaxed);
        life_phase_[i].store(static_cast<uint8_t>(TrailLifePhase::Empty),
                              std::memory_order_relaxed);
        life_fill_[i].store(0.f, std::memory_order_relaxed);
        life_hold_sec_[i].store(0, std::memory_order_relaxed);
    }

    // 20 Hz HP / 20 kHz LP — Section 2 point 4. OnePole freq is fraction of Fs.
    hp_.Init();
    hp_.SetFilterMode(daisysp::OnePole::FILTER_MODE_HIGH_PASS);
    hp_.SetFrequency(20.f * sample_rate_inv_);

    lp_.Init();
    lp_.SetFilterMode(daisysp::OnePole::FILTER_MODE_LOW_PASS);
    lp_.SetFrequency(Clampf(20000.f * sample_rate_inv_, 0.f, 0.497f));

    for(size_t t = 0; t < kTrailCount; ++t)
        for(size_t s = 0; s < kMaxBufferSamples; ++s)
            trail_buffer[t][s] = 0.f;
}

float CaptureEngine::FilterInput(float x)
{
    return lp_.Process(hp_.Process(x));
}

size_t CaptureEngine::BufferLengthSamples() const
{
    const float secs
        = Clampf(params_.buffer_s, 0.05f, static_cast<float>(kMaxBufferSeconds));
    size_t      n    = static_cast<size_t>(secs * sample_rate_ + 0.5f);
    if(n < 64)
        n = 64;
    if(n > kMaxBufferSamples)
        n = kMaxBufferSamples;
    return n;
}

// Loop-seam (~40 ms): recording writes past the loop end; FinishRecording
// equal-power-blends that overflow into the head so x[length−1]→x[0] is
// continuous *in the buffer*. Playback then hard-wraps the full length —
// no shortened play length / runtime CF (those fought the baked seam and
// left level-proportional crackle on both Spectra and Swarm).
size_t CaptureEngine::LoopXfadeSamples(size_t length)
{
    size_t xf = static_cast<size_t>(0.040f * static_cast<float>(kSampleRate) + 0.5f);
    if(xf > length / 4)
        xf = length / 4;
    if(length + xf > kMaxBufferSamples)
        xf = kMaxBufferSamples - length;
    return xf;
}

size_t CaptureEngine::LoopPlayLength(size_t length)
{
    // Full recorded loop — seam is baked at FinishRecording.
    return length;
}

float CaptureEngine::ReadLooped(size_t trail, size_t pos, size_t length)
{
    if(length < 2 || trail >= kTrailCount)
        return 0.f;
    if(pos >= length)
        pos %= length;
    return trail_buffer[trail][pos];
}

int CaptureEngine::ActiveCount() const
{
    return static_cast<int>(Clampf(params_.count, 1.f, 5.f) + 0.5f);
}

size_t CaptureEngine::PickRoundRobinTarget() const
{
    const int  count       = ActiveCount();
    const bool allow_steal = ToggleOn(params_.overwrite); // OFF = Hold-Lock

    size_t best       = kTrailCount;
    size_t best_gen   = SIZE_MAX;

    for(int i = 0; i < count; ++i)
    {
        const size_t idx = static_cast<size_t>(i);
        if(mixer_[idx].locked)
            continue;
        if(voices_[idx].state == TrailState::Recording
           || voices_[idx].state == TrailState::ArmingRecord)
            continue;

        // Overwrite OFF: finish finite Hold + Fade-Out before replace (INF still
        // stealable). Empty preferred either way.
        if(!allow_steal)
        {
            if(voices_[idx].state == TrailState::FadingOut)
                continue;
            if(voices_[idx].state == TrailState::Playing
               && !voices_[idx].infinite_hold)
                continue;
        }

        // Prefer Empty, then oldest generation among eligible slots.
        const size_t gen = voices_[idx].state == TrailState::Empty
                               ? 0
                               : voices_[idx].generation;
        if(gen < best_gen)
        {
            best_gen = gen;
            best     = idx;
        }
    }
    return best;
}

bool CaptureEngine::RecordSlotBusy() const
{
    return active_record_index_ < kTrailCount
           || arming_record_index_ < kTrailCount;
}

void CaptureEngine::BeginRecordWrites(size_t index)
{
    // Only one SDRAM write head — demote any stray Recording / Arming voices.
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        if(i == index)
            continue;
        if(voices_[i].state == TrailState::Recording
           || voices_[i].state == TrailState::ArmingRecord)
        {
            voices_[i].state  = TrailState::Empty;
            voices_[i].length = 0;
            voices_[i].fade_gain = 0.f;
            voices_[i].fade_inc  = 0.f;
        }
    }

    TrailVoice& v = voices_[index];
    v.state              = TrailState::Recording;
    v.write_pos          = 0;
    v.read_pos           = 0;
    v.length             = BufferLengthSamples();
    v.generation         = next_generation_++;
    v.fade_gain          = 0.f;
    v.fade_inc           = 0.f;
    v.hold_samples_left  = 0.f;
    v.hold_samples_total = 0.f;
    v.infinite_hold      = false;
    v.just_finished_rec  = false;
    active_record_index_ = index;
    arming_record_index_ = kTrailCount;
    rec_slot_display_.store(static_cast<uint8_t>(index + 1),
                            std::memory_order_relaxed);
    rec_active_.store(true, std::memory_order_relaxed);
}

void CaptureEngine::StartRecording(size_t index)
{
    TrailVoice& v = voices_[index];

    // Replacing an audible playing trail: fade out first, then overwrite.
    // (Cont.Rec often arms the next take the sample after FinishRecording —
    // a hard mute here is the "end of recording" click while Play is on.)
    const bool audible
        = (v.state == TrailState::Playing || v.state == TrailState::FadingOut
           || v.state == TrailState::ArmingRecord)
          && v.fade_gain > 0.001f && v.length >= 2;

    if(audible)
    {
        // Cancel any other arming slot.
        if(arming_record_index_ < kTrailCount && arming_record_index_ != index)
        {
            TrailVoice& o = voices_[arming_record_index_];
            if(o.state == TrailState::ArmingRecord)
            {
                o.state     = TrailState::Empty;
                o.length    = 0;
                o.fade_gain = 0.f;
                o.fade_inc  = 0.f;
            }
        }

        // BBD-style one-pole slew toward silence (Phase 9); fade_inc unused.
        v.state    = TrailState::ArmingRecord;
        v.fade_inc = 0.f;
        arming_record_index_ = index;
        rec_slot_display_.store(static_cast<uint8_t>(index + 1),
                                std::memory_order_relaxed);
        rec_active_.store(true, std::memory_order_relaxed);
        return;
    }

    BeginRecordWrites(index);
}

void CaptureEngine::BeginHold(size_t index)
{
    TrailVoice& v = voices_[index];
    const float hold = params_.hold_s;
    if(hold > kHoldInfiniteAbove)
    {
        v.infinite_hold      = true;
        v.hold_samples_left  = 0.f;
        v.hold_samples_total = 0.f;
    }
    else
    {
        v.infinite_hold      = false;
        const float secs
            = Clampf(hold, 0.f, kHoldInfiniteAbove);
        v.hold_samples_total = secs * sample_rate_;
        v.hold_samples_left  = v.hold_samples_total;
    }
}

void CaptureEngine::FinishRecording(size_t index)
{
    TrailVoice& v = voices_[index];
    if(v.write_pos < 64)
    {
        v.state = TrailState::Empty;
        v.length = 0;
    }
    else
    {
        if(v.write_pos > v.length)
        {
            // Bake equal-power seam into the head: at k=0 the sample is the
            // natural continuation of x[length−1] (overflow); over ~40 ms it
            // fades to the original head. Hard wrap length−1→0 is then clean
            // for every reader (trail_mix → Spectra, buffer → Swarm).
            const size_t xf  = v.write_pos - v.length;
            float*       buf = trail_buffer[index];
            const float  inv = 1.f / static_cast<float>(xf);
            for(size_t k = 0; k < xf; ++k)
            {
                const float t      = static_cast<float>(k) * inv;
                const float w_head = std::sin(1.5707964f * t);
                const float w_ovf  = std::cos(1.5707964f * t);
                buf[k] = buf[k] * w_head + buf[v.length + k] * w_ovf;
            }
        }
        else
        {
            v.length = v.write_pos; // defensive: partial take, no seam data
        }
        v.read_pos          = 0;
        v.state             = TrailState::Playing;
        v.just_finished_rec = true;
        BeginHold(index);

        // Fade in newly finished take using Fade In time.
        const float fade_s = Clampf(params_.fade_in_s, 0.001f, 5.f);
        const float fade_n = fade_s * sample_rate_;
        v.fade_gain = 0.f;
        v.fade_inc  = fade_n > 1.f ? (1.f / fade_n) : 1.f;
    }

    if(active_record_index_ == index)
    {
        active_record_index_ = kTrailCount;
        rec_active_.store(false, std::memory_order_relaxed);
    }
    if(arming_record_index_ == index)
        arming_record_index_ = kTrailCount;
}

void CaptureEngine::StartFadeOut(size_t index)
{
    TrailVoice& v = voices_[index];
    if(v.state == TrailState::Empty || v.state == TrailState::Recording)
        return;
    if(mixer_[index].locked)
        return; // Lock protects against fade-out (4.2 / 4.8)

    const float fade_s = Clampf(params_.fade_out_s, 0.001f, 5.f);
    const float fade_n = fade_s * sample_rate_;
    v.state    = TrailState::FadingOut;
    v.fade_inc = fade_n > 1.f ? (-v.fade_gain / fade_n) : -1.f;
}

void CaptureEngine::RequestManualTrigger()
{
    manual_trig_count_.fetch_add(1, std::memory_order_relaxed);
}

void CaptureEngine::ClearAll()
{
    clear_count_.fetch_add(1, std::memory_order_relaxed);
}

float CaptureEngine::HoldRemainingNorm(size_t trail) const
{
    if(trail >= kTrailCount)
        return 0.f;
    return hold_remaining_norm_[trail].load(std::memory_order_relaxed);
}

void CaptureEngine::GetTrailLifeUi(TrailLifeUi out[kTrailCount]) const
{
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        out[i].phase = static_cast<TrailLifePhase>(
            life_phase_[i].load(std::memory_order_relaxed));
        out[i].fill = life_fill_[i].load(std::memory_order_relaxed);
        out[i].hold_sec
            = life_hold_sec_[i].load(std::memory_order_relaxed);
    }
}

void CaptureEngine::SyncFromUi(const CaptureParamValues& params,
                              const TrailMixerState     mixer[kTrailCount],
                              bool                      playing,
                              const SpatialParamValues& spatial)
{
    params_  = params;
    spatial_ = spatial;
    for(size_t i = 0; i < kTrailCount; ++i)
        mixer_[i] = mixer[i];
    want_playing_ = playing;

    record_source_.SetMode(params.routing >= 0.5f ? AudioRoutingMode::Sidechain
                                                  : AudioRoutingMode::Stereo);
}

float CaptureEngine::PanLfo(float phase) const
{
    // phase in cycles 0…1 → triangle/sine blend in −1…1.
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
    const float sine = FastSin(ph * 6.2831853f);
    return tri * 0.55f + sine * 0.45f;
}

void CaptureEngine::ComputeTrailPan(size_t trail, float& pan_l, float& pan_r) const
{
    const float amp = Clampf(spatial_.pan_amplitude, 0.f, 1.f);
    const int   n   = ActiveCount();
    const float n_f = n > 0 ? static_cast<float>(n) : 1.f;
    // Phase=0 → sync; Phase=1 → even spacing over one LFO cycle.
    const float offset
        = static_cast<float>(trail) * Clampf(spatial_.pan_phase, 0.f, 1.f)
          / n_f;
    const float ph  = pan_phase_master_ + offset;
    const float lfo = PanLfo(ph) + pan_jitter_[trail] * 0.12f;
    const float pan = Clampf(lfo * amp, -1.f, 1.f);
    // Constant-power: pan −1…+1 → angle 0…π/2.
    const float angle = (pan + 1.f) * 0.25f * kPi;
    pan_l = FastCos(angle);
    pan_r = FastSin(angle);
}

float CaptureEngine::CrossfadeGain(size_t trail, int count, bool soloed) const
{
    // Solo overrides the wave (ARCHITECTURE Block 9 detail).
    if(soloed)
        return 1.f;
    const float amp = Clampf(spatial_.xfade_amplitude, 0.f, 1.f);
    if(amp < 0.001f || count <= 1)
        return 1.f;
    if(static_cast<int>(trail) >= count)
        return 1.f;

    const float n = static_cast<float>(count);
    float       d = static_cast<float>(trail) - xfade_focus_;
    if(d < 0.f)
        d = -d;
    if(d > n * 0.5f)
        d = n - d;

    float lobe = 0.f;
    if(d < 1.f)
    {
        const float c = FastCos(d * kHalfPi); // 1 at focus, 0 at ±1 Trail
        lobe          = c * c;
    }
    return (1.f - amp) + amp * lobe;
}

void CaptureEngine::AdvanceSpatial(size_t samples)
{
    // Pan Drift velocity (bipolar): |v|² → 0…~2 Hz, sign = direction.
    float pvel = spatial_.pan_velocity;
    if(pvel > 1.f)
        pvel = 1.f;
    else if(pvel < -1.f)
        pvel = -1.f;
    const float pan_hz = pvel * pvel * 2.f;
    const float pan_dir = pvel >= 0.f ? 1.f : -1.f;
    pan_phase_master_
        += pan_dir * pan_hz * sample_rate_inv_ * static_cast<float>(samples);
    // Keep master in a bounded range.
    if(pan_phase_master_ > 1024.f || pan_phase_master_ < -1024.f)
        pan_phase_master_
            -= static_cast<float>(static_cast<int>(pan_phase_master_));

    // Slight per-Trail jitter (smooth random walk).
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        pan_rng_ = pan_rng_ * 1664525u + 1013904223u;
        const float noise
            = (static_cast<float>(pan_rng_ >> 8) * (1.f / 16777215.f)) * 2.f
              - 1.f;
        pan_jitter_[i] += 0.002f * static_cast<float>(samples) * noise;
        pan_jitter_[i] *= 0.995f;
        pan_jitter_[i] = Clampf(pan_jitter_[i], -1.f, 1.f);
    }

    // Crossfade travel (bipolar, 4% deadzone already in stored value).
    float xvel = spatial_.xfade_velocity;
    if(xvel > 1.f)
        xvel = 1.f;
    else if(xvel < -1.f)
        xvel = -1.f;
    if(xvel > -0.02f && xvel < 0.02f)
        return; // frozen (UI deadzone is ±2%; keep a tiny guard)

    const int   count = ActiveCount();
    const float n     = count > 0 ? static_cast<float>(count) : 1.f;
    // |v|² → up to ~1.5 Trail-slots / second.
    const float rate = xvel * xvel * 1.5f;
    const float dir  = xvel >= 0.f ? 1.f : -1.f;
    xfade_focus_
        += dir * rate * sample_rate_inv_ * static_cast<float>(samples);
    while(xfade_focus_ >= n)
        xfade_focus_ -= n;
    while(xfade_focus_ < 0.f)
        xfade_focus_ += n;
}

void CaptureEngine::ApplyGlobalPlayFade(bool want_play, size_t /*size*/)
{
    const float target = want_play ? 1.f : 0.f;
    if(play_gain_ == target)
        return;

    const float fade_s = want_play ? Clampf(params_.fade_in_s, 0.001f, 5.f)
                                   : Clampf(params_.fade_out_s, 0.001f, 5.f);
    const float step   = 1.f / (fade_s * sample_rate_);
    if(want_play)
    {
        play_gain_ += step;
        if(play_gain_ > 1.f)
            play_gain_ = 1.f;
    }
    else
    {
        play_gain_ -= step;
        if(play_gain_ < 0.f)
            play_gain_ = 0.f;
    }
}

void CaptureEngine::Process(const float* in_l,
                            const float* in_r,
                            float*       out_l,
                            float*       out_r,
                            float*       trail_mix,
                            size_t       size)
{
    // Clear-all from UI
    const uint32_t clear_now = clear_count_.load(std::memory_order_relaxed);
    if(clear_now != clear_seen_)
    {
        clear_seen_ = clear_now;
        for(size_t i = 0; i < kTrailCount; ++i)
        {
            voices_[i] = TrailVoice{};
            hold_remaining_norm_[i].store(0.f, std::memory_order_relaxed);
            life_phase_[i].store(static_cast<uint8_t>(TrailLifePhase::Empty),
                                 std::memory_order_relaxed);
            life_fill_[i].store(0.f, std::memory_order_relaxed);
            life_hold_sec_[i].store(0, std::memory_order_relaxed);
        }
        active_record_index_ = kTrailCount;
        arming_record_index_ = kTrailCount;
        rec_active_.store(false, std::memory_order_relaxed);
        gate_open_ = false;
        was_above_ = false;
    }

    const bool capture_on = ToggleOn(params_.on_off);
    const bool cont_rec   = ToggleOn(params_.cont_rec);
    const float thresh    = Clampf(params_.threshold, 0.f, 1.f);

    // Manual Rec/Trig
    const uint32_t trig_now = manual_trig_count_.load(std::memory_order_relaxed);
    const bool     manual   = (trig_now != manual_trig_seen_);
    if(manual)
        manual_trig_seen_ = trig_now;

    float peak_block = 0.f;

    // BBD replace-slew coefficient (~60 ms τ) — constant for the block.
    const float replace_coeff
        = 1.f - std::exp(-sample_rate_inv_ / kReplaceSlewSec);

    // Pan Drift / Crossfade: block-rate is enough (≪1 Trail / LFO cycle per block).
    AdvanceSpatial(size);

    bool any_solo = false;
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        if(mixer_[i].solo)
        {
            any_solo = true;
            break;
        }
    }
    const int count = ActiveCount();

    // Cache Crossfade + Pan gains for this block (same VCA stage as Trail Level).
    float xfade_g[kTrailCount];
    float pan_l[kTrailCount];
    float pan_r[kTrailCount];
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        const bool soloed = mixer_[i].solo;
        xfade_g[i] = CrossfadeGain(i, count, soloed && any_solo);
        ComputeTrailPan(i, pan_l[i], pan_r[i]);
    }

    for(size_t n = 0; n < size; ++n)
    {
        ApplyGlobalPlayFade(want_playing_, 1);

        const float raw_cap = record_source_.CaptureSample(in_l[n], in_r[n]);
        const float dry_mon
            = record_source_.DryMonitorSample(in_l[n], in_r[n]);
        const float filtered = FilterInput(raw_cap);

        const float abs_in = filtered >= 0.f ? filtered : -filtered;
        // Fast attack / slower release envelope for threshold + VU.
        constexpr float kAtk = 0.25f;
        constexpr float kRel = 0.002f;
        if(abs_in > envelope_follower_)
            envelope_follower_ += kAtk * (abs_in - envelope_follower_);
        else
            envelope_follower_ += kRel * (abs_in - envelope_follower_);

        if(abs_in > peak_block)
            peak_block = abs_in;

        const bool above = envelope_follower_ >= thresh;
        bool       trigger = false;

        if(capture_on)
        {
            // Never arm a new take while one is writing or soft-replacing.
            if(!RecordSlotBusy())
            {
                if(manual && n == 0)
                    trigger = true;
                else if(cont_rec)
                {
                    // Re-trigger while above without waiting to drop below.
                    if(above)
                        trigger = true;
                }
                else
                {
                    // Rising edge only.
                    if(above && !was_above_)
                        trigger = true;
                }
            }
        }
        was_above_ = above;
        gate_open_ = above;

        if(trigger)
        {
            const size_t target = PickRoundRobinTarget();
            if(target < kTrailCount)
                StartRecording(target);
        }

        // Write into active recording trail
        if(active_record_index_ < kTrailCount)
        {
            TrailVoice& v = voices_[active_record_index_];
            if(v.state == TrailState::Recording)
            {
                trail_buffer[active_record_index_][v.write_pos] = filtered;
                ++v.write_pos;
                // Keep writing past the loop end for the seam crossfade.
                if(v.write_pos >= v.length + LoopXfadeSamples(v.length))
                    FinishRecording(active_record_index_);
            }
        }

        // Mix playback
        float mix = 0.f;

        for(size_t i = 0; i < static_cast<size_t>(count); ++i)
        {
            TrailVoice& v = voices_[i];
            if(v.state == TrailState::Empty || v.state == TrailState::Recording)
                continue;
            if(v.length == 0)
                continue;
            if(any_solo && !mixer_[i].solo)
                continue;

            // ArmingRecord: exponential slew toward 0, then overwrite.
            if(v.state == TrailState::ArmingRecord)
            {
                v.fade_gain += replace_coeff * (0.f - v.fade_gain);
                if(v.fade_gain <= kReplaceDoneEps)
                {
                    v.fade_gain = 0.f;
                    BeginRecordWrites(i);
                    continue;
                }
            }
            else if(v.fade_inc != 0.f)
            {
                v.fade_gain += v.fade_inc;
                if(v.fade_inc > 0.f && v.fade_gain >= 1.f)
                {
                    v.fade_gain = 1.f;
                    v.fade_inc  = 0.f;
                }
                else if(v.fade_inc < 0.f && v.fade_gain <= 0.f)
                {
                    v.fade_gain = 0.f;
                    v.fade_inc  = 0.f;
                    v.state  = TrailState::Empty;
                    v.length = 0;
                    hold_remaining_norm_[i].store(0.f, std::memory_order_relaxed);
                    continue;
                }
            }

            const float vca = mixer_[i].level * v.fade_gain * xfade_g[i];

            const size_t play = LoopPlayLength(v.length);
            const float  s    = ReadLooped(i, v.read_pos, v.length);
            mix += s * vca;

            ++v.read_pos;
            if(v.read_pos >= play)
                v.read_pos = 0;

            // Hold countdown (only while Playing, not during fade-out / arming)
            if(v.state == TrailState::Playing && !v.infinite_hold
               && !mixer_[i].locked)
            {
                v.hold_samples_left -= 1.f;
                if(v.hold_samples_total > 0.f)
                {
                    const float rem = Clampf(
                        v.hold_samples_left / v.hold_samples_total, 0.f, 1.f);
                    hold_remaining_norm_[i].store(rem, std::memory_order_relaxed);
                }
                if(v.hold_samples_left <= 0.f)
                    StartFadeOut(i);
            }
            else if(v.infinite_hold || mixer_[i].locked)
            {
                hold_remaining_norm_[i].store(1.f, std::memory_order_relaxed);
            }
        }

        // The arming slot must still finish when the mix loop above cannot
        // reach it — lowering Count past its index or soloing another Trail
        // would otherwise leave the record head claimed forever, and
        // RecordSlotBusy() then silently swallows every later trigger
        // (Threshold, Cont. Rec and Rec/Trig alike).
        if(arming_record_index_ < kTrailCount)
        {
            const size_t ai = arming_record_index_;
            TrailVoice&  av = voices_[ai];
            if(av.state != TrailState::ArmingRecord)
            {
                arming_record_index_ = kTrailCount;
            }
            else
            {
                const bool reached = ai < static_cast<size_t>(count)
                                     && av.length != 0
                                     && !(any_solo && !mixer_[ai].solo);
                if(!reached)
                {
                    av.fade_gain += replace_coeff * (0.f - av.fade_gain);
                    if(av.fade_gain <= kReplaceDoneEps)
                    {
                        av.fade_gain = 0.f;
                        BeginRecordWrites(ai);
                    }
                }
            }
        }

        // Dry monitor only here — Spectra (Phase 4+) owns the wet path.
        // trail_mix = Trail VCA sum × play (level × fade × Crossfade × play).
        const float wet = mix * play_gain_;
        if(trail_mix != nullptr)
            trail_mix[n] = wet;
        out_l[n] = dry_mon;
        out_r[n] = dry_mon;
    }

    // Swarm grain sources + Spectra cloud pan — same audio callback.
    {
        float w_l   = 0.f;
        float w_r   = 0.f;
        float w_sum = 0.f;
        for(size_t i = 0; i < kTrailCount; ++i)
        {
            SwarmTrailView& sv = swarm_views_[i];
            sv.length          = 0;
            sv.gain            = 0.f;
            sv.pan_l           = pan_l[i];
            sv.pan_r           = pan_r[i];
            if(i >= static_cast<size_t>(count))
                continue;
            const TrailVoice& v = voices_[i];
            if(v.state == TrailState::Empty || v.state == TrailState::Recording)
                continue;
            if(v.length < 2)
                continue;
            if(any_solo && !mixer_[i].solo)
                continue;
            const float vca
                = mixer_[i].level * v.fade_gain * xfade_g[i] * play_gain_;
            sv.length = static_cast<uint32_t>(v.length);
            sv.gain   = vca;
            w_l += vca * pan_l[i];
            w_r += vca * pan_r[i];
            w_sum += vca;
        }
        if(w_sum > 1e-6f)
        {
            const float norm
                = 1.f / std::sqrt(w_l * w_l + w_r * w_r + 1e-12f);
            cloud_pan_.l = w_l * norm;
            cloud_pan_.r = w_r * norm;
        }
        else
        {
            cloud_pan_.l = 0.70710678f;
            cloud_pan_.r = 0.70710678f;
        }
    }

    // Mild display boost — avoid noise floor filling the VU on a quiet bench.
    const float vu = Clampf(peak_block * 1.5f, 0.f, 1.f);
    input_level_.store(vu, std::memory_order_relaxed);

    // Publish per-trail life bars for the dashboard (FIN / Hold / FOUT).
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        const TrailVoice& v = voices_[i];
        TrailLifePhase    phase = TrailLifePhase::Empty;
        float             fill  = 0.f;
        int16_t           hsec  = 0;

        if(v.state == TrailState::Recording
           || v.state == TrailState::ArmingRecord)
        {
            // Arming still shows as Recording (sparkle) — write head imminent.
            phase = TrailLifePhase::Recording;
            if(v.state == TrailState::Recording)
            {
                fill = v.length > 0
                           ? Clampf(static_cast<float>(v.write_pos)
                                        / static_cast<float>(v.length),
                                    0.f,
                                    1.f)
                           : 0.f;
            }
            else
                fill = Clampf(v.fade_gain, 0.f, 1.f);
        }
        else if(v.state == TrailState::FadingOut)
        {
            phase = TrailLifePhase::FadeOut;
            fill  = Clampf(v.fade_gain, 0.f, 1.f);
        }
        else if(v.state == TrailState::Playing)
        {
            if(v.fade_inc > 0.f && v.fade_gain < 0.999f)
            {
                phase = TrailLifePhase::FadeIn;
                fill  = Clampf(v.fade_gain, 0.f, 1.f);
            }
            else
            {
                phase = TrailLifePhase::Hold;
                fill  = 1.f;
                if(v.infinite_hold || mixer_[i].locked)
                    hsec = -1;
                else
                {
                    const float secs
                        = v.hold_samples_left * sample_rate_inv_;
                    hsec = static_cast<int16_t>(secs + 0.999f); // ceil
                    if(hsec < 0)
                        hsec = 0;
                }
            }
        }

        life_phase_[i].store(static_cast<uint8_t>(phase),
                             std::memory_order_relaxed);
        life_fill_[i].store(fill, std::memory_order_relaxed);
        life_hold_sec_[i].store(hsec, std::memory_order_relaxed);
    }

    // R / REC index always within active Count (CNT=1 → only R1 / REC1).
    if(active_record_index_ < kTrailCount
       && static_cast<int>(active_record_index_) < count)
    {
        rec_slot_display_.store(
            static_cast<uint8_t>(active_record_index_ + 1),
            std::memory_order_relaxed);
    }
    else if(arming_record_index_ < kTrailCount
            && static_cast<int>(arming_record_index_) < count)
    {
        rec_slot_display_.store(
            static_cast<uint8_t>(arming_record_index_ + 1),
            std::memory_order_relaxed);
    }
    else
    {
        const size_t next = PickRoundRobinTarget();
        uint8_t      slot = 1;
        if(next < kTrailCount && static_cast<int>(next) < count)
            slot = static_cast<uint8_t>(next + 1);
        else if(count >= 1)
            slot = 1;
        rec_slot_display_.store(slot, std::memory_order_relaxed);
    }

    // Dashboard Crossfade marker (UI thread).
    xfade_focus_ui_.store(xfade_focus_, std::memory_order_relaxed);
    xfade_amp_ui_.store(Clampf(spatial_.xfade_amplitude, 0.f, 1.f),
                        std::memory_order_relaxed);
}

} // namespace perseids
