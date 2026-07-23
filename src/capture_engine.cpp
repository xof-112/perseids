#include "capture_engine.h"

#include <cmath>

namespace perseids
{

float DSY_SDRAM_BSS trail_buffer[CaptureEngine::kTrailCount]
                                [CaptureEngine::kMaxBufferSamples];

CaptureEngine::SwarmTrailView
    CaptureEngine::swarm_views_[CaptureEngine::kTrailCount];

namespace
{
inline float Clampf(float x, float lo, float hi)
{
    if(x < lo)
        return lo;
    if(x > hi)
        return hi;
    return x;
}

inline bool ToggleOn(float v) { return v >= 0.5f; }
} // namespace

void CaptureEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    next_generation_ = 1;
    active_record_index_ = kTrailCount;
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

    params_ = CaptureParamValues{};
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        mixer_[i] = TrailMixerState{};
        voices_[i] = TrailVoice{};
        swarm_views_[i].length = 0;
        swarm_views_[i].gain   = 0.f;
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

int CaptureEngine::ActiveCount() const
{
    return static_cast<int>(Clampf(params_.count, 1.f, 5.f) + 0.5f);
}

size_t CaptureEngine::PickRoundRobinTarget() const
{
    const int count = ActiveCount();

    size_t best       = kTrailCount;
    size_t best_gen   = SIZE_MAX;

    for(int i = 0; i < count; ++i)
    {
        const size_t idx = static_cast<size_t>(i);
        if(mixer_[idx].locked)
            continue;
        if(voices_[idx].state == TrailState::Recording)
            continue;

        // Prefer Empty, then oldest generation among Playing/FadingOut/Empty.
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

void CaptureEngine::StartRecording(size_t index)
{
    // Only one SDRAM write head — demote any stray Recording voices.
    for(size_t i = 0; i < kTrailCount; ++i)
    {
        if(i == index)
            continue;
        if(voices_[i].state == TrailState::Recording)
        {
            voices_[i].state  = TrailState::Empty;
            voices_[i].length = 0;
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
    rec_slot_display_.store(static_cast<uint8_t>(index + 1),
                            std::memory_order_relaxed);
    rec_active_.store(true, std::memory_order_relaxed);
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
        v.length            = v.write_pos;
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
                              bool                      playing)
{
    params_ = params;
    for(size_t i = 0; i < kTrailCount; ++i)
        mixer_[i] = mixer[i];
    want_playing_ = playing;

    record_source_.SetMode(params.routing >= 0.5f ? AudioRoutingMode::Sidechain
                                                  : AudioRoutingMode::Stereo);
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
            // Never arm a new take while one is already writing.
            if(active_record_index_ >= kTrailCount)
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
                if(v.write_pos >= v.length)
                    FinishRecording(active_record_index_);
            }
        }

        // Mix playback
        float mix = 0.f;
        bool  any_solo = false;
        for(size_t i = 0; i < kTrailCount; ++i)
        {
            if(mixer_[i].solo)
            {
                any_solo = true;
                break;
            }
        }

        const int count = ActiveCount();

        for(size_t i = 0; i < static_cast<size_t>(count); ++i)
        {
            TrailVoice& v = voices_[i];
            if(v.state == TrailState::Empty || v.state == TrailState::Recording)
                continue;
            if(v.length == 0)
                continue;
            if(any_solo && !mixer_[i].solo)
                continue;

            // Per-voice fade gain
            if(v.fade_inc != 0.f)
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
                    v.state     = TrailState::Empty;
                    v.length    = 0;
                    hold_remaining_norm_[i].store(0.f, std::memory_order_relaxed);
                    continue;
                }
            }

            const float s = trail_buffer[i][v.read_pos];
            mix += s * mixer_[i].level * v.fade_gain;

            ++v.read_pos;
            if(v.read_pos >= v.length)
                v.read_pos = 0;

            // Hold countdown (only while Playing, not during fade-out)
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

        // Dry monitor only here — Spectra (Phase 4+) owns the wet path.
        // trail_mix is the pre-fader Trail sum × global play gain (Section 2.5).
        const float wet = mix * play_gain_;
        if(trail_mix != nullptr)
            trail_mix[n] = wet;
        out_l[n] = dry_mon;
        out_r[n] = dry_mon;
    }

    // Swarm grain sources — same audio callback, after this Process returns.
    {
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
        for(size_t i = 0; i < kTrailCount; ++i)
        {
            SwarmTrailView& sv = swarm_views_[i];
            sv.length          = 0;
            sv.gain            = 0.f;
            if(i >= static_cast<size_t>(count))
                continue;
            const TrailVoice& v = voices_[i];
            if(v.state == TrailState::Empty || v.state == TrailState::Recording)
                continue;
            if(v.length < 2)
                continue;
            if(any_solo && !mixer_[i].solo)
                continue;
            sv.length = static_cast<uint32_t>(v.length);
            sv.gain   = mixer_[i].level * v.fade_gain * play_gain_;
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

        if(v.state == TrailState::Recording)
        {
            phase = TrailLifePhase::Recording;
            fill  = v.length > 0
                        ? Clampf(static_cast<float>(v.write_pos)
                                     / static_cast<float>(v.length),
                                 0.f,
                                 1.f)
                        : 0.f;
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
    const int count = ActiveCount();
    if(active_record_index_ < kTrailCount
       && static_cast<int>(active_record_index_) < count)
    {
        rec_slot_display_.store(
            static_cast<uint8_t>(active_record_index_ + 1),
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
}

} // namespace perseids
