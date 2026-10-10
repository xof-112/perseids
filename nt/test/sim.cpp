// Perseids-core NT: native simulation.
//
//   ./test/sim              all checks, writes listening files to test/out/
//   ./test/sim input.wav    also runs your own recording through the plug-in
//
// Test material is synthesised (plucked strings, a sung-vowel pad, breath
// noise, gaps) so the checks are repeatable; the WAV files in test/out/ are
// for listening, because the Phase 4 faults (fleas/siren, mono noise) were
// only audible.

#include "stubs.h"

#include "../perseids_nt.cpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{

int g_fails = 0;
#define CHECK(c, ...)                                                                    \
    do                                                                                   \
    {                                                                                    \
        if(c)                                                                            \
            printf("ok   ");                                                             \
        else                                                                             \
        {                                                                                \
            printf("FAIL ");                                                             \
            ++g_fails;                                                                   \
        }                                                                                \
        printf(__VA_ARGS__);                                                             \
        printf("\n");                                                                    \
    } while(0)

constexpr float kSr = 48000.f;

// ---------------------------------------------------------------------------
// Host

struct Inst
{
    std::vector<uint8_t> sram;
    std::vector<uint8_t> dram;
    int16_t*             v;
    PerseidsAlgorithm*   a;
    int                  seconds;
};

void SetP(Inst& in, int p, int value)
{
    in.v[p] = static_cast<int16_t>(value);
    parameterChanged(in.a, p);
}

Inst Make(int seconds, int16_t* vbuf)
{
    Inst in;
    in.seconds    = seconds;
    int32_t spec[1] = {seconds};
    _NT_algorithmRequirements req{};
    calculateRequirements(req, spec);
    in.sram.assign(req.sram + 16, 0);
    in.dram.assign(req.dram + 16, 0);
    _NT_algorithmMemoryPtrs ptrs{in.sram.data(), in.dram.data(), nullptr, nullptr};
    in.a = static_cast<PerseidsAlgorithm*>(construct(ptrs, req, spec));
    in.v = vbuf;
    for(int p = 0; p < kNumParams; ++p)
        in.v[p] = in.a->parameters[p].def;
    in.a->v                = in.v;
    in.a->vIncludingCommon = in.v;
    for(int p = 0; p < kNumParams; ++p)
        parameterChanged(in.a, p);
    return in;
}

// Busses: 1/2 audio in, 3 rec trig, 13/14 out (Perseids defaults).
struct Signal
{
    std::vector<float> l;
    std::vector<float> r;
};

Signal Run(Inst&              in,
           const Signal&      input,
           int                frames,
           const float*       rec   = nullptr,
           std::vector<float>* bus13 = nullptr)
{
    const size_t       n = input.l.size();
    Signal             out;
    std::vector<float> bus(64 * 128);
    out.l.resize(n);
    out.r.resize(n);
    for(size_t pos = 0; pos < n; pos += frames)
    {
        const int f = static_cast<int>(n - pos < static_cast<size_t>(frames) ? n - pos : frames) & ~3;
        if(f == 0)
            break;
        std::fill(bus.begin(), bus.begin() + 64 * f, 0.f);
        for(int i = 0; i < f; ++i)
        {
            bus[0 * f + i] = input.l[pos + i];
            bus[1 * f + i] = input.r[pos + i];
            if(rec)
                bus[2 * f + i] = rec[pos + i];
            if(bus13)
                bus[12 * f + i] = (*bus13)[pos + i];
        }
        step(in.a, bus.data(), f / 4);
        for(int i = 0; i < f; ++i)
        {
            out.l[pos + i] = bus[12 * f + i];
            out.r[pos + i] = bus[13 * f + i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Material (volts, Eurorack audio ≈ ±5 V)

struct Rng
{
    uint32_t s = 12345;
    float    Next()
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
    }
};

// Karplus-Strong string.
void Pluck(std::vector<float>& out, size_t at, float hz, float amp, float secs, Rng& rng)
{
    const int          period = static_cast<int>(kSr / hz + 0.5f);
    std::vector<float> line(period);
    for(auto& x : line)
        x = rng.Next();
    float  prev = 0.f;
    size_t len  = static_cast<size_t>(secs * kSr);
    for(size_t i = 0; i < len && at + i < out.size(); ++i)
    {
        const int   k = static_cast<int>(i % period);
        const float y = 0.5f * (line[k] + prev) * 0.996f;
        prev          = line[k];
        line[k]       = y;
        out[at + i] += y * amp;
    }
}

// Sawtooth through three vowel formants with vibrato ("aah").
void VowelPad(std::vector<float>& out, size_t at, float hz, float amp, float secs)
{
    struct Bp
    {
        float f, q, z1 = 0, z2 = 0, a0 = 0, b1 = 0, b2 = 0;
        void  Init()
        {
            const float w = 6.2831853f * f / kSr;
            const float r = 1.f - w / (2.f * q);
            a0            = 1.f - r;
            b1            = 2.f * r * std::cos(w);
            b2            = -r * r;
        }
        float Process(float x)
        {
            const float y = a0 * x + b1 * z1 + b2 * z2;
            z2            = z1;
            z1            = y;
            return y;
        }
    } bp[3] = {{730, 8}, {1090, 10}, {2440, 12}};
    for(auto& b : bp)
        b.Init();
    float        ph  = 0.f;
    const size_t len = static_cast<size_t>(secs * kSr);
    for(size_t i = 0; i < len && at + i < out.size(); ++i)
    {
        const float t   = static_cast<float>(i) / kSr;
        const float vib = 1.f + 0.006f * std::sin(6.2831853f * 5.2f * t);
        ph += hz * vib / kSr;
        ph -= std::floor(ph);
        const float saw = 2.f * ph - 1.f;
        float       y   = 0.f;
        for(auto& b : bp)
            y += b.Process(saw);
        float env = 1.f;
        if(t < 0.4f)
            env = t / 0.4f;
        if(t > secs - 0.6f)
            env = (secs - t) / 0.6f;
        out[at + i] += y * amp * env;
    }
}

void Breath(std::vector<float>& out, size_t at, float amp, float secs, Rng& rng)
{
    float        lp  = 0.f;
    const size_t len = static_cast<size_t>(secs * kSr);
    for(size_t i = 0; i < len && at + i < out.size(); ++i)
    {
        lp += 0.08f * (rng.Next() - lp);
        const float t   = static_cast<float>(i) / kSr;
        const float env = std::sin(3.14159265f * t / secs);
        out[at + i] += lp * amp * env;
    }
}

Signal Material(float secs)
{
    Rng                rng;
    const size_t       n = static_cast<size_t>(secs * kSr);
    std::vector<float> m(n, 0.f);
    const float        notes[] = {220.f, 261.63f, 329.63f, 392.f, 293.66f, 246.94f};
    for(int k = 0; k * 2.5f + 0.5f < secs; ++k)
    {
        const size_t at = static_cast<size_t>((0.5f + k * 2.5f) * kSr);
        if(k % 3 == 2)
            VowelPad(m, at, notes[k % 6] * 0.5f, 3.f, 2.0f);
        else
        {
            Pluck(m, at, notes[k % 6], 4.f, 2.2f, rng);
            Pluck(m, at + 4800, notes[(k + 2) % 6], 2.5f, 2.f, rng);
        }
        if(k % 4 == 3)
            Breath(m, at + 24000, 6.f, 1.2f, rng);
    }
    Signal s;
    s.l = m;
    s.r.resize(n);
    for(size_t i = 0; i < n; ++i) // a slightly different right channel
        s.r[i] = i >= 96 ? 0.8f * m[i - 96] : 0.f;
    return s;
}

Signal Tone(float hz, float secs, float volts)
{
    const size_t n = static_cast<size_t>(secs * kSr);
    Signal       s;
    s.l.resize(n);
    for(size_t i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / kSr;
        float       y = 0.f;
        for(int h = 1; h <= 6; ++h)
            y += std::sin(6.2831853f * hz * h * t) / h;
        s.l[i] = y * volts * 0.5f;
    }
    s.r.assign(n, 0.f);
    return s;
}

// ---------------------------------------------------------------------------
// Analysis helpers

float Rms(const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if(to == 0 || to > x.size())
        to = x.size();
    double s = 0;
    for(size_t i = from; i < to; ++i)
        s += x[i] * x[i];
    return to > from ? static_cast<float>(std::sqrt(s / (to - from))) : 0.f;
}

float Peak(const std::vector<float>& x)
{
    float p = 0.f;
    for(float v : x)
        p = std::fabs(v) > p ? std::fabs(v) : p;
    return p;
}

bool Finite(const std::vector<float>& x)
{
    for(float v : x)
        if(!std::isfinite(v))
            return false;
    return true;
}

float Goertzel(const std::vector<float>& x, size_t from, size_t len, float hz)
{
    const float w = 6.2831853f * hz / kSr;
    const float c = 2.f * std::cos(w);
    float       s1 = 0, s2 = 0;
    for(size_t i = 0; i < len; ++i)
    {
        // Hann window against leakage.
        const float win = 0.5f - 0.5f * std::cos(6.2831853f * i / (len - 1));
        const float s0  = x[from + i] * win + c * s1 - s2;
        s2              = s1;
        s1              = s0;
    }
    return std::sqrt(std::max(0.f, s1 * s1 + s2 * s2 - c * s1 * s2));
}

float StrongestHz(const std::vector<float>& x, size_t from, size_t len, float lo, float hi)
{
    float best = 0.f, best_hz = 0.f;
    for(float hz = lo; hz <= hi; hz += 1.f)
    {
        const float m = Goertzel(x, from, len, hz);
        if(m > best)
        {
            best    = m;
            best_hz = hz;
        }
    }
    return best_hz;
}

// Clicks: samples whose step is far above the local step size (first
// difference against a 5 ms moving RMS of the difference).
int Clicks(const std::vector<float>& x, float min_step_volts)
{
    const size_t       n = x.size();
    std::vector<float> d(n, 0.f);
    for(size_t i = 1; i < n; ++i)
        d[i] = x[i] - x[i - 1];
    const size_t w     = 240;
    double       acc   = 0;
    int          count = 0;
    for(size_t i = 0; i < n; ++i)
    {
        acc += d[i] * d[i];
        if(i >= w)
            acc -= d[i - w] * d[i - w];
        if(i < 2 * w)
            continue;
        // Reference: RMS of the window *before* this sample.
        const float ref = static_cast<float>(std::sqrt(std::max(0.0, acc) / w));
        if(std::fabs(d[i]) > min_step_volts && std::fabs(d[i]) > 12.f * ref + 1e-4f)
            ++count;
    }
    return count;
}

void WriteWav(const std::string& path, const Signal& s)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f)
        return;
    const uint32_t n = static_cast<uint32_t>(s.l.size());
    const uint32_t data = n * 4;
    auto           u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto           u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(48000 * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(data);
    for(uint32_t i = 0; i < n; ++i)
    {
        // ±10 V → full scale.
        for(float v : {s.l[i], s.r[i]})
        {
            float x = v / 10.f * 32767.f;
            x       = x > 32767.f ? 32767.f : (x < -32767.f ? -32767.f : x);
            int16_t q = static_cast<int16_t>(std::lrint(x));
            fwrite(&q, 2, 1, f);
        }
    }
    fclose(f);
}

bool ReadWav(const char* path, Signal& s)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    std::vector<uint8_t> b;
    uint8_t              buf[65536];
    size_t               got;
    while((got = fread(buf, 1, sizeof(buf), f)) > 0)
        b.insert(b.end(), buf, buf + got);
    fclose(f);
    if(b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4))
        return false;
    int    fmt = 0, ch = 0, bits = 0;
    size_t pos = 12;
    while(pos + 8 <= b.size())
    {
        const uint32_t sz = b[pos + 4] | b[pos + 5] << 8 | b[pos + 6] << 16 | b[pos + 7] << 24;
        if(!std::memcmp(b.data() + pos, "fmt ", 4))
        {
            fmt  = b[pos + 8] | b[pos + 9] << 8;
            ch   = b[pos + 10] | b[pos + 11] << 8;
            bits = b[pos + 22] | b[pos + 23] << 8;
            if(fmt == 0xFFFE)
                fmt = b[pos + 32] | b[pos + 33] << 8; // extensible: sub-format
        }
        else if(!std::memcmp(b.data() + pos, "data", 4))
        {
            const size_t bytes  = bits / 8;
            const size_t frames = std::min<size_t>(sz, b.size() - pos - 8) / (bytes * ch);
            s.l.resize(frames);
            s.r.resize(frames);
            for(size_t i = 0; i < frames; ++i)
                for(int c = 0; c < ch && c < 2; ++c)
                {
                    const uint8_t* p = b.data() + pos + 8 + (i * ch + c) * bytes;
                    float          v = 0.f;
                    if(fmt == 3 && bits == 32)
                        std::memcpy(&v, p, 4);
                    else if(bits == 16)
                        v = static_cast<int16_t>(p[0] | p[1] << 8) / 32768.f;
                    else if(bits == 24)
                        v = static_cast<int32_t>((p[0] << 8 | p[1] << 16 | p[2] << 24)) / 2147483648.f;
                    else if(bits == 32)
                        v = static_cast<int32_t>(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24) / 2147483648.f;
                    (c == 0 ? s.l : s.r)[i] = v * 5.f; // full scale → ±5 V
                }
            if(ch == 1)
                s.r = s.l;
            return frames > 0;
        }
        pos += 8 + sz + (sz & 1);
    }
    return false;
}

// UI events
_NT_uiData Ui()
{
    _NT_uiData d{};
    return d;
}

void Press(Inst& in, uint32_t bit, bool nt_emu)
{
    _NT_uiData d = Ui();
    d.controls   = static_cast<uint16_t>(bit);
    customUi(in.a, d); // press: c=1 l=0
    d.lastButtons = static_cast<uint16_t>(bit);
    if(nt_emu)
        customUi(in.a, d); // nt_emu release: c=1 l=1
    else
    {
        customUi(in.a, d); // NT hold: c=1 l=1
        d.controls = 0;
        customUi(in.a, d); // NT release: c=0 l=1
    }
}

std::string g_out = "test/out";

} // namespace

int main(int argc, char** argv)
{
    mkdir(g_out.c_str(), 0755);

    // --- construction ------------------------------------------------------
    {
        _NT_algorithmRequirements req{};
        int32_t spec[1] = {10};
        calculateRequirements(req, spec);
        CHECK(req.numParameters == kNumParams, "%d parameters", kNumParams);
        CHECK(req.dram > 4800000 && req.dram < 5200000, "DRAM for 5 x 10 s: %.2f MB", req.dram / 1e6);
        CHECK(req.sram < 4096, "SRAM %u bytes", req.sram);
        spec[0] = 30;
        calculateRequirements(req, spec);
        CHECK(req.dram < 15000000, "DRAM for 5 x 30 s: %.2f MB", req.dram / 1e6);
        calculateRequirements(req, nullptr);
        CHECK(req.dram > 4800000, "no specification → default 10 s");
        printf("     engines %zu bytes in DRAM\n", sizeof(Engines));
    }

    Inst A          = Make(10, g_params);
    g_alg           = A.a;
    g_parameterChanged = parameterChanged;
    CHECK(A.a->params[kParamBuffer].max == 100, "Buffer max follows the specification (10.0 s)");

    // --- silence -----------------------------------------------------------
    {
        Signal s;
        s.l.assign(48000 * 2, 0.f);
        s.r.assign(48000 * 2, 0.f);
        Signal o = Run(A, s, 24);
        CHECK(Finite(o.l) && Peak(o.l) < 1e-6f && Peak(o.r) < 1e-6f, "silence in → silence out");
    }

    // --- Spectra follows pitch -------------------------------------------------
    {
        Inst B = Make(4, g_params);
        g_alg  = B.a;
        SetP(B, kParamCount, 1);
        SetP(B, kParamBuffer, 10);
        SetP(B, kParamHold, kHoldInf);
        SetP(B, kParamFadeIn, 1);
        SetP(B, kParamBlend, 0);
        Signal tone = Tone(220.f, 6.f, 4.f);
        Signal o    = Run(B, tone, 24);
        const float hz = StrongestHz(o.l, 4 * 48000, 48000, 100.f, 1000.f);
        CHECK(std::fabs(hz - 220.f) <= 3.f, "Spectra resynthesises 220 Hz (strongest %.0f Hz)", hz);
        CHECK(Rms(o.l, 4 * 48000) > 0.05f, "Spectra output level %.2f V rms", Rms(o.l, 4 * 48000));
        SetP(B, kParamPitchSpectra, 12);
        Signal o2  = Run(B, tone, 24);
        const float hz2 = StrongestHz(o2.l, 3 * 48000, 48000, 100.f, 1500.f);
        CHECK(std::fabs(hz2 - 440.f) <= 5.f, "Pitch Spectra +12 → %.0f Hz", hz2);
        WriteWav(g_out + "/spectra_220_tone.wav", o);

        SetP(B, kParamPitchSpectra, 0);
        SetP(B, kParamBlend, 100);
        SetP(B, kParamPitchSwarm, 12);
        SetP(B, kParamScatter, 20);
        Signal o3   = Run(B, tone, 24);
        const float hz3 = StrongestHz(o3.l, 3 * 48000, 48000, 100.f, 1500.f);
        CHECK(std::fabs(hz3 - 440.f) <= 5.f, "Swarm with Pitch Swarm +12 → %.0f Hz", hz3);
        const float side = [&] {
            std::vector<float> d(o3.l.size());
            for(size_t i = 0; i < d.size(); ++i)
                d[i] = o3.l[i] - o3.r[i];
            return Rms(d, 3 * 48000);
        }();
        CHECK(side > 0.05f * Rms(o3.l, 3 * 48000), "Swarm is stereo (side/mid %.2f)", side / Rms(o3.l, 3 * 48000));
        g_alg = A.a;
    }

    // --- realistic material ------------------------------------------------
    Signal mat = Material(40.f);
    WriteWav(g_out + "/input.wav", mat);
    {
        SetP(A, kParamCount, 5);
        SetP(A, kParamContRec, 1);
        SetP(A, kParamBuffer, 15);
        SetP(A, kParamHold, 4);
        SetP(A, kParamFadeIn, 3);
        SetP(A, kParamFadeOut, 5);
        SetP(A, kParamThreshold, 10);

        struct Case
        {
            const char* name;
            int         blend, atmo, umbra, wave;
            bool        steps_ok; // by design: Radiation's sample & hold, the naive saw
        } cases[] = {
            {"spectra", 0, 0, 0, 0, false},
            {"swarm", 100, 0, 0, 0, false},
            {"blend50", 50, 0, 0, 0, false},
            {"swarm_blur", 100, -90, 0, 0, false},
            {"swarm_radiation", 100, 80, 0, 0, true},
            {"spectra_umbra_fold", 0, 0, -80, 60, false},
            {"spectra_aurora_saw", 0, 0, 80, -60, true},
        };
        const float in_rms = Rms(mat.l);
        for(const Case& c : cases)
        {
            Inst I = Make(10, g_params);
            g_alg  = I.a;
            for(int p : {kParamCount, kParamContRec, kParamBuffer, kParamHold, kParamFadeIn,
                         kParamFadeOut, kParamThreshold})
                SetP(I, p, A.v[p]);
            SetP(I, kParamBlend, c.blend);
            SetP(I, kParamAtmosphere, c.atmo);
            SetP(I, kParamUmbra, c.umbra);
            SetP(I, kParamWaveshape, c.wave);
            Signal o = Run(I, mat, 24);
            const int clicks = Clicks(o.l, 0.15f) + Clicks(o.r, 0.15f);
            CHECK(Finite(o.l) && Finite(o.r), "%-20s finite", c.name);
            CHECK(Peak(o.l) <= 5.5f && Peak(o.r) <= 5.5f, "%-20s peak %.2f V", c.name,
                  std::max(Peak(o.l), Peak(o.r)));
            CHECK(Rms(o.l) > 0.08f * in_rms, "%-20s level %.2f V rms (input %.2f)", c.name,
                  Rms(o.l), in_rms);
            if(c.steps_ok)
                printf("     %-20s steps (by design): %d\n", c.name, clicks);
            else
                CHECK(clicks == 0, "%-20s clicks: %d", c.name, clicks);
            WriteWav(g_out + "/" + c.name + ".wav", o);
        }
        g_alg = A.a;
    }

    // --- block-size independence ------------------------------------------
    {
        float rms[3];
        int   k = 0;
        for(int frames : {4, 24, 128})
        {
            Inst I = Make(10, g_params);
            g_alg  = I.a;
            SetP(I, kParamCount, 4);
            SetP(I, kParamBuffer, 15);
            SetP(I, kParamBlend, 50);
            Signal o = Run(I, mat, frames);
            rms[k++] = Rms(o.l);
        }
        CHECK(std::fabs(rms[0] / rms[2] - 1.f) < 0.15f && std::fabs(rms[1] / rms[2] - 1.f) < 0.15f,
              "same level at 4 / 24 / 128 frames per step: %.3f %.3f %.3f V", rms[0], rms[1], rms[2]);
        g_alg = A.a;
    }

    // --- capture behaviour ----------------------------------------------------
    {
        Inst I = Make(10, g_params);
        g_alg  = I.a;
        SetP(I, kParamCount, 2);
        SetP(I, kParamBuffer, 10);
        SetP(I, kParamHold, 1);
        SetP(I, kParamFadeIn, 2);
        SetP(I, kParamFadeOut, 3);
        SetP(I, kParamCapture, 0);
        Signal o = Run(I, mat, 24);
        CHECK(Peak(o.l) < 1e-6f, "Capture Off: nothing is recorded");

        // Rec trig input.
        SetP(I, kParamCapture, 1);
        SetP(I, kParamRecIn, 3);
        SetP(I, kParamThreshold, 100); // threshold never reached
        Signal             s = Tone(330.f, 2.f, 3.f);
        std::vector<float> trig(s.l.size(), 0.f);
        for(size_t i = 4800; i < 4900; ++i)
            trig[i] = 5.f;
        TrailLifeUi life[kTrailCount];
        Run(I, s, 24, trig.data());
        I.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase != TrailLifePhase::Empty, "Rec trig input starts a recording (phase %d)",
              static_cast<int>(life[0].phase));

        // Life cycle: Hold 1 s, Fade Out 0.3 s → empty again later.
        SetP(I, kParamFadeOut, 3);
        Signal quiet;
        quiet.l.assign(48000 * 4, 0.f);
        quiet.r = quiet.l;
        Run(I, quiet, 24);
        I.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase == TrailLifePhase::Empty, "Trail fades out after Hold (phase %d)",
              static_cast<int>(life[0].phase));

        // Clear trails.
        Run(I, s, 24, trig.data());
        SetP(I, kParamClear, 1);
        Run(I, quiet, 24);
        I.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase == TrailLifePhase::Empty && I.v[kParamClear] == 0,
              "Clear trails empties all Trails and returns to 0");
        g_alg = A.a;
    }

    // --- output modes ----------------------------------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        SetP(I, kParamBlend, 0);
        std::vector<float> existing(48000 * 3, 1.f);
        Signal             silent;
        silent.l.assign(48000 * 3, 0.f);
        silent.r = silent.l;
        Signal add = Run(I, silent, 24, nullptr, &existing);
        CHECK(std::fabs(add.l[1000] - 1.f) < 1e-6f, "Out mode Add keeps the bus content");
        SetP(I, kParamOutLMode, 1);
        Signal rep = Run(I, silent, 24, nullptr, &existing);
        CHECK(std::fabs(rep.l[1000]) < 1e-6f, "Out mode Replace overwrites the bus");
        g_alg = A.a;
    }

    // --- two instances share nothing -------------------------------------------
    {
        static int16_t v2[256];
        Inst X = Make(4, g_params);
        Inst Y = Make(4, v2);
        Run(X, mat, 24);
        Signal silent;
        silent.l.assign(48000 * 2, 0.f);
        silent.r = silent.l;
        Signal oy = Run(Y, silent, 24);
        CHECK(Peak(oy.l) < 1e-6f, "second instance stays silent while the first plays");
    }

    // --- controls --------------------------------------------------------------
    for(int emu = 0; emu < 2; ++emu)
    {
        Inst I = Make(10, g_params);
        g_alg  = I.a;
        const char* host = emu ? "nt_emu" : "NT";

        _NT_uiData d = Ui();
        d.controls   = kNT_potL | kNT_potC | kNT_potR;
        d.pots[0]    = 0.3f;
        d.pots[1]    = 0.75f;
        d.pots[2]    = 1.f;
        customUi(I.a, d);
        CHECK(I.v[kParamBlend] == 30 && I.v[kParamScan] == 75 && I.v[kParamSize] == 24,
              "%s: pots → Blend %d, Scan %d, Size %d", host, I.v[kParamBlend], I.v[kParamScan],
              I.v[kParamSize]);
        Press(I, kNT_potButtonR, emu);
        d.controls = kNT_potR;
        d.pots[2]  = 0.f;
        customUi(I.a, d);
        CHECK(I.v[kParamAtmosphere] == -100 && I.v[kParamSize] == 24,
              "%s: Pot R press switches to Atmosphere (%d)", host, I.v[kParamAtmosphere]);
        _NT_float3 pots;
        setupUi(I.a, pots);
        CHECK(std::fabs(pots[0] - 0.3f) < 1e-6f && std::fabs(pots[2]) < 1e-6f,
              "%s: setupUi reports the pot targets", host);

        d          = Ui();
        d.encoders[0] = 1;
        customUi(I.a, d);
        d.encoders[0] = 0;
        d.encoders[1] = 3;
        customUi(I.a, d);
        CHECK(I.a->selected == 1 && I.v[kParamTrailLevel1 + 1] == 56,
              "%s: Encoder L selects Trail 2, Encoder R sets its level (%d %%)", host,
              I.v[kParamTrailLevel1 + 1]);
        for(int k = 0; k < 9; ++k)
        {
            d             = Ui();
            d.encoders[0] = 1;
            customUi(I.a, d);
        }
        CHECK(I.a->selected == 2, "%s: selection stops at Count (Trail %d)", host, I.a->selected + 1);

        Press(I, kNT_encoderButtonR, emu);
        CHECK(I.v[kParamLock1 + 2] == 1, "%s: Encoder R click locks the Trail", host);
        Press(I, kNT_encoderButtonL, emu);
        CHECK(I.v[kParamSolo1 + 2] == 1, "%s: Encoder L click solos the Trail", host);
        Press(I, kNT_encoderButtonL, emu);
        CHECK(I.v[kParamSolo1 + 2] == 0, "%s: second click releases Solo", host);

        // Dragging an encoder (nt_emu presses it while turning) is not a click.
        d          = Ui();
        d.controls = kNT_encoderButtonR;
        customUi(I.a, d);
        d.encoders[1] = 1;
        d.lastButtons = kNT_encoderButtonR;
        customUi(I.a, d);
        d.encoders[1] = 0;
        d.controls    = emu ? kNT_encoderButtonR : 0;
        customUi(I.a, d);
        CHECK(I.v[kParamLock1 + 2] == 1, "%s: turning while pressed does not toggle Lock", host);

        Press(I, kNT_button4, emu);
        CHECK(I.v[kParamHold] == kHoldInf, "%s: Button 4 → Hold INF", host);
        Press(I, kNT_button4, emu);
        CHECK(I.v[kParamHold] == 15, "%s: Button 4 again → Hold back to %d s", host, I.v[kParamHold]);

        SetP(I, kParamThreshold, 100);
        Press(I, kNT_button3, emu);
        Signal s = Tone(200.f, 0.2f, 2.f);
        Run(I, s, 24);
        CHECK(I.a->eng->capture.RecActive(), "%s: Button 3 starts recording", host);

        char  buff[64];
        const int n = parameterString(I.a, kParamHold, kHoldInf, buff);
        CHECK(n == 3 && !std::strcmp(buff, "INF"), "Hold 31 shows as INF");
        parameterString(I.a, kParamHold, 7, buff);
        CHECK(!std::strcmp(buff, "7 s"), "Hold 7 shows as '%s'", buff);
        g_alg = A.a;
    }

    // --- display -----------------------------------------------------------------
    {
        Inst I = Make(10, g_params);
        g_alg  = I.a;
        g_textOob = g_shapeOob = 0;
        draw(I.a);
        SetP(I, kParamCount, 5);
        SetP(I, kParamHold, kHoldInf);
        SetP(I, kParamLock1 + 4, 1);
        SetP(I, kParamSolo1 + 3, 1);
        SetP(I, kParamTrailLevel1 + 2, 100);
        Run(I, mat, 24);
        draw(I.a);
        I.a->pot_r_atmo = true;
        SetP(I, kParamAtmosphere, -100);
        draw(I.a);
        SetP(I, kParamHold, 30);
        Run(I, mat, 24);
        draw(I.a);
        // Every Rec style through a recording, its burn-out and Fade In.
        int embers = 0;
        for(int style = 0; style < 3; ++style)
        {
            SetP(I, kParamRecStyle, style);
            SetP(I, kParamClear, 1);
            Signal s = Tone(220.f, 0.03f, 3.f);
            Run(I, s, 24);
            SetP(I, kParamCount, 1);
            SetP(I, kParamContRec, 0);
            Signal t = Tone(220.f, 2.5f, 3.f);
            for(size_t k = 0; k < t.l.size(); k += 2400) // draw every 50 ms
            {
                Signal part;
                part.l.assign(t.l.begin() + k, t.l.begin() + std::min(t.l.size(), k + 2400));
                part.r.assign(part.l.size(), 0.f);
                Run(I, part, 24);
                const int before = g_draws;
                g_logDraw        = false;
                draw(I.a);
                if(style < 2 && I.a->eng->capture.RecActive())
                    embers += g_draws - before;
            }
        }
        CHECK(embers > 200, "recording embers drawn (%d draw calls while recording)", embers);
        CHECK(g_textOob == 0 && g_shapeOob == 0, "display: everything on screen (%d draws)", g_draws);
        g_alg = A.a;
    }

    // --- speed (native, indicative only) ----------------------------------------
    {
        Inst I = Make(10, g_params);
        g_alg  = I.a;
        SetP(I, kParamCount, 5);
        SetP(I, kParamBuffer, 30);
        SetP(I, kParamHold, kHoldInf);
        SetP(I, kParamBlend, 50);
        SetP(I, kParamSize, 24);
        SetP(I, kParamPartials, 32);
        Run(I, mat, 24); // fill Trails
        const auto t0 = std::chrono::steady_clock::now();
        Run(I, mat, 24);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("     native speed: %.1f %% of real time (24 grains, 32 partials, 5 Trails)\n",
               100.0 * s / 40.0);
        g_alg = A.a;
    }

    // --- your own recording ---------------------------------------------------------
    if(argc > 1)
    {
        Signal in;
        if(!ReadWav(argv[1], in) || in.l.empty())
            printf("could not read %s (PCM 16/24/32 bit or float WAV)\n", argv[1]);
        else
        {
            for(int blend : {0, 50, 100})
            {
                Inst I = Make(10, g_params);
                g_alg  = I.a;
                SetP(I, kParamBlend, blend);
                SetP(I, kParamContRec, 1);
                Signal      o    = Run(I, in, 24);
                std::string name = g_out + "/yours_blend" + std::to_string(blend) + ".wav";
                WriteWav(name, o);
                printf("     %s: %.2f V rms, %d clicks\n", name.c_str(), Rms(o.l),
                       Clicks(o.l, 0.15f) + Clicks(o.r, 0.15f));
            }
        }
    }

    printf("\n%s (%d failed)\n", g_fails ? "FAILED" : "all checks passed", g_fails);
    return g_fails ? 1 : 0;
}
