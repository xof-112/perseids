#include "spectra_engine.h"

#include "daisy.h"

extern "C" {
#include "arm_math.h"
}

#include <cmath>
#include <cstring>

namespace perseids
{

namespace
{
constexpr float kTwoPi = 6.28318530718f;

arm_rfft_fast_instance_f32 g_rfft;

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

// Hann coherent gain ≈ 0.5 → unit sine peak mag ≈ N/4.
// Extra makeup so quiet Trail taps still speak through the oscillator bank.
constexpr float MagToAmp()
{
    return (4.f / static_cast<float>(SpectraEngine::kFftSize)) * 2.5f;
}
} // namespace

float SpectraEngine::FastSin(float phase01)
{
    // Parabolic sine approx, phase in [0,1). Good enough for additive bank.
    float x = phase01;
    x = x - static_cast<float>(static_cast<int>(x)); // wrap [0,1)
    if(x < 0.f)
        x += 1.f;
    x = (x < 0.5f) ? (4.f * x - 1.f) : (3.f - 4.f * x);
    return x * (1.f - 0.25f * x * x);
}

void SpectraEngine::Init(float sample_rate)
{
    sample_rate_     = sample_rate > 1.f ? sample_rate : 48000.f;
    sample_rate_inv_ = 1.f / sample_rate_;
    bin_hz_          = sample_rate_ / static_cast<float>(kFftSize);
    params_          = SpectraParamValues{};

    input_write_.store(0, std::memory_order_relaxed);
    input_read_.store(0, std::memory_order_relaxed);
    analysis_count_ = 0;
    pending_count_  = 0;
    prev_count_     = 0;
    targets_seq_.store(0, std::memory_order_relaxed);
    targets_seen_      = 0;
    active_partials_   = 0;
    waveshape_morph_   = 0.f;
    fold_gain_         = 1.f;

    std::memset(input_ring_, 0, sizeof(input_ring_));
    std::memset(fft_time_, 0, sizeof(fft_time_));
    std::memset(fft_freq_, 0, sizeof(fft_freq_));
    std::memset(magnitudes_, 0, sizeof(magnitudes_));
    std::memset(analysis_targets_, 0, sizeof(analysis_targets_));
    std::memset(pending_targets_, 0, sizeof(pending_targets_));
    std::memset(prev_targets_, 0, sizeof(prev_targets_));

    BuildWindow();
    arm_rfft_fast_init_f32(&g_rfft, static_cast<uint16_t>(kFftSize));

    folder_.Init();
    folder_.SetGain(1.f);
    for(size_t i = 0; i < kMaxPartials; ++i)
    {
        phase_[i]       = static_cast<float>(i) * 0.017f;
        osc_amp_[i]     = 0.f;
        osc_freq_[i]    = 110.f;
        target_amp_[i]  = 0.f;
        target_freq_[i] = 110.f;
        phase_inc_[i]   = 110.f * sample_rate_inv_;
    }
}

void SpectraEngine::BuildWindow()
{
    const float denom = static_cast<float>(kFftSize - 1);
    for(size_t i = 0; i < kFftSize; ++i)
    {
        const float t = static_cast<float>(i) / denom;
        window_[i]    = 0.5f * (1.f - std::cos(kTwoPi * t));
    }
}

void SpectraEngine::SyncFromUi(const SpectraParamValues& params)
{
    params_ = params;
}

float SpectraEngine::PitchRatio() const
{
    return std::pow(2.f, BipolarNorm(params_.pitch_spectra, -1.f, 1.f));
}

void SpectraEngine::PushInput(const float* samples, size_t size)
{
    uint32_t w = input_write_.load(std::memory_order_relaxed);
    for(size_t i = 0; i < size; ++i)
    {
        input_ring_[w & (kInputRing - 1)] = samples[i];
        ++w;
    }
    input_write_.store(w, std::memory_order_release);
}

void SpectraEngine::ApplyUmbraAurora(float* mags, size_t bins, float f0_hz) const
{
    const float macro = BipolarNorm(params_.umbra_aurora, -1.f, 1.f);
    if(std::fabs(macro) < 0.001f)
        return;

    if(macro < 0.f)
    {
        const float depth = -macro;
        float       peak  = 1e-6f;
        for(size_t i = 1; i < bins; ++i)
            if(mags[i] > peak)
                peak = mags[i];
        const float inv_peak = 1.f / peak;
        for(size_t i = 1; i < bins; ++i)
        {
            const float hz   = static_cast<float>(i) * bin_hz_;
            const float low  = Clampf(1.f - hz / 2000.f, 0.f, 1.f);
            const float loud = mags[i] * inv_peak;
            mags[i] *= (1.f - depth * low * loud);
            mags[i] += depth * (1.f - loud) * 0.35f * peak * 0.15f;
        }
    }
    else
    {
        const float depth = macro;
        const float f0    = f0_hz > 40.f ? f0_hz : 110.f;
        const float f1    = 500.f * (f0 / 110.f);
        const float f2    = 1500.f * (f0 / 110.f);
        const float f3    = 2500.f * (f0 / 110.f);
        for(size_t i = 1; i < bins; ++i)
        {
            const float hz = static_cast<float>(i) * bin_hz_;
            const float d1 = (hz - f1) / (f1 * 0.35f + 30.f);
            const float d2 = (hz - f2) / (f2 * 0.35f + 30.f);
            const float d3 = (hz - f3) / (f3 * 0.35f + 30.f);
            const float env
                = 0.35f
                  + depth
                        * (0.9f * std::exp(-d1 * d1) + 0.7f * std::exp(-d2 * d2)
                           + 0.5f * std::exp(-d3 * d3));
            mags[i] *= env;
        }
    }
}

void SpectraEngine::PickPartials(const float* mags, size_t bins)
{
    const size_t want = static_cast<size_t>(
        Clampf(params_.partials + 0.5f,
               static_cast<float>(kMinPartials),
               static_cast<float>(kMaxPartials)));

    struct Peak
    {
        float mag;
        float freq;
    };
    Peak   peaks[64];
    size_t npeaks = 0;

    const size_t max_bin = static_cast<size_t>(
        Clampf(12000.f / bin_hz_, 2.f, static_cast<float>(bins - 2)));

    // Absolute silence floor — below this the frame is treated as quiet.
    // Unit sine ≈ N/4 ≈ 128; keep this low so soft Trails still resynthesize.
    constexpr float kSilenceMag = 0.08f;

    float max_mag = 0.f;
    for(size_t i = 2; i < max_bin; ++i)
        if(mags[i] > max_mag)
            max_mag = mags[i];

    if(max_mag < kSilenceMag)
    {
        analysis_count_ = prev_count_;
        for(size_t i = 0; i < kMaxPartials; ++i)
        {
            analysis_targets_[i].freq_hz
                = (i < prev_count_) ? prev_targets_[i].freq_hz : 110.f;
            analysis_targets_[i].amp = 0.f;
        }
        return;
    }

    // Relative floor: ignore tiny side-lobes / noise under the loudest peak.
    const float floor_mag = max_mag * 0.02f;

    for(size_t i = 2; i < max_bin; ++i)
    {
        if(mags[i] >= mags[i - 1] && mags[i] > mags[i + 1]
           && mags[i] > floor_mag)
        {
            const float a     = mags[i - 1];
            const float b     = mags[i];
            const float c     = mags[i + 1];
            const float denom = (a - 2.f * b + c);
            float       delta = 0.f;
            if(std::fabs(denom) > 1e-12f)
                delta = 0.5f * (a - c) / denom;
            delta = Clampf(delta, -0.5f, 0.5f);
            if(npeaks < 64)
            {
                peaks[npeaks].mag = b;
                peaks[npeaks].freq
                    = (static_cast<float>(i) + delta) * bin_hz_;
                ++npeaks;
            }
        }
    }

    // Partial-select top `want` by magnitude (selection sort into front).
    for(size_t i = 0; i < npeaks && i < want; ++i)
    {
        size_t best = i;
        for(size_t j = i + 1; j < npeaks; ++j)
            if(peaks[j].mag > peaks[best].mag)
                best = j;
        if(best != i)
        {
            const Peak tmp = peaks[i];
            peaks[i]       = peaks[best];
            peaks[best]    = tmp;
        }
    }

    const size_t take = npeaks < want ? npeaks : want;
    const float  pitch = PitchRatio();
    const float  scale = MagToAmp();

    // Continuity: assign peaks to previous oscillator slots by nearest freq
    // so one partial does not teleport between oscillators every hop.
    bool claimed[64];
    for(size_t i = 0; i < 64; ++i)
        claimed[i] = false;

    PartialTarget out[kMaxPartials];
    for(size_t i = 0; i < kMaxPartials; ++i)
    {
        out[i].freq_hz = 110.f;
        out[i].amp     = 0.f;
    }

    const float max_abs_hz = 2.5f * bin_hz_; // ~2.5 bins

    for(size_t s = 0; s < prev_count_ && s < kMaxPartials; ++s)
    {
        if(prev_targets_[s].amp < 1e-4f)
            continue;

        const float pref = prev_targets_[s].freq_hz;
        const float max_d
            = max_abs_hz > 0.08f * pref ? max_abs_hz : (0.08f * pref);

        size_t best_p = take;
        float  best_d = max_d;
        for(size_t p = 0; p < take; ++p)
        {
            if(claimed[p])
                continue;
            const float f = peaks[p].freq * pitch;
            const float d = std::fabs(f - pref);
            if(d < best_d)
            {
                best_d = d;
                best_p = p;
            }
        }
        if(best_p < take)
        {
            claimed[best_p]   = true;
            out[s].freq_hz    = Clampf(peaks[best_p].freq * pitch, 20.f, 16000.f);
            out[s].amp        = peaks[best_p].mag * scale;
        }
        else
        {
            // Keep frequency, fade amp — avoids a chirp into a new peak.
            out[s].freq_hz = pref;
            out[s].amp     = 0.f;
        }
    }

    // Remaining peaks → free slots (quiet or unused).
    for(size_t p = 0; p < take; ++p)
    {
        if(claimed[p])
            continue;
        size_t slot = kMaxPartials;
        for(size_t s = 0; s < want; ++s)
        {
            if(out[s].amp < 1e-5f)
            {
                slot = s;
                break;
            }
        }
        if(slot >= kMaxPartials)
            break;
        out[slot].freq_hz
            = Clampf(peaks[p].freq * pitch, 20.f, 16000.f);
        out[slot].amp = peaks[p].mag * scale;
    }

    analysis_count_ = want;
    for(size_t i = 0; i < kMaxPartials; ++i)
        analysis_targets_[i] = out[i];
}

void SpectraEngine::PublishTargets()
{
    // Seqlock: odd = write in progress, even = stable snapshot for audio.
    const uint32_t seq = targets_seq_.load(std::memory_order_relaxed);
    targets_seq_.store(seq + 1, std::memory_order_release); // odd

    pending_count_ = analysis_count_;
    for(size_t i = 0; i < kMaxPartials; ++i)
        pending_targets_[i] = analysis_targets_[i];

    targets_seq_.store(seq + 2, std::memory_order_release); // even

    // Continuity snapshot after publish (analysis thread only).
    prev_count_ = analysis_count_;
    for(size_t i = 0; i < kMaxPartials; ++i)
        prev_targets_[i] = analysis_targets_[i];
}

void SpectraEngine::ProcessAnalysis()
{
    const uint32_t w         = input_write_.load(std::memory_order_acquire);
    uint32_t       r         = input_read_.load(std::memory_order_relaxed);
    uint32_t       available = w - r;

    // Drop backlog so UI never waits on a queue of FFTs (mux starvation).
    while(available >= kHopSize * 2)
    {
        r += kHopSize;
        available -= kHopSize;
    }
    if(available < kHopSize)
    {
        input_read_.store(r, std::memory_order_relaxed);
        return;
    }

    r += kHopSize;
    if(w < kFftSize || r < kFftSize)
    {
        input_read_.store(r, std::memory_order_relaxed);
        return;
    }

    const uint32_t start = r - kFftSize;
    for(size_t i = 0; i < kFftSize; ++i)
        fft_time_[i] = input_ring_[(start + static_cast<uint32_t>(i))
                                   & (kInputRing - 1)];
    input_read_.store(r, std::memory_order_relaxed);

    arm_mult_f32(fft_time_, window_, fft_time_, static_cast<uint32_t>(kFftSize));
    arm_rfft_fast_f32(&g_rfft, fft_time_, fft_freq_, 0);

    magnitudes_[0] = std::fabs(fft_freq_[0]);
    arm_cmplx_mag_f32(&fft_freq_[2],
                      &magnitudes_[1],
                      static_cast<uint32_t>(kBinCount - 1));

    float        f0     = 110.f;
    float        best_m = 0.f;
    const size_t f0_max = static_cast<size_t>(
        Clampf(1000.f / bin_hz_, 2.f, static_cast<float>(kBinCount - 1)));
    for(size_t i = 2; i < f0_max; ++i)
    {
        if(magnitudes_[i] > best_m)
        {
            best_m = magnitudes_[i];
            f0     = static_cast<float>(i) * bin_hz_;
        }
    }

    ApplyUmbraAurora(magnitudes_, kBinCount, f0);
    PickPartials(magnitudes_, kBinCount);
    PublishTargets();
}

void SpectraEngine::ConsumeTargets()
{
    const uint32_t seq = targets_seq_.load(std::memory_order_acquire);
    if((seq & 1u) != 0u)
        return;
    if(seq == targets_seen_)
        return;
    targets_seen_ = seq;

    active_partials_ = pending_count_;
    if(active_partials_ > kMaxPartials)
        active_partials_ = kMaxPartials;

    const float drift        = Clampf(params_.ensemble, 0.f, 1.f);
    const float detune_ratio = 1.f + drift * 0.008f;

    for(size_t i = 0; i < kMaxPartials; ++i)
    {
        float freq = pending_targets_[i].freq_hz;
        float amp  = (i < active_partials_) ? pending_targets_[i].amp : 0.f;
        if(drift > 0.001f && i < active_partials_ && amp > 1e-5f)
        {
            if((i & 1u) == 0u)
                freq *= detune_ratio;
            else
                freq /= detune_ratio;
        }
        target_freq_[i] = Clampf(freq, 20.f, 16000.f);
        target_amp_[i]  = amp;
    }
}

void SpectraEngine::Process(float* out_l, float* out_r, size_t size)
{
    ConsumeTargets();

    // ENS=0 → faster tracking; ENS=1 → slower (architecture Ensemble/Drift).
    // Keep freq a bit softer than amp so residual mismatches don't chirp.
    const float drift     = Clampf(params_.ensemble, 0.f, 1.f);
    const float slew_amp  = Lerp(0.35f, 0.04f, drift);
    const float slew_freq = Lerp(0.22f, 0.03f, drift);
    waveshape_morph_      = BipolarNorm(params_.waveshape, -1.f, 1.f);
    fold_gain_            = 1.f + std::fabs(waveshape_morph_) * 2.5f;
    folder_.SetGain(fold_gain_);

    const size_t n_act = active_partials_;
    for(size_t i = 0; i < n_act; ++i)
    {
        osc_freq_[i] += slew_freq * (target_freq_[i] - osc_freq_[i]);
        osc_amp_[i] += slew_amp * (target_amp_[i] - osc_amp_[i]);
        phase_inc_[i] = osc_freq_[i] * sample_rate_inv_;
    }
    for(size_t i = n_act; i < kMaxPartials; ++i)
    {
        osc_amp_[i] += slew_amp * (0.f - osc_amp_[i]);
        target_amp_[i] = 0.f;
    }

    const float morph    = waveshape_morph_;
    const bool  do_saw   = morph < -0.02f;
    const bool  do_fold  = morph > 0.02f;
    const float saw_amt  = do_saw ? -morph : 0.f;
    const float fold_amt = do_fold ? morph : 0.f;

    for(size_t n = 0; n < size; ++n)
    {
        float mix = 0.f;
        for(size_t i = 0; i < n_act; ++i)
        {
            if(osc_amp_[i] < 1e-4f)
                continue;

            phase_[i] += phase_inc_[i];
            if(phase_[i] >= 1.f)
                phase_[i] -= 1.f;

            const float sine = FastSin(phase_[i]);
            float       s    = sine;
            if(do_saw)
            {
                const float saw = 2.f * phase_[i] - 1.f;
                s               = Lerp(sine, saw, saw_amt);
            }
            else if(do_fold)
            {
                s = Lerp(sine, folder_.Process(sine), fold_amt);
            }
            mix += s * osc_amp_[i];
        }

        mix      = Clampf(mix, -1.2f, 1.2f);
        out_l[n] = mix;
        out_r[n] = mix;
    }
}

} // namespace perseids
