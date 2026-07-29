#include "reverb_engine.h"

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

// Cheap soft-clip (Friction) — avoids std::tanh per sample.
inline float SoftClip(float x)
{
    const float a = std::fabs(x);
    return x * (27.f + a * a) / (27.f + 9.f * a * a);
}
} // namespace

void ReverbEngine::Init(float sample_rate, daisysp::ReverbSc& verb_sc)
{
    sample_rate_   = sample_rate > 1.f ? sample_rate : 48000.f;
    params_        = ReverbParamValues{};
    friction_fb_l_ = 0.f;
    friction_fb_r_ = 0.f;
    hold_l_        = 0.f;
    hold_r_        = 0.f;
    verb_          = &verb_sc;

    // Half-rate tank: ~50% ReverbSc CPU; delay times stay correct in seconds.
    verb_->Init(sample_rate_ * 0.5f);
    verb_->SetFeedback(0.85f);
    verb_->SetLpFreq(10000.f);

    chorus_.Init(sample_rate_);
    chorus_.SetLfoFreq(0.35f);
    chorus_.SetLfoDepth(0.35f);
    chorus_.SetDelay(0.5f);
    chorus_.SetFeedback(0.15f);
    chorus_.SetPan(0.25f, 0.75f);
}

void ReverbEngine::SyncFromUi(const ReverbParamValues& params)
{
    params_ = params;
    if(verb_ == nullptr)
        return;

    const float dec = Clampf(params_.decay, 0.f, 1.f);
    verb_->SetFeedback(Lerp(0.55f, 0.97f, dec));

    const float damp = Clampf(params_.damping, 0.f, 1.f);
    verb_->SetLpFreq(Lerp(16000.f, 800.f, damp));

    const float ch         = BipolarNorm(params_.character, -1.f, 1.f);
    const float chorus_amt = ch < 0.f ? -ch : 0.f;
    chorus_.SetLfoFreq(Lerp(0.15f, 0.55f, chorus_amt));
    chorus_.SetLfoDepth(Lerp(0.05f, 0.55f, chorus_amt));
}

void ReverbEngine::Process(const float* in_l,
                           const float* in_r,
                           float*       wet_l,
                           float*       wet_r,
                           size_t       size)
{
    if(verb_ == nullptr)
        return;

    const float mix = Clampf(params_.mix, 0.f, 1.f);
    if(mix < 0.001f)
    {
        friction_fb_l_ = 0.f;
        friction_fb_r_ = 0.f;
        hold_l_        = 0.f;
        hold_r_        = 0.f;
        for(size_t n = 0; n < size; ++n)
        {
            wet_l[n] = 0.f;
            wet_r[n] = 0.f;
        }
        return;
    }

    const float ch           = BipolarNorm(params_.character, -1.f, 1.f);
    const float chorus_amt   = ch < 0.f ? -ch : 0.f;
    const float friction_amt = ch > 0.f ? ch : 0.f;
    const float drive        = Lerp(1.f, 3.2f, friction_amt);
    const float fb_gain      = friction_amt * 0.45f;
    const bool  do_chorus    = chorus_amt > 0.001f;
    const bool  do_friction  = friction_amt > 0.001f;

    for(size_t n = 0; n < size; ++n)
    {
        // Advance the tank on even samples only (half-rate).
        if((n & 1u) == 0u)
        {
            float s_l = in_l[n] + friction_fb_l_ * fb_gain;
            float s_r = in_r[n] + friction_fb_r_ * fb_gain;
            if(do_friction)
            {
                s_l = SoftClip(s_l * drive);
                s_r = SoftClip(s_r * drive);
            }

            float w_l = 0.f;
            float w_r = 0.f;
            verb_->Process(s_l, s_r, &w_l, &w_r);

            friction_fb_l_ = w_l;
            friction_fb_r_ = w_r;

            if(do_chorus)
            {
                const float mid = 0.5f * (w_l + w_r);
                chorus_.Process(mid);
                w_l = Lerp(w_l, chorus_.GetLeft(), chorus_amt);
                w_r = Lerp(w_r, chorus_.GetRight(), chorus_amt);
            }

            hold_l_ = w_l;
            hold_r_ = w_r;
        }

        wet_l[n] = hold_l_;
        wet_r[n] = hold_r_;
    }
}

} // namespace perseids
