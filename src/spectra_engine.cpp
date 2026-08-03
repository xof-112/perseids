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

// Hann coherent gain ≈ 0.5 → unit sine peak mag ≈ N/4 → amp ≈ 1.
// Keep makeup ≤ ~1.0: ×1.4+ brought level-proportional crackle back (~50% Trail).
constexpr float MagToAmp()
{
    return 4.f / static_cast<float>(SpectraEngine::kFftSize);
}

// Magnitudes scale with N, so absolute thresholds tuned at FFT 512 must too.
constexpr float kMagScale = static_cast<float>(SpectraEngine::kFftSize) / 512.f;

// A-weighting response (linear). Peaks near 2.5 kHz, drops steeply in the bass
// — a usable stand-in for the ear's sensitivity at moderate listening levels.
inline float AWeightLinear(float f_hz)
{
    const float f2 = f_hz * f_hz;
    const float num = 12194.f * 12194.f * f2 * f2;
    const float den = (f2 + 20.6f * 20.6f)
                      * std::sqrt((f2 + 107.7f * 107.7f)
                                  * (f2 + 737.9f * 737.9f))
                      * (f2 + 12194.f * 12194.f);
    return (den > 1e-30f) ? (num / den) : 0.f;
}

// Perceived-loudness weighting for one partial. At equal amplitude a high
// Trail outshines low ones, so partials above the reference are attenuated —
// never boosting the bass, which would only eat headroom. Partial strength
// (kLoudTilt) keeps a bright source bright; the clamp stops the top octaves
// from being buried.
inline float LoudnessWeight(float f_hz)
{
    constexpr float kLoudRefHz = 250.f;
    constexpr float kLoudTilt  = 0.6f; // 1.0 = full inverse A-weighting
    constexpr float kLoudFloor = 0.35f;

    const float ra = AWeightLinear(f_hz);
    if(ra <= 1e-20f)
        return 1.f;
    const float ratio = AWeightLinear(kLoudRefHz) / ra;
    if(ratio >= 1.f)
        return 1.f; // at or below the reference: leave as is
    const float w = std::pow(ratio, kLoudTilt);
    return w < kLoudFloor ? kLoudFloor : w;
}
} // namespace

// SDRAM: at FFT 2048 these four are 48 KB and would push DTCM past 95%.
// One SpectraEngine instance exists (g_spectra), same pattern as trail_buffer.
float DSY_SDRAM_BSS g_spec_window[SpectraEngine::kFftSize];
float DSY_SDRAM_BSS g_spec_mags[SpectraEngine::kBinCount];
float DSY_SDRAM_BSS g_spec_mag_smooth[SpectraEngine::kBinCount];
float DSY_SDRAM_BSS g_spec_input_ring[SpectraEngine::kInputRing];

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
    pitch_both_      = 0.f;
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

    std::memset(g_spec_input_ring, 0, sizeof(g_spec_input_ring));
    std::memset(fft_time_, 0, sizeof(fft_time_));
    std::memset(fft_freq_, 0, sizeof(fft_freq_));
    std::memset(g_spec_mags, 0, sizeof(g_spec_mags));
    std::memset(g_spec_mag_smooth, 0, sizeof(g_spec_mag_smooth));
    mag_smooth_valid_ = false;
    f0_smooth_hz_     = 110.f;
    f0_smooth_valid_  = false;
    poly_score_       = 0;
    polyphonic_       = false;
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
    slew_amp_  = 0.f;
    slew_freq_ = 0.f;
}

void SpectraEngine::BuildWindow()
{
    const float denom = static_cast<float>(kFftSize - 1);
    for(size_t i = 0; i < kFftSize; ++i)
    {
        const float t    = static_cast<float>(i) / denom;
        g_spec_window[i] = 0.5f * (1.f - std::cos(kTwoPi * t));
    }
}

void SpectraEngine::SyncFromUi(const SpectraParamValues& params, float pitch_both)
{
    params_     = params;
    pitch_both_ = pitch_both;
}

float SpectraEngine::PitchRatio() const
{
    // Pitch Both expands PSP's octave span from ±1 (PB=0) to ±2 (PB=1) —
    // it is not an extra pitch offset. Up to ±2 oct can alias.
    const float span = 1.f + Clampf(pitch_both_, 0.f, 1.f);
    return std::pow(2.f, BipolarNorm(params_.pitch_spectra, -1.f, 1.f) * span);
}

void SpectraEngine::PushInput(const float* samples, size_t size)
{
    uint32_t w = input_write_.load(std::memory_order_relaxed);
    for(size_t i = 0; i < size; ++i)
    {
        g_spec_input_ring[w & (kInputRing - 1)] = samples[i];
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
    // Unit sine ≈ N/4; keep this low so soft Trails still resynthesize.
    constexpr float kSilenceMag = 0.08f * kMagScale;

    // Bin 2 ≈ 47 Hz at FFT 2048 — below musical Trail content, and low enough
    // that fundamentals are resolved instead of being read as overtones.
    float max_mag = 0.f;
    for(size_t i = 2; i < max_bin; ++i)
        if(mags[i] > max_mag)
            max_mag = mags[i];

    if(max_mag < kSilenceMag)
    {
        mag_smooth_valid_ = false;
        f0_smooth_valid_  = false;
        analysis_count_   = prev_count_;
        for(size_t i = 0; i < kMaxPartials; ++i)
        {
            analysis_targets_[i].freq_hz
                = (i < prev_count_) ? prev_targets_[i].freq_hz : 110.f;
            analysis_targets_[i].amp = 0.f;
        }
        return;
    }

    // Relative floor ≈ −12 dB — above Hann sidelobes, keeps strong body.
    constexpr float kAbsPeakFloor = 1.f * kMagScale;
    float           floor_mag     = max_mag * 0.25f;
    if(floor_mag < kAbsPeakFloor)
        floor_mag = kAbsPeakFloor;

    for(size_t i = 2; i < max_bin; ++i)
    {
        const float a = mags[i - 1];
        const float b = mags[i];
        const float c = mags[i + 1];
        if(!(b > a && b > c && b > floor_mag))
            continue;

        const float denom = (a - 2.f * b + c);
        float       delta = 0.f;
        if(std::fabs(denom) > 1e-12f)
            delta = 0.5f * (a - c) / denom;
        delta = Clampf(delta, -0.5f, 0.5f);

        if(npeaks < 64)
        {
            peaks[npeaks].mag  = b;
            peaks[npeaks].freq = (static_cast<float>(i) + delta) * bin_hz_;
            ++npeaks;
        }
    }

    // Instant f0 = lowest strong peak; EMA across hops for a stable grid.
    constexpr float kF0Rel = 0.35f;
    float           f0_raw = 0.f;
    for(size_t i = 0; i < npeaks; ++i)
    {
        if(peaks[i].mag < max_mag * kF0Rel)
            continue;
        if(f0_raw <= 0.f || peaks[i].freq < f0_raw)
            f0_raw = peaks[i].freq;
    }
    if(f0_raw <= 0.f && npeaks > 0)
        f0_raw = peaks[0].freq;

    // Hysteresis: an anchor keeps its job while its peak is merely quieter
    // (kF0KeepRel), not only while it wins kF0Rel. On rich material the
    // fundamental drifts across a single threshold every few seconds, and
    // each crossing reshuffles the accept order below.
    constexpr float kF0KeepRel = 0.18f;
    float           f0_target  = f0_raw;
    if(f0_smooth_valid_)
    {
        float keep_d = 0.06f * f0_smooth_hz_;
        for(size_t i = 0; i < npeaks; ++i)
        {
            if(peaks[i].mag < max_mag * kF0KeepRel)
                continue;
            const float d = std::fabs(peaks[i].freq - f0_smooth_hz_);
            if(d < keep_d)
            {
                keep_d    = d;
                f0_target = peaks[i].freq;
            }
        }
    }

    float f0_hz = f0_target;
    if(f0_target > 40.f)
    {
        if(!f0_smooth_valid_)
        {
            f0_smooth_hz_    = f0_target;
            f0_smooth_valid_ = true;
        }
        else
        {
            const float rel = std::fabs(f0_target - f0_smooth_hz_)
                              / (f0_smooth_hz_ > 1.f ? f0_smooth_hz_ : 1.f);
            const float a   = (rel > 0.10f) ? 0.55f : 0.12f;
            f0_smooth_hz_ += a * (f0_target - f0_smooth_hz_);
        }
        f0_hz = f0_smooth_hz_;
    }

    // Multi-Trail roots: strong peaks that are not harmonics of a lower root.
    // ≥2 roots → true polyphony (hold several pitches), not one flipping f0.
    constexpr size_t kMaxRoots = 8;
    size_t           cand_idx[64];
    size_t           n_cand = 0;
    for(size_t i = 0; i < npeaks; ++i)
    {
        if(peaks[i].mag < max_mag * kF0Rel)
            continue;
        cand_idx[n_cand++] = i;
    }
    // Low frequency first — each new root is an independent voice.
    for(size_t i = 0; i < n_cand; ++i)
    {
        size_t best = i;
        for(size_t j = i + 1; j < n_cand; ++j)
            if(peaks[cand_idx[j]].freq < peaks[cand_idx[best]].freq)
                best = j;
        if(best != i)
        {
            const size_t tmp = cand_idx[i];
            cand_idx[i]      = cand_idx[best];
            cand_idx[best]   = tmp;
        }
    }
    auto is_harm_of = [](float freq, float root) -> bool {
        if(root < 40.f)
            return false;
        const float n       = freq / root;
        const float nearest = std::round(n);
        return nearest >= 1.f && std::fabs(n - nearest) <= 0.08f;
    };
    size_t root_idx[kMaxRoots];
    size_t n_roots = 0;
    for(size_t i = 0; i < n_cand && n_roots < kMaxRoots; ++i)
    {
        const float f     = peaks[cand_idx[i]].freq;
        bool        child = false;
        for(size_t r = 0; r < n_roots; ++r)
        {
            const float rf = peaks[root_idx[r]].freq;
            if(is_harm_of(f, rf) || std::fabs(f - rf) < 2.f * bin_hz_)
            {
                child = true;
                break;
            }
        }
        if(!child)
            root_idx[n_roots++] = cand_idx[i];
    }

    // Hysteresis on the mono/poly decision. A bare "n_roots >= 2" flipped mid
    // note on rich material, and every flip reshuffles the whole accept order.
    poly_score_ += (n_roots >= 2) ? 1 : -1;
    if(poly_score_ > 4)
        poly_score_ = 4;
    if(poly_score_ < 0)
        poly_score_ = 0;
    polyphonic_           = polyphonic_ ? (poly_score_ >= 1) : (poly_score_ >= 3);
    const bool polyphonic = polyphonic_;

    auto harmonic_n = [f0_hz](float freq) -> int {
        if(f0_hz < 40.f)
            return 1;
        const float n       = freq / f0_hz;
        const float nearest = std::round(n);
        if(nearest < 1.f || std::fabs(n - nearest) > 0.08f)
            return 0;
        return static_cast<int>(nearest);
    };

    const float pitch = PitchRatio();
    Peak        accepted[64];
    size_t      n_acc = 0;
    bool        used[64];
    for(size_t i = 0; i < 64; ++i)
        used[i] = false;
    // Hann main lobe spans ±2 bins — closer "peaks" are the same lobe.
    const float min_sep_hz = 2.f * bin_hz_;
    const float rel_match  = polyphonic ? 0.04f : 0.03f;
    const float abs_match  = 0.75f * bin_hz_;

    auto too_near_accepted = [&](float freq) -> bool {
        for(size_t a = 0; a < n_acc; ++a)
            if(std::fabs(freq - accepted[a].freq) < min_sep_hz)
                return true;
        return false;
    };

    // Continuation outranks every ranking below: a partial that is already
    // sounding keeps its peak. Otherwise a reshuffle (f0 or mono/poly flip)
    // can push the fundamental out of the top `want` while it is still
    // clearly audible — heard as the tone cutting out with a jump.
    for(size_t s = 0; s < prev_count_ && n_acc < want; ++s)
    {
        if(prev_targets_[s].amp < 1e-4f)
            continue;
        const float pref = prev_targets_[s].freq_hz;
        float       best_d
            = abs_match > rel_match * pref ? abs_match : (rel_match * pref);
        size_t best_i = npeaks;
        for(size_t i = 0; i < npeaks; ++i)
        {
            if(used[i])
                continue;
            const float d = std::fabs(peaks[i].freq * pitch - pref);
            if(d < best_d)
            {
                best_d = d;
                best_i = i;
            }
        }
        if(best_i < npeaks && !too_near_accepted(peaks[best_i].freq))
        {
            used[best_i]      = true;
            accepted[n_acc++] = peaks[best_i];
        }
    }

    if(polyphonic)
    {
        // Stable chord: roots first, then their 2f/3f if present — never
        // "loudest mover of the frame" (that was mal-dies-mal-das).
        for(size_t r = 0; r < n_roots && n_acc < want; ++r)
        {
            const size_t i = root_idx[r];
            if(used[i] || too_near_accepted(peaks[i].freq))
                continue;
            used[i]           = true;
            accepted[n_acc++] = peaks[i];
        }

        for(size_t r = 0; r < n_roots && n_acc < want; ++r)
        {
            for(int h = 2; h <= 3; ++h)
            {
                if(n_acc >= want)
                    break;
                const float target
                    = peaks[root_idx[r]].freq * static_cast<float>(h);
                size_t best_i = npeaks;
                float  best_d = 0.06f * target;
                if(best_d < min_sep_hz)
                    best_d = min_sep_hz;
                for(size_t i = 0; i < npeaks; ++i)
                {
                    if(used[i])
                        continue;
                    const float d = std::fabs(peaks[i].freq - target);
                    if(d < best_d)
                    {
                        best_d = d;
                        best_i = i;
                    }
                }
                if(best_i >= npeaks || too_near_accepted(peaks[best_i].freq))
                    continue;
                used[best_i]      = true;
                accepted[n_acc++] = peaks[best_i];
            }
        }
    }
    else
    {
        // Mono: f0 → harmonics → (if Partials>8) inharmonics. The used flags
        // travel with the peaks through the sort, so continuation survives.
        for(size_t i = 0; i < npeaks; ++i)
        {
            size_t best = i;
            for(size_t j = i + 1; j < npeaks; ++j)
            {
                const int  nj = harmonic_n(peaks[j].freq);
                const int  nb = harmonic_n(peaks[best].freq);
                const bool j_f0
                    = (f0_hz > 0.f && std::fabs(peaks[j].freq - f0_hz) < 3.f);
                const bool b_f0
                    = (f0_hz > 0.f
                       && std::fabs(peaks[best].freq - f0_hz) < 3.f);
                bool take_j = false;
                if(j_f0 && !b_f0)
                    take_j = true;
                else if(j_f0 == b_f0)
                {
                    if(nj > 0 && nb == 0)
                        take_j = true;
                    else if((nj == 0) == (nb == 0))
                    {
                        if(nj > 0 && nb > 0 && nj < nb)
                            take_j = true;
                        else if(nj == nb && peaks[j].mag > peaks[best].mag)
                            take_j = true;
                        else if(nj == 0 && nb == 0
                                && peaks[j].mag > peaks[best].mag)
                            take_j = true;
                    }
                }
                if(take_j)
                    best = j;
            }
            if(best != i)
            {
                const Peak tmp = peaks[i];
                peaks[i]       = peaks[best];
                peaks[best]    = tmp;
                const bool ut  = used[i];
                used[i]        = used[best];
                used[best]     = ut;
            }
        }

        const bool allow_inharm = (want > 8);
        for(size_t i = 0; i < npeaks && n_acc < want; ++i)
        {
            if(used[i])
                continue;
            if(!allow_inharm && harmonic_n(peaks[i].freq) == 0)
                continue;
            if(too_near_accepted(peaks[i].freq))
                continue;
            used[i]           = true;
            accepted[n_acc++] = peaks[i];
        }
    }

    const size_t take  = n_acc;
    const float  scale = MagToAmp();
    auto         peak_amp = [scale, max_mag](float mag, float out_hz) -> float {
        float rel = mag / max_mag;
        if(rel < 0.f)
            rel = 0.f;
        if(rel > 1.f)
            rel = 1.f;
        return mag * scale * (0.35f + 0.65f * rel) * LoudnessWeight(out_hz);
    };

    bool claimed[64];
    for(size_t i = 0; i < 64; ++i)
        claimed[i] = false;

    PartialTarget out[kMaxPartials];
    for(size_t i = 0; i < kMaxPartials; ++i)
    {
        out[i].freq_hz = 110.f;
        out[i].amp     = 0.f;
    }

    for(size_t s = 0; s < prev_count_ && s < kMaxPartials; ++s)
    {
        if(prev_targets_[s].amp < 1e-4f)
            continue;

        const float pref  = prev_targets_[s].freq_hz;
        const float max_d = abs_match > rel_match * pref ? abs_match
                                                         : (rel_match * pref);

        size_t best_p = take;
        float  best_d = max_d;
        for(size_t p = 0; p < take; ++p)
        {
            if(claimed[p])
                continue;
            const float f = accepted[p].freq * pitch;
            const float d = std::fabs(f - pref);
            if(d < best_d)
            {
                best_d = d;
                best_p = p;
            }
        }
        if(best_p < take)
        {
            claimed[best_p] = true;
            const float new_f
                = Clampf(accepted[best_p].freq * pitch, 20.f, 16000.f);
            // A match is a small move now, so light smoothing is enough —
            // the heavy stickiness only existed to hide 93.75 Hz bin jitter.
            const float follow = polyphonic ? 0.35f : 0.5f;
            out[s].freq_hz     = pref + (new_f - pref) * follow;
            out[s].amp = peak_amp(accepted[best_p].mag, out[s].freq_hz);
        }
        else if(polyphonic)
        {
            // Beat null / brief miss: linger — don't kill the voice (flip).
            out[s].freq_hz = pref;
            out[s].amp     = prev_targets_[s].amp * 0.85f;
            if(out[s].amp < 1e-4f)
                out[s].amp = 0.f;
        }
        else
        {
            out[s].freq_hz = pref;
            out[s].amp     = 0.f;
        }
    }

    // Births: poly → only unmatched roots; mono → strong/f0.
    constexpr float kBirthRel = 0.4f;
    for(size_t p = 0; p < take; ++p)
    {
        if(claimed[p])
            continue;
        bool ok = false;
        if(polyphonic)
        {
            for(size_t r = 0; r < n_roots; ++r)
            {
                if(std::fabs(accepted[p].freq - peaks[root_idx[r]].freq) < 3.f)
                {
                    ok = true;
                    break;
                }
            }
        }
        else
        {
            const bool is_f0
                = (f0_hz > 0.f && std::fabs(accepted[p].freq - f0_hz) < 3.f);
            ok = is_f0 || accepted[p].mag >= max_mag * kBirthRel;
        }
        if(!ok)
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
            = Clampf(accepted[p].freq * pitch, 20.f, 16000.f);
        out[slot].amp = peak_amp(accepted[p].mag, out[slot].freq_hz);
    }

    // Active count may be << UI Partials (e.g. one sine → one partial).
    size_t active = 0;
    for(size_t i = 0; i < kMaxPartials; ++i)
    {
        if(out[i].amp > 1e-5f)
            active = i + 1;
    }
    analysis_count_ = active;
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
        fft_time_[i] = g_spec_input_ring[(start + static_cast<uint32_t>(i))
                                         & (kInputRing - 1)];
    input_read_.store(r, std::memory_order_relaxed);

    arm_mult_f32(
        fft_time_, g_spec_window, fft_time_, static_cast<uint32_t>(kFftSize));
    arm_rfft_fast_f32(&g_rfft, fft_time_, fft_freq_, 0);

    g_spec_mags[0] = std::fabs(fft_freq_[0]);
    arm_cmplx_mag_f32(&fft_freq_[2],
                      &g_spec_mags[1],
                      static_cast<uint32_t>(kBinCount - 1));

    float        f0     = 110.f;
    float        best_m = 0.f;
    const size_t f0_max = static_cast<size_t>(
        Clampf(1000.f / bin_hz_, 2.f, static_cast<float>(kBinCount - 1)));
    for(size_t i = 2; i < f0_max; ++i)
    {
        if(g_spec_mags[i] > best_m)
        {
            best_m = g_spec_mags[i];
            f0     = static_cast<float>(i) * bin_hz_;
        }
    }

    ApplyUmbraAurora(g_spec_mags, kBinCount, f0);

    // Smooth magnitudes across analysis frames before peak pick — multi-Trail
    // / Elements spectra hop less between frames (fewer flea births).
    constexpr float kMagSmooth = 0.28f; // slightly stickier — multi-Trail beat
    if(!mag_smooth_valid_)
    {
        for(size_t i = 0; i < kBinCount; ++i)
            g_spec_mag_smooth[i] = g_spec_mags[i];
        mag_smooth_valid_ = true;
    }
    else
    {
        for(size_t i = 0; i < kBinCount; ++i)
            g_spec_mag_smooth[i]
                += kMagSmooth * (g_spec_mags[i] - g_spec_mag_smooth[i]);
    }

    PickPartials(g_spec_mag_smooth, kBinCount);
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

    // Continuous exp slew. Stick against hop jitter; follow real pitch moves
    // faster so Elements / PSP changes stay roughly proportional.
    const float tau_s = Lerp(0.180f, 0.320f, drift);
    const float a     = 1.f - std::exp(-sample_rate_inv_ / tau_s);
    slew_amp_  = a;
    slew_freq_ = a * 0.55f;

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

        // A silent or reassigned slot must jump, never glide. Sliding a
        // recycled oscillator from its old partial to its new one is what drew
        // the periodic 200 → 300 Hz sweeps in the spectrogram. Below ~-60 dB
        // the jump is inaudible; a >25% move can only be a slot reassignment,
        // since matched partials are held to a few percent per hop.
        const float cur = osc_freq_[i];
        const float rel
            = (cur > 1.f) ? (std::fabs(target_freq_[i] - cur) / cur) : 1.f;
        if(osc_amp_[i] < 1e-3f || rel > 0.25f)
        {
            osc_freq_[i]  = target_freq_[i];
            phase_inc_[i] = osc_freq_[i] * sample_rate_inv_;
        }
    }
}

void SpectraEngine::Process(float* out_l, float* out_r, size_t size)
{
    ConsumeTargets();

    waveshape_morph_ = BipolarNorm(params_.waveshape, -1.f, 1.f);
    fold_gain_       = 1.f + std::fabs(waveshape_morph_) * 2.5f;
    folder_.SetGain(fold_gain_);

    const size_t n_act    = active_partials_;
    const float  morph    = waveshape_morph_;
    const bool   do_saw   = morph < -0.02f;
    const bool   do_fold  = morph > 0.02f;
    const float  saw_amt  = do_saw ? -morph : 0.f;
    const float  fold_amt = do_fold ? morph : 0.f;
    const float  sa       = slew_amp_ > 0.f ? slew_amp_ : 0.001f;
    const float  sf       = slew_freq_ > 0.f ? slew_freq_ : 0.001f;

    for(size_t n = 0; n < size; ++n)
    {
        for(size_t i = 0; i < kMaxPartials; ++i)
        {
            const float amp1 = (i < n_act) ? target_amp_[i] : 0.f;
            osc_amp_[i] += sa * (amp1 - osc_amp_[i]);
            osc_freq_[i] += sf * (target_freq_[i] - osc_freq_[i]);
            phase_inc_[i] = osc_freq_[i] * sample_rate_inv_;
        }

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

        mix      = std::tanh(mix);
        out_l[n] = mix;
        out_r[n] = mix;
    }
}

} // namespace perseids
