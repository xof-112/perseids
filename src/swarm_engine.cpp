#include "swarm_engine.h"

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
}

void SwarmEngine::SyncFromUi(const SwarmParamValues& params)
{
    params_ = params;
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

float SwarmEngine::WindowEnv(float age01, float blur) const
{
    age01 = Clampf(age01, 0.f, 1.f);
    // Hann at center Atmosphere.
    const float hann = 0.5f - 0.5f * std::cos(2.f * kPi * age01);
    if(blur < 0.001f)
        return hann;

    // Blur: flatten toward a soft raised-sine plateau (edgeless cloud).
    const float s    = std::sin(kPi * age01);
    const float powv = Lerp(1.f, 0.3f, blur);
    const float flat = std::pow(s > 0.f ? s : 0.f, powv);
    return Lerp(hann, flat, blur);
}

float SwarmEngine::ReadInterp(size_t trail, float pos, size_t length) const
{
    if(length < 2)
        return 0.f;

    const size_t play = CaptureEngine::LoopPlayLength(length);
    const float  play_f = static_cast<float>(play);
    while(pos < 0.f)
        pos += play_f;
    while(pos >= play_f)
        pos -= play_f;

    const size_t i0 = static_cast<size_t>(pos);
    size_t       i1 = i0 + 1;
    if(i1 >= play)
        i1 = 0;
    const float frac = pos - static_cast<float>(i0);
    const float a    = CaptureEngine::ReadLooped(trail, i0, length);
    const float b    = CaptureEngine::ReadLooped(trail, i1, length);
    return a + (b - a) * frac;
}

void SwarmEngine::SpawnGrain(size_t trail, size_t length, float gain)
{
    if(length < 32 || gain < 1e-4f)
        return;

    size_t slot = kMaxGrains;
    for(size_t i = 0; i < kMaxGrains; ++i)
    {
        if(!grains_[i].active)
        {
            slot = i;
            break;
        }
    }
    if(slot >= kMaxGrains)
        return;

    const float dur_s   = GrainDurationSec();
    const float dur_n   = dur_s * sample_rate_;
    const float pitch   = PitchRatio();
    const float spread  = Clampf(params_.spread, 0.f, 1.f);
    const float jitter  = (NextRand() - 0.5f) * 0.04f * static_cast<float>(length);
    const size_t play   = CaptureEngine::LoopPlayLength(length);
    const float  play_f = static_cast<float>(play);
    float       start   = scan_pos_[trail] + jitter;
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

    Grain& g  = grains_[slot];
    g.active  = true;
    g.trail   = trail;
    g.pos     = start;
    g.incr    = pitch;
    g.age     = 0.f;
    g.age_inc = 1.f / (dur_n > 1.f ? dur_n : 1.f);
    g.pan_l   = pl * norm;
    g.pan_r   = pr * norm;
    // Relative grain weight; live Trail gain (level×fade×play) applied in Process
    // so soft-replace fades mute grains without a hard cut.
    g.amp     = 1.f;
}

void SwarmEngine::Process(float* out_l, float* out_r, size_t size)
{
    const CaptureEngine::SwarmTrailView* views = CaptureEngine::SwarmViews();

    const float scan_u = Clampf(params_.scan, 0.f, 1.f);
    // Scan=0 freeze; Scan=1 ≈ one full buffer pass every ~1.5 s (per active trail).
    const float scan_rate = scan_u * scan_u * (1.f / 1.5f);

    const float atmo  = BipolarNorm(params_.atmosphere, -1.f, 1.f);
    const float blur  = atmo < 0.f ? -atmo : 0.f;
    const float rad   = atmo > 0.f ? atmo : 0.f;

    // Radiation: slower BBD slew + longer sample-hold.
    const float slew_coeff = Lerp(1.f, 0.02f, rad);
    const int   hold_period = 1 + static_cast<int>(rad * 12.f + 0.5f);

    const float dur_s = GrainDurationSec();
    const float spawn_interval = (dur_s * sample_rate_) * kSpawnDuty;

    size_t rr = 0;
    for(size_t n = 0; n < size; ++n)
    {
        // Advance scan heads (freeze when scan≈0).
        for(size_t t = 0; t < kTrailCount; ++t)
        {
            const size_t len = views[t].length;
            if(len < 2)
                continue;
            const size_t play = CaptureEngine::LoopPlayLength(len);
            const float  play_f = static_cast<float>(play);
            if(scan_rate > 1e-6f)
            {
                scan_pos_[t] += scan_rate * play_f * sample_rate_inv_;
                while(scan_pos_[t] >= play_f)
                    scan_pos_[t] -= play_f;
            }
            else if(scan_pos_[t] >= play_f)
                scan_pos_[t] = 0.f;
        }

        spawn_phase_ += 1.f;
        if(spawn_phase_ >= spawn_interval)
        {
            spawn_phase_ -= spawn_interval;
            // Round-robin among playable trails.
            for(size_t tries = 0; tries < kTrailCount; ++tries)
            {
                const size_t t = (rr + tries) % kTrailCount;
                if(views[t].length >= 32 && views[t].gain > 1e-4f)
                {
                    SpawnGrain(t, views[t].length, views[t].gain);
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

            const size_t len = views[g.trail].length;
            const float  tg  = views[g.trail].gain;
            if(len < 2 || tg < 1e-4f)
            {
                g.active = false;
                continue;
            }

            float sample = ReadInterp(g.trail, g.pos, len);
            // Radiation lo-fi: quantize read position steps slightly.
            if(rad > 0.001f)
            {
                const float q = 1.f + rad * 7.f;
                const float qp
                    = static_cast<float>(static_cast<int>(g.pos / q)) * q;
                sample = ReadInterp(g.trail, qp, len);
            }

            const float env = WindowEnv(g.age, blur);
            const float a   = g.amp * env * tg;
            mix_l += sample * a * g.pan_l;
            mix_r += sample * a * g.pan_r;
            ++n_on;

            g.pos += g.incr;
            const size_t play = CaptureEngine::LoopPlayLength(len);
            const float  play_f = static_cast<float>(play);
            while(g.pos >= play_f)
                g.pos -= play_f;
            while(g.pos < 0.f)
                g.pos += play_f;

            g.age += g.age_inc;
            if(g.age >= 1.f)
                g.active = false;
        }

        if(n_on > 1)
        {
            const float inv = 1.f / std::sqrt(static_cast<float>(n_on));
            mix_l *= inv;
            mix_r *= inv;
        }

        // Radiation BBD slew on the summed cloud.
        if(rad > 0.001f)
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
                hold_l_     = mix_l;
                hold_r_     = mix_r;
                hold_left_  = hold_period;
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
