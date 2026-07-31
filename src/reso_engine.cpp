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

// Decay maps to a real ring time. A bandpass at fc with damping d decays as
// exp(-pi*d*fc*t), so T60 = ln(1000)/(pi*fc*d) and d = 2.199/(fc*T60).
constexpr float kT60Min  = 0.08f;
constexpr float kT60Max  = 8.f;
constexpr float kT60Coef = 2.199f;

// Floor on damping ≈ Q 1250. Purely numerical headroom — the Svf can't
// self-oscillate (its cubic drive term only ever removes energy).
constexpr float kDampMin = 0.0008f;
constexpr float kDampMax = 1.6f;

// Damping=1 shortens a mode's ring by (f/root)^-kDampExpMax, so the 8th
// harmonic dies roughly an order of magnitude before the fundamental.
constexpr float kDampExpMax = 1.2f;

// Mode weights 1/sqrt(n): the root leads, upper modes fill in the body.
// Normalising by the vector norm (not the plain sum) is the honest choice —
// the modes sit on different frequencies, so they add incoherently.
constexpr float kModeWeight[8] = {1.f,
                                  0.70711f,
                                  0.57735f,
                                  0.5f,
                                  0.44721f,
                                  0.40825f,
                                  0.37796f,
                                  0.35355f};

// Fundamental stays centred, upper modes fan out alternately. Keeps the pitch
// anchored while the partials open up the image.
constexpr float kModePan[8]
    = {0.f, 0.143f, -0.286f, 0.429f, -0.571f, 0.714f, -0.857f, 1.f};

// Leaves a little room under the soft clip after the level jump that dropping
// the old 1/8 normalisation brings.
constexpr float kBankTrim = 0.8f;
} // namespace

void ResonatorEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    params_          = ResoParamValues{};
    scale_           = 0.f;
    intonation_      = 0.f;

    for(size_t i = 0; i < kNumModes; ++i)
    {
        modes_[i].Init(sample_rate_);
        modes_[i].SetRes(0.5f);
        // Drive is the bank's safety net, not a tone control: the Svf subtracts
        // drive*band^3, so a mode settles near sqrt(damp/drive). Without it the
        // Q values this engine now reaches would ring far too hot.
        modes_[i].SetDrive(0.5f);
        freq_hz_[i] = 110.f * kHarmonic[i];
        modes_[i].SetFreq(freq_hz_[i]);
    }

    tuned_pitch_      = 1e9f; // force the first Update*() pass
    gained_spread_    = 1e9f;
    UpdateTuning();
    UpdateGains();
}

void ResonatorEngine::SyncFromUi(const ResoParamValues& params,
                                 float                  scale,
                                 float                  intonation)
{
    params_     = params;
    scale_      = scale;
    intonation_ = intonation;

    if(params_.pitch != tuned_pitch_ || params_.decay != tuned_decay_
       || params_.damping != tuned_damping_
       || params_.quantized != tuned_quantized_ || scale != tuned_scale_
       || intonation != tuned_intonation_)
    {
        UpdateTuning();
    }
    if(params_.spread != gained_spread_)
        UpdateGains();
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

    // Decay is a ring time, not a raw Q. Svf::SetRes maps res through
    // damp = 2*(1 - res^0.25), whose fourth root squeezes every musically
    // useful Q into the last few percent of the knob — so derive the damping
    // the mode actually needs and invert that curve instead.
    const float t60
        = kT60Min * std::pow(kT60Max / kT60Min, Clampf(params_.decay, 0.f, 1.f));
    const float damp_exp = Clampf(params_.damping, 0.f, 1.f) * kDampExpMax;

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

        hz          = Clampf(hz, 40.f, fmax);
        freq_hz_[i] = hz;

        // Upper modes ring shorter as Damping opens up — that is the whole
        // wood/metal axis: exponent 0 leaves the bank glassy and even.
        float t60_i = t60;
        if(damp_exp > 0.f && hz > root)
            t60_i = t60 * std::pow(hz / root, -damp_exp);

        const float damp = Clampf(kT60Coef / (hz * t60_i), kDampMin, kDampMax);
        const float r    = 1.f - 0.5f * damp;
        const float res  = Clampf(r * r * r * r, 0.f, 0.9995f);

        // SetFreq folds res_ into damp_ and SetRes folds freq_ in — freq first.
        modes_[i].SetFreq(hz);
        modes_[i].SetRes(res);
    }

    tuned_pitch_      = params_.pitch;
    tuned_decay_      = params_.decay;
    tuned_damping_    = params_.damping;
    tuned_quantized_  = params_.quantized;
    tuned_scale_      = scale_;
    tuned_intonation_ = intonation_;
}

void ResonatorEngine::UpdateGains()
{
    const float spread = Clampf(params_.spread, 0.f, 1.f);

    float norm = 0.f;
    for(size_t i = 0; i < kNumModes; ++i)
        norm += kModeWeight[i] * kModeWeight[i];
    norm = kBankTrim / std::sqrt(norm);

    for(size_t i = 0; i < kNumModes; ++i)
    {
        // Equal power, scaled so a centred mode reads unity on both sides.
        const float theta = (kModePan[i] * spread + 1.f) * 0.7853982f;
        const float g     = kModeWeight[i] * norm * 1.4142136f;
        gain_l_[i]        = g * std::cos(theta);
        gain_r_[i]        = g * std::sin(theta);
    }

    gained_spread_ = params_.spread;
}

void ResonatorEngine::Process(float* io_l, float* io_r, size_t size)
{
    const float mix = Clampf(params_.mix, 0.f, 1.f);
    if(mix < 0.001f)
        return;

    // Equal-power dry/wet so Mix=50% stays balanced.
    const float wet_g = std::sin(mix * 1.5707964f);
    const float dry_g = std::cos(mix * 1.5707964f);

    for(size_t n = 0; n < size; ++n)
    {
        const float dry_l = io_l[n];
        const float dry_r = io_r[n];
        const float mid   = 0.5f * (dry_l + dry_r);

        float wl = 0.f;
        float wr = 0.f;
        for(size_t i = 0; i < kNumModes; ++i)
        {
            modes_[i].Process(mid);
            const float b = modes_[i].Band();
            wl += b * gain_l_[i];
            wr += b * gain_r_[i];
        }

        // Soft limit the ringing bank before the wet mix (cheap clip).
        const float al = std::fabs(wl);
        const float ar = std::fabs(wr);
        wl = wl * (27.f + al * al) / (27.f + 9.f * al * al);
        wr = wr * (27.f + ar * ar) / (27.f + 9.f * ar * ar);

        io_l[n] = dry_l * dry_g + wl * wet_g;
        io_r[n] = dry_r * dry_g + wr * wet_g;
    }
}

} // namespace perseids
