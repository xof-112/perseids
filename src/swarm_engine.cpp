#include "swarm_engine.h"

#include "daisy.h"

#include <cmath>

namespace perseids
{

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

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float BipolarNorm(float v, float min_v, float max_v)
{
    const float mid  = 0.5f * (min_v + max_v);
    const float half = 0.5f * (max_v - min_v);
    if(half <= 0.f)
        return 0.f;
    return Clampf((v - mid) / half, -1.f, 1.f);
}

constexpr float kPi = 3.14159265358979323846f;
// spawn_interval = grain_length × kSpawnDuty → ~1/kSpawnDuty overlapping
// grains at steady state. Amp uses √duty (energy / uncorrelated sum), not
// linear ×duty — linear made the cloud so thin that single Hann envelopes
// poked through as episodic ticks even with continuous Trail audio.
constexpr float kSpawnDuty = 0.15f;

// Blur is quantised to this grid before the envelope table is rebuilt, so a
// full Atmosphere sweep costs a bounded number of rebuilds instead of one per
// UI iteration. 1/128 is far below the audible resolution of the parameter.
constexpr float kBlurSteps = 128.f;

// Minimum spacing between two table rebuilds. The audio thread latches the
// active buffer pointer for a whole block, so two flips inside one block could
// let the writer land on the buffer the callback is still reading. 16 ms is
// three blocks at 256/48k, which makes that impossible — and it also caps what
// an Atmosphere sweep can cost the UI loop.
constexpr uint32_t kBlurRebuildMs = 16;

// Governor thresholds on the fast CPU average (block rate).
constexpr float kGovPanic   = 0.92f;
constexpr float kGovEngage  = 0.85f;
constexpr float kGovRelease = 0.70f;
// Recover one grain every N blocks (~43 ms at 256/48k) so the cloud fills back
// in smoothly and the indicator does not flicker around the threshold.
constexpr uint32_t kGovRecoverBlocks = 8;
} // namespace

void SwarmEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    params_          = SwarmParamValues{};
    spawn_phase_     = 0.f;
    rng_             = 0xA5A5u;
    slew_l_ = slew_r_ = 0.f;
    hold_l_ = hold_r_ = 0.f;
    hold_left_        = 0;

    for(size_t i = 0; i < kMaxGrains; ++i)
        grains_[i] = Grain{};
    for(size_t i = 0; i < kTrailCount; ++i)
        scan_pos_[i] = 0.f;

    inv_sqrt_[0] = 1.f;
    for(size_t i = 1; i <= kMaxGrains; ++i)
        inv_sqrt_[i] = 1.f / std::sqrt(static_cast<float>(i));

    grain_cap_   = kMaxGrains;
    gov_recover_ = 0;
    gov_active_.store(false, std::memory_order_relaxed);

    // BuildWindowTable always fills the whole back buffer before flipping the
    // index, so the audio thread never sees a partially written table.
    win_index_.store(0, std::memory_order_relaxed);
    win_blur_     = 0.f;
    win_build_ms_ = 0;
    BuildWindowTable(0.f);
}

void SwarmEngine::BuildWindowTable(float blur)
{
    const uint32_t back = win_index_.load(std::memory_order_relaxed) ^ 1u;
    float* const   tab  = window_tab_[back];

    const float powv = Lerp(1.f, 0.3f, blur);
    const float inv  = 1.f / static_cast<float>(kWindowLut);
    const bool  flat_mix = blur >= 0.001f;

    for(size_t i = 0; i <= kWindowLut; ++i)
    {
        const float x = static_cast<float>(i) * inv;
        // Hann at center Atmosphere.
        const float hann = 0.5f - 0.5f * std::cos(2.f * kPi * x);
        if(!flat_mix)
        {
            tab[i] = hann;
            continue;
        }
        // Blur: flatten toward a soft raised-sine plateau (edgeless cloud).
        const float s    = std::sin(kPi * x);
        const float flat = std::pow(s > 0.f ? s : 0.f, powv);
        tab[i]           = Lerp(hann, flat, blur);
    }

    win_index_.store(back, std::memory_order_release);
}

void SwarmEngine::SyncFromUi(const SwarmParamValues& params)
{
    params_ = params;

    // Atmosphere holds still for whole blocks at a time, so the exact blur
    // curve is tabulated here (main loop) rather than evaluated with pow() per
    // grain per sample in the callback.
    const float atmo = BipolarNorm(params_.atmosphere, -1.f, 1.f);
    const float blur = atmo < 0.f ? -atmo : 0.f;
    const float quantised
        = static_cast<float>(static_cast<int>(blur * kBlurSteps + 0.5f))
          / kBlurSteps;
    if(quantised == win_blur_)
        return;

    // Leaving win_blur_ untouched means the next UI iteration retries, so the
    // final pot position always lands even if this one is thrown away.
    const uint32_t now = daisy::System::GetNow();
    if(now - win_build_ms_ < kBlurRebuildMs)
        return;

    win_build_ms_ = now;
    win_blur_     = quantised;
    BuildWindowTable(quantised);
}

void SwarmEngine::UpdateGovernor(float cpu_load)
{
    if(cpu_load > kGovEngage && grain_cap_ > kMinGrains)
    {
        const size_t step
            = (cpu_load > kGovPanic && grain_cap_ - kMinGrains >= 2) ? 2u : 1u;
        grain_cap_ -= step;
        gov_recover_ = 0;
    }
    else if(cpu_load < kGovRelease && grain_cap_ < kMaxGrains)
    {
        if(++gov_recover_ >= kGovRecoverBlocks)
        {
            gov_recover_ = 0;
            ++grain_cap_;
        }
    }
    else
    {
        gov_recover_ = 0;
    }

    gov_active_.store(grain_cap_ < kMaxGrains, std::memory_order_relaxed);
}

float SwarmEngine::PitchRatio() const
{
    return std::pow(2.f, BipolarNorm(params_.pitch_swarm, -1.f, 1.f));
}

float SwarmEngine::GrainDurationSec() const
{
    const float u = Clampf(params_.size, 0.f, 1.f);
    // ~8 ms … ~180 ms
    return Lerp(0.008f, 0.180f, u * u);
}

float SwarmEngine::NextRand()
{
    // xorshift32
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(x & 0xFFFFFFu) * (1.f / 16777215.f);
}

float SwarmEngine::ReadInterp(size_t trail, float pos, float play_f) const
{
    if(play_f < 2.f)
        return 0.f;

    while(pos < 0.f)
        pos += play_f;
    while(pos >= play_f)
        pos -= play_f;

    const size_t play = static_cast<size_t>(play_f);
    const size_t i0   = static_cast<size_t>(pos);
    size_t       i1   = i0 + 1;
    if(i1 >= play)
        i1 = 0;
    const float  frac = pos - static_cast<float>(i0);
    // Direct buffer read: CaptureEngine::ReadLooped is a cross-TU call the
    // compiler cannot inline, and play_f already bounds both indices.
    const float* buf = trail_buffer[trail];
    const float  a   = buf[i0];
    const float  b   = buf[i1];
    return a + (b - a) * frac;
}

void SwarmEngine::SpawnGrain(size_t trail,
                             float  play_f,
                             float  dur_n,
                             float  pitch)
{
    size_t slot = grain_cap_;
    for(size_t i = 0; i < grain_cap_; ++i)
    {
        if(!grains_[i].active)
        {
            slot = i;
            break;
        }
    }
    if(slot >= grain_cap_)
        return;

    const float spread = Clampf(params_.spread, 0.f, 1.f);
    const float jitter = (NextRand() - 0.5f) * 0.04f * play_f;
    float       start  = scan_pos_[trail] + jitter;
    while(start < 0.f)
        start += play_f;
    while(start >= play_f)
        start -= play_f;

    // Stereo: ±spread from center; slight per-grain random.
    const float pan = (NextRand() * 2.f - 1.f) * spread;
    const float pl  = Clampf(0.5f * (1.f - pan), 0.f, 1.f);
    const float pr  = Clampf(0.5f * (1.f + pan), 0.f, 1.f);
    // Constant-power-ish
    const float norm = 1.f / std::sqrt(pl * pl + pr * pr + 1e-6f);

    // Playback direction (Block 5 DIR): Fwd / Rev / Rnd (coin flip per grain).
    float dir_sign = 1.f;
    {
        int mode = static_cast<int>(params_.direction + 0.5f);
        if(mode < 0)
            mode = 0;
        if(mode > 2)
            mode = 2;
        if(mode == 1)
            dir_sign = -1.f;
        else if(mode == 2)
            dir_sign = (NextRand() < 0.5f) ? -1.f : 1.f;
    }

    Grain& g  = grains_[slot];
    g.active  = true;
    g.trail   = trail;
    g.pos     = start;
    g.incr    = pitch * dir_sign;
    g.age     = 0.f;
    g.age_inc = 1.f / (dur_n > 1.f ? dur_n : 1.f);
    g.pan_l   = pl * norm;
    g.pan_r   = pr * norm;
    // Relative grain weight; live Trail gain (level×fade×play) applied in Process
    // so soft-replace fades mute grains without a hard cut.
    g.amp     = 1.f;

    const CaptureEngine::SwarmTrailView& v = CaptureEngine::SwarmViews()[trail];
    float       el = g.pan_l * v.pan_l;
    float       er = g.pan_r * v.pan_r;
    const float en = 1.f / std::sqrt(el * el + er * er + 1e-6f);
    g.eff_l        = el * en;
    g.eff_r        = er * en;
}

void SwarmEngine::Process(float* out_l, float* out_r, size_t size)
{
    const CaptureEngine::SwarmTrailView* views = CaptureEngine::SwarmViews();
    const float* const win
        = window_tab_[win_index_.load(std::memory_order_acquire)];

    const float scan_u = Clampf(params_.scan, 0.f, 1.f);
    // Scan=0 freeze; Scan=1 ≈ one full buffer pass every ~1.5 s (per active trail).
    const float scan_rate = scan_u * scan_u * (1.f / 1.5f);
    const bool  scanning  = scan_rate > 1e-6f;

    const float atmo = BipolarNorm(params_.atmosphere, -1.f, 1.f);
    const float rad  = atmo > 0.f ? atmo : 0.f;

    // Radiation: slower BBD slew + longer sample-hold.
    const float slew_coeff  = Lerp(1.f, 0.02f, rad);
    const int   hold_period = 1 + static_cast<int>(rad * 12.f + 0.5f);
    const bool  do_rad      = rad > 0.001f;
    const float rad_q       = 1.f + rad * 7.f;

    const float dur_n          = GrainDurationSec() * sample_rate_;
    const float spawn_interval = dur_n * kSpawnDuty;
    const float pitch          = PitchRatio();

    // Per-Trail block constants. SwarmViews() is written once per block at the
    // end of Capture::Process, so none of this can change inside the loop.
    float play_f[kTrailCount];
    float scan_inc[kTrailCount];
    float tgain[kTrailCount];
    bool  playable[kTrailCount];
    for(size_t t = 0; t < kTrailCount; ++t)
    {
        const size_t len = views[t].length;
        tgain[t]         = views[t].gain;
        play_f[t]        = len >= 2 ? static_cast<float>(
                        CaptureEngine::LoopPlayLength(len))
                                    : 0.f;
        playable[t]  = len >= 2;
        scan_inc[t]  = scan_rate * play_f[t] * sample_rate_inv_;
        if(playable[t] && !scanning && scan_pos_[t] >= play_f[t])
            scan_pos_[t] = 0.f;
    }

    // Grain pan × Trail Pan Drift: both factors hold for the whole block.
    for(size_t i = 0; i < kMaxGrains; ++i)
    {
        Grain& g = grains_[i];
        if(!g.active)
            continue;
        float       pl = g.pan_l * views[g.trail].pan_l;
        float       pr = g.pan_r * views[g.trail].pan_r;
        const float pn = 1.f / std::sqrt(pl * pl + pr * pr + 1e-6f);
        g.eff_l        = pl * pn;
        g.eff_r        = pr * pn;
    }

    size_t rr = 0;
    for(size_t n = 0; n < size; ++n)
    {
        // Advance scan heads (freeze when scan≈0).
        if(scanning)
        {
            for(size_t t = 0; t < kTrailCount; ++t)
            {
                if(!playable[t])
                    continue;
                scan_pos_[t] += scan_inc[t];
                if(scan_pos_[t] >= play_f[t])
                    scan_pos_[t] -= play_f[t];
            }
        }

        spawn_phase_ += 1.f;
        if(spawn_phase_ >= spawn_interval)
        {
            spawn_phase_ -= spawn_interval;
            // Round-robin among playable trails.
            for(size_t tries = 0; tries < kTrailCount; ++tries)
            {
                const size_t t = (rr + tries) % kTrailCount;
                if(play_f[t] >= 32.f && tgain[t] > 1e-4f)
                {
                    SpawnGrain(t, play_f[t], dur_n, pitch);
                    rr = (t + 1) % kTrailCount;
                    break;
                }
            }
        }

        float mix_l = 0.f;
        float mix_r = 0.f;
        int   n_on  = 0;
        for(size_t i = 0; i < kMaxGrains; ++i)
        {
            Grain& g = grains_[i];
            if(!g.active)
                continue;

            const size_t t  = g.trail;
            const float  pf = play_f[t];
            const float  tg = tgain[t];
            if(!playable[t] || tg < 1e-4f)
            {
                g.active = false;
                continue;
            }

            // Radiation lo-fi: quantize the read position into coarse steps.
            float read_pos = g.pos;
            if(do_rad)
            {
                read_pos = static_cast<float>(
                               static_cast<int>(g.pos / rad_q))
                           * rad_q;
            }
            const float sample = ReadInterp(t, read_pos, pf);

            // Tabulated grain envelope (Hann → blurred plateau).
            const float ax = g.age * static_cast<float>(kWindowLut);
            size_t      ai = static_cast<size_t>(ax);
            if(ai >= kWindowLut)
                ai = kWindowLut - 1;
            const float af  = ax - static_cast<float>(ai);
            const float env = win[ai] + (win[ai + 1] - win[ai]) * af;

            const float a = g.amp * env * tg;
            mix_l += sample * a * g.eff_l;
            mix_r += sample * a * g.eff_r;
            ++n_on;

            g.pos += g.incr;
            if(g.pos >= pf)
                g.pos -= pf;
            else if(g.pos < 0.f)
                g.pos += pf;

            g.age += g.age_inc;
            if(g.age >= 1.f)
                g.active = false;
        }

        if(n_on > 1)
        {
            const float inv = inv_sqrt_[n_on];
            mix_l *= inv;
            mix_r *= inv;
        }

        // Radiation BBD slew on the summed cloud.
        if(do_rad)
        {
            slew_l_ += slew_coeff * (mix_l - slew_l_);
            slew_r_ += slew_coeff * (mix_r - slew_r_);
            mix_l = slew_l_;
            mix_r = slew_r_;
        }
        else
        {
            slew_l_ = mix_l;
            slew_r_ = mix_r;
        }

        // Extra sample-hold for Radiation.
        if(rad > 0.05f)
        {
            if(hold_left_ <= 0)
            {
                hold_l_    = mix_l;
                hold_r_    = mix_r;
                hold_left_ = hold_period;
            }
            --hold_left_;
            mix_l = hold_l_;
            mix_r = hold_r_;
        }

        out_l[n] = std::tanh(mix_l);
        out_r[n] = std::tanh(mix_r);
    }
}

} // namespace perseids
