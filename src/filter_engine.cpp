#include "filter_engine.h"

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

// SetFreq does sinf/powf — only refresh every N samples when Feedback drives
// audio-rate cutoff FM (ARCHITECTURE Block 10).
constexpr size_t kFbFreqPeriod = 4;
} // namespace

void FilterEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    params_          = FilterParamValues{};
    prev_l_          = 0.f;
    prev_r_          = 0.f;

    svf_l_.Init(sample_rate_);
    svf_r_.Init(sample_rate_);
    svf_l_.SetFreq(CutoffHz());
    svf_r_.SetFreq(CutoffHz());
    svf_l_.SetRes(0.2f);
    svf_r_.SetRes(0.2f);
    svf_l_.SetDrive(0.5f);
    svf_r_.SetDrive(0.5f);
}

void FilterEngine::SyncFromUi(const FilterParamValues& params)
{
    params_ = params;
    const float res = Clampf(params_.resonance, 0.f, 0.95f);
    svf_l_.SetRes(res);
    svf_r_.SetRes(res);
    const float drv = Lerp(0.4f, 0.9f, Clampf(params_.feedback, 0.f, 1.f));
    svf_l_.SetDrive(drv);
    svf_r_.SetDrive(drv);
}

int FilterEngine::Destination() const
{
    const int d = static_cast<int>(params_.destination + 0.5f);
    if(d < kFilterDestOff)
        return kFilterDestOff;
    if(d > kFilterDestReverb)
        return kFilterDestReverb;
    return d;
}

bool FilterEngine::IsBypassed() const
{
    // Wide-open LP ≈ dry — skip the stereo Svf bank (CPU).
    return params_.cutoff >= 0.98f && params_.resonance <= 0.05f
           && params_.feedback <= 0.02f;
}

float FilterEngine::CutoffHz() const
{
    const float u = Clampf(params_.cutoff, 0.f, 1.f);
    return 80.f * std::pow(200.f, u);
}

void FilterEngine::Process(float* io_l, float* io_r, size_t size)
{
    if(IsBypassed())
        return;

    const float base_hz  = CutoffHz();
    const float fmax     = sample_rate_ * 0.45f;
    const float fb       = Clampf(params_.feedback, 0.f, 1.f);
    const float fb_depth = fb * 0.85f;
    const bool  fm       = fb_depth > 0.02f;

    if(!fm)
    {
        const float hz = Clampf(base_hz, 40.f, fmax);
        svf_l_.SetFreq(hz);
        svf_r_.SetFreq(hz);
    }

    for(size_t n = 0; n < size; ++n)
    {
        if(fm && (n % kFbFreqPeriod) == 0)
        {
            const float hz_l
                = Clampf(base_hz * (1.f + prev_l_ * fb_depth), 40.f, fmax);
            const float hz_r
                = Clampf(base_hz * (1.f + prev_r_ * fb_depth), 40.f, fmax);
            svf_l_.SetFreq(hz_l);
            svf_r_.SetFreq(hz_r);
        }

        svf_l_.Process(io_l[n]);
        svf_r_.Process(io_r[n]);
        const float out_l = svf_l_.Low();
        const float out_r = svf_r_.Low();
        io_l[n]           = out_l;
        io_r[n]           = out_r;
        prev_l_           = out_l;
        prev_r_           = out_r;
    }
}

void FilterEngine::ProcessMono(float* io, size_t size)
{
    if(IsBypassed())
        return;

    const float base_hz  = CutoffHz();
    const float fmax     = sample_rate_ * 0.45f;
    const float fb       = Clampf(params_.feedback, 0.f, 1.f);
    const float fb_depth = fb * 0.85f;
    const bool  fm       = fb_depth > 0.02f;

    if(!fm)
        svf_l_.SetFreq(Clampf(base_hz, 40.f, fmax));

    for(size_t n = 0; n < size; ++n)
    {
        if(fm && (n % kFbFreqPeriod) == 0)
        {
            const float hz
                = Clampf(base_hz * (1.f + prev_l_ * fb_depth), 40.f, fmax);
            svf_l_.SetFreq(hz);
        }
        svf_l_.Process(io[n]);
        const float out = svf_l_.Low();
        io[n]           = out;
        prev_l_         = out;
        prev_r_         = out;
    }
}

} // namespace perseids
