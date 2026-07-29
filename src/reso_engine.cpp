#include "reso_engine.h"

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

// Semitone offsets from C for the three Settings scales (8 degrees).
constexpr int kMajorSemi[8]  = {0, 2, 4, 5, 7, 9, 11, 12};
constexpr int kMinorSemi[8]  = {0, 2, 3, 5, 7, 8, 10, 12};
constexpr int kPentaSemi[8]  = {0, 2, 4, 7, 9, 12, 14, 16};

// Just-intonation ratios relative to the root (same degree order as above).
constexpr float kJustMajor[8]
    = {1.f, 9.f / 8.f, 5.f / 4.f, 4.f / 3.f, 3.f / 2.f, 5.f / 3.f, 15.f / 8.f, 2.f};
constexpr float kJustMinor[8]
    = {1.f, 9.f / 8.f, 6.f / 5.f, 4.f / 3.f, 3.f / 2.f, 8.f / 5.f, 9.f / 5.f, 2.f};
constexpr float kJustPenta[8]
    = {1.f, 9.f / 8.f, 5.f / 4.f, 3.f / 2.f, 5.f / 3.f, 2.f, 9.f / 4.f, 5.f / 2.f};

// Unquantized: odd harmonics of the root (spectral body, not a scale).
constexpr float kHarmonic[8] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
} // namespace

void ResonatorEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    params_          = ResoParamValues{};
    scale_           = 0.f;
    intonation_      = 0.f;
    q_               = 0.5f;

    for(size_t i = 0; i < kNumModes; ++i)
    {
        modes_[i].Init(sample_rate_);
        modes_[i].SetRes(0.5f);
        modes_[i].SetDrive(0.5f);
        freq_hz_[i] = 110.f * kHarmonic[i];
        modes_[i].SetFreq(freq_hz_[i]);
    }
    UpdateTuning();
}

void ResonatorEngine::SyncFromUi(const ResoParamValues& params,
                                 float                  scale,
                                 float                  intonation)
{
    params_     = params;
    scale_      = scale;
    intonation_ = intonation;
    UpdateTuning();
}

float ResonatorEngine::RootHz() const
{
    // C2 ≈ 65.41 Hz, shifted ±1 octave by Pitch.
    const float pitch
        = std::pow(2.f, BipolarNorm(params_.pitch, -1.f, 1.f));
    return 65.406f * pitch;
}

void ResonatorEngine::UpdateTuning()
{
    const float root = RootHz();
    const bool  quant = params_.quantized >= 0.5f;
    const bool  just  = intonation_ >= 0.5f;
    const int   scale = static_cast<int>(Clampf(scale_, 0.f, 2.f) + 0.5f);

    const int*   semis = kMajorSemi;
    const float* justs = kJustMajor;
    if(scale == 1)
    {
        semis = kMinorSemi;
        justs = kJustMinor;
    }
    else if(scale == 2)
    {
        semis = kPentaSemi;
        justs = kJustPenta;
    }

    // Decay 0 → short/dull (low Q); 1 → long ring (high Q, stay <1 for Svf).
    q_ = Lerp(0.15f, 0.92f, Clampf(params_.decay, 0.f, 1.f));

    const float fmax = sample_rate_ * 0.45f;
    for(size_t i = 0; i < kNumModes; ++i)
    {
        float hz;
        if(!quant)
            hz = root * kHarmonic[i];
        else if(just)
            hz = root * justs[i];
        else
            hz = root * std::pow(2.f, static_cast<float>(semis[i]) / 12.f);

        hz = Clampf(hz, 40.f, fmax);
        freq_hz_[i] = hz;
        modes_[i].SetFreq(hz);
        modes_[i].SetRes(q_);
    }
}

void ResonatorEngine::Process(float* io_l, float* io_r, size_t size)
{
    const float mix = Clampf(params_.mix, 0.f, 1.f);
    if(mix < 0.001f)
        return;

    // Equal-power dry/wet so Mix=50% stays balanced.
    const float wet_g = std::sin(mix * 1.5707964f);
    const float dry_g = std::cos(mix * 1.5707964f);
    const float inv_n = 1.f / static_cast<float>(kNumModes);

    for(size_t n = 0; n < size; ++n)
    {
        const float dry_l = io_l[n];
        const float dry_r = io_r[n];
        const float mid   = 0.5f * (dry_l + dry_r);

        float sum = 0.f;
        for(size_t i = 0; i < kNumModes; ++i)
        {
            modes_[i].Process(mid);
            sum += modes_[i].Band();
        }
        sum *= inv_n;

        // Soft limit the ringing bank before the wet mix.
        sum = std::tanh(sum * 1.4f);

        io_l[n] = dry_l * dry_g + sum * wet_g;
        io_r[n] = dry_r * dry_g + sum * wet_g;
    }
}

} // namespace perseids
