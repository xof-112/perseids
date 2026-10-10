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
    in.a->mod_arm_now = true; // as if back on the plug-in screen (own test below)
}

// Like the NT menu: no return to the plug-in screen, slot settings wait.
void SetMenu(Inst& in, int p, int value)
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

// Turns a pot like a hand would: from where its target stands now (catch-up
// picks it up there at once) to `to`, in 1 % steps.
void Turn(Inst& in, int k, float to)
{
    _NT_float3 now;
    setupUi(in.a, now);
    const uint32_t bits[3] = {kNT_potL, kNT_potC, kNT_potR};
    const float    from    = now[k];
    const int      steps   = 1 + static_cast<int>(std::fabs(to - from) * 100.f);
    for(int i = 1; i <= steps; ++i)
    {
        _NT_uiData d = Ui();
        d.controls   = static_cast<uint16_t>(bits[k]);
        d.pots[k]    = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
        customUi(in.a, d);
    }
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
        CHECK(req.sram < 8192, "SRAM %u bytes (instance + parameter table)", req.sram);
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

    // --- Level match: Spectra and Swarm equally loud ------------------------------
    {
        // Glock-like bell hits and a soft sine pad, Blend 0 vs 100.
        auto Glock = [](float secs) {
            Signal s;
            const size_t n = static_cast<size_t>(secs * kSr);
            s.l.assign(n, 0.f);
            const float notes[] = {1046.5f, 1318.5f, 1568.f, 1174.7f};
            for(int k = 0; k * 0.6f + 0.2f < secs; ++k)
            {
                const size_t at = static_cast<size_t>((0.2f + k * 0.6f) * kSr);
                const float  f  = notes[k % 4];
                for(size_t i = 0; at + i < n && i < kSr * 1.5f; ++i)
                {
                    const float t = i / kSr;
                    s.l[at + i] += 3.f * (std::exp(-3.f * t) * std::sin(6.2831853f * f * t)
                                          + 0.4f * std::exp(-7.f * t) * std::sin(6.2831853f * f * 2.76f * t)
                                          + 0.2f * std::exp(-12.f * t) * std::sin(6.2831853f * f * 5.4f * t));
                }
            }
            s.r = s.l;
            return s;
        };
        auto Pad = [](float secs) {
            Signal s;
            const size_t n = static_cast<size_t>(secs * kSr);
            s.l.assign(n, 0.f);
            for(size_t i = 0; i < n; ++i)
            {
                const float t = i / kSr;
                const float env = std::min(1.f, t / 0.5f);
                s.l[i] = 2.f * env * (std::sin(6.2831853f * 220.f * t) + 0.5f * std::sin(6.2831853f * 330.f * t)
                                      + 0.3f * std::sin(6.2831853f * 440.f * t));
            }
            s.r = s.l;
            return s;
        };
        const char* names[2] = {"glock", "pad"};
        for(int m = 0; m < 2; ++m)
        for(int match = 1; match >= 0; --match)
        {
            Signal in = m == 0 ? Glock(12.f) : Pad(12.f);
            float  p90[2];
            int    slot = 0;
            for(int blend : {0, 100})
            {
                Inst I = Make(10, g_params);
                g_alg  = I.a;
                SetP(I, kParamLevelMatch, match);
                SetP(I, kParamBlend, 50);
                Signal half;
                half.l.assign(in.l.begin(), in.l.begin() + 48000 * 6);
                half.r = half.l;
                Run(I, half, 24); // learn at Blend 50
                SetP(I, kParamBlend, blend);
                Signal o = Run(I, in, 24);
                std::vector<float> tail(o.l.begin() + 48000 * 6, o.l.end());
                // short-term loudness: 90th percentile of 50 ms RMS windows
                std::vector<float> w;
                for(size_t i = 0; i + 2400 <= tail.size(); i += 2400)
                {
                    double e = 0;
                    for(size_t j = 0; j < 2400; ++j) e += tail[i + j] * tail[i + j];
                    w.push_back(std::sqrt(e / 2400));
                }
                std::sort(w.begin(), w.end());
                p90[slot++] = w[w.size() * 9 / 10];
                CHECK(Finite(o.l) && Peak(o.l) < 5.5f, "%s, Level match %s, Blend %d: finite, peak %.2f V",
                      names[m], match ? "on" : "off", blend, Peak(o.l));
            }
            const float db = 20.f * std::log10(p90[1] / p90[0]);
            if(match)
                CHECK(std::fabs(db) < 1.5f, "%s: Level match on, Swarm vs Spectra %+.1f dB", names[m], db);
            else
                printf("     %s: Level match off, Swarm vs Spectra %+.1f dB (unmatched, for reference)\n", names[m], db);
        }
        // Trail Level stays an accent after Level match: both the Trail sum and
        // the engines move with it, so the match does not undo it.
        for(int lmo = getenv("LM_TRACE") ? 0 : 1; lmo < 2; ++lmo)
        for(int blend : {0, 50, 100})
        {
            Inst I = Make(10, g_params);
            g_alg  = I.a;
            SetP(I, kParamLevelMatch, lmo);
            SetP(I, kParamCount, 1);
            SetP(I, kParamHold, kHoldInf);
            SetP(I, kParamOverwrite, 0);
            SetP(I, kParamBlend, blend);
            Signal take = Glock(3.f);
            Run(I, take, 24);
            Signal settle;
            settle.l.assign(48000 * 8, 0.f); // let the match settle first
            settle.r = settle.l;
            Run(I, settle, 24);
            Signal quiet;
            quiet.l.assign(48000 * 6, 0.f);
            quiet.r = quiet.l;
            auto Loud = [&](const Signal& o) {
                std::vector<float> w;
                for(size_t i = 48000 * 2; i + 2400 <= o.l.size(); i += 2400)
                {
                    double e = 0;
                    for(size_t k = 0; k < 2400; ++k) e += o.l[i + k] * o.l[i + k];
                    w.push_back(std::sqrt(e / 2400));
                }
                std::sort(w.begin(), w.end());
                return w[w.size() * 9 / 10];
            };
            if(getenv("LM_TRACE"))
                for(int t = 0; t < 12; ++t)
                {
                    Signal q; q.l.assign(24000, 0.f); q.r = q.l;
                    Run(I, q, 24);
                    printf("     t+%.1fs g_sp %+.2f dB g_sw %+.2f dB in %.2e sp %.2e\n", 0.5f * (t + 1), 20 * std::log10(I.a->lm_g_sp), 20 * std::log10(I.a->lm_g_sw), I.a->lm_in.env, I.a->lm_sp.env);
                }
            const float l50 = Loud(Run(I, quiet, 24));
            const float g_sp = I.a->lm_g_sp, g_sw = I.a->lm_g_sw;
            SetP(I, kParamTrailLevel1, 20);
            const float l20 = Loud(Run(I, quiet, 24));
            if(!lmo)
            {
                printf("     Level match off, Blend %d: Level 50 → 20 %% is %.1f dB\n", blend, 20.f * std::log10(l20 / l50));
                continue;
            }
            const float db  = 20.f * std::log10(l20 / l50);
            const float dg  = 20.f * std::log10((I.a->lm_g_sp * I.a->lm_g_sw) / (g_sp * g_sw));
            CHECK(db < -6.5f && db > -9.5f && std::fabs(dg) < 1.5f,
                  "Level match, Blend %d: Trail Level 50 → 20 %% is %.1f dB quieter (−8 expected), gains moved %+.1f dB",
                  blend, db, dg);
        }
        // Match speed: knock the Swarm gain 6 dB off its settled value and see
        // how much of the way back it gets in one second.
        float moved[3];
        for(int spd = 0; spd < 3; ++spd)
        {
            Inst I = Make(10, g_params);
            g_alg  = I.a;
            SetP(I, kParamMatchSpeed, spd);
            SetP(I, kParamCount, 1);
            SetP(I, kParamHold, kHoldInf);
            SetP(I, kParamOverwrite, 0);
            SetP(I, kParamBlend, 100);
            Run(I, Glock(3.f), 24);
            Signal q;
            q.l.assign(48000 * 8, 0.f);
            q.r = q.l;
            Run(I, q, 24);
            const float g0 = I.a->lm_g_sw;
            I.a->lm_g_sw   = g0 * 2.f;
            Signal one;
            one.l.assign(48000, 0.f);
            one.r = one.l;
            Run(I, one, 24);
            moved[spd] = 1.f - std::log(I.a->lm_g_sw / g0) / std::log(2.f);
        }
        // Lock-in: a fresh instance is close to its settled match early on.
        {
            Inst I = Make(10, g_params);
            g_alg  = I.a;
            SetP(I, kParamMatchSpeed, 0); // even at Slow
            SetP(I, kParamCount, 1);
            SetP(I, kParamHold, kHoldInf);
            SetP(I, kParamOverwrite, 0);
            SetP(I, kParamBlend, 0);
            Run(I, Glock(3.f), 24);
            Signal q;
            q.l.assign(48000 * 3, 0.f);
            q.r = q.l;
            Run(I, q, 24);
            const float early = I.a->lm_g_sp;
            Run(I, q, 24);
            Run(I, q, 24);
            Run(I, q, 24);
            const float late = I.a->lm_g_sp;
            CHECK(std::fabs(20.f * std::log10(early / late)) < 1.5f,
                  "Level match locks in fast: 3 s into playback %+.1f dB, settled %+.1f dB (Slow)",
                  20.f * std::log10(early), 20.f * std::log10(late));
        }
        CHECK(moved[2] > 0.8f && moved[1] > moved[0] && moved[2] > moved[1],
              "Match speed: share of the re-levelling after 1 s Slow %.0f %%, Medium %.0f %%, Fast %.0f %%",
              moved[0] * 100.f, moved[1] * 100.f, moved[2] * 100.f);
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


    // --- Hold: INF keeps Trails, changes apply to playing Trails ---------------
    {
        Inst I = Make(10, g_params);
        g_alg  = I.a;
        TrailLifeUi life[kTrailCount];
        // Overwrite Off + INF: threshold triggers fill the pool, then leave it alone.
        SetP(I, kParamHold, kHoldInf);
        SetP(I, kParamOverwrite, 0);
        Signal m = Material(30.f);
        Run(I, m, 24);
        I.a->eng->capture.GetTrailLifeUi(life);
        bool all_inf = true;
        for(int k = 0; k < 3; ++k)
            all_inf = all_inf && life[k].phase == TrailLifePhase::Hold && life[k].hold_sec < 0;
        CHECK(all_inf && !I.a->eng->capture.RecActive(),
              "Overwrite Off + Hold INF: Trails keep playing, no re-recording (%d %d %d)",
              static_cast<int>(life[0].phase), static_cast<int>(life[1].phase), static_cast<int>(life[2].phase));
        I.a->eng->capture.RequestManualTrigger();
        Run(I, Material(0.5f), 24);
        CHECK(I.a->eng->capture.RecActive(), "Overwrite Off + Hold INF: Rec still replaces the oldest Trail");

        // Live changes on one playing Trail (Rec trig, quiet afterwards).
        Inst J = Make(10, g_params);
        g_alg  = J.a;
        SetP(J, kParamCount, 1);
        SetP(J, kParamThreshold, 100);
        SetP(J, kParamRecIn, 3);
        SetP(J, kParamBuffer, 10);
        SetP(J, kParamFadeIn, 1);
        SetP(J, kParamFadeOut, 3);
        SetP(J, kParamHold, 15);
        Signal             s = Tone(330.f, 1.5f, 3.f);
        std::vector<float> trig(s.l.size(), 0.f);
        for(size_t i = 480; i < 580; ++i)
            trig[i] = 5.f;
        Run(J, s, 24, trig.data()); // ~1.1 s recorded, ~0.4 s of Hold
        Signal quiet;
        quiet.l.assign(48000 * 3, 0.f);
        quiet.r = quiet.l;
        Run(J, quiet, 24); // ~3.4 s of Hold played
        SetP(J, kParamHold, 10);
        Run(J, Signal{std::vector<float>(4800, 0.f), std::vector<float>(4800, 0.f)}, 24);
        J.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase == TrailLifePhase::Hold && life[0].hold_sec >= 6 && life[0].hold_sec <= 7,
              "Hold 15 → 10 s while playing: %d s left", life[0].hold_sec);
        SetP(J, kParamHold, kHoldInf);
        Run(J, quiet, 24);
        Run(J, quiet, 24);
        Run(J, quiet, 24); // 9 s more: would have run out at 10 s
        J.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase == TrailLifePhase::Hold && life[0].hold_sec < 0,
              "Hold → INF while playing stops the countdown (phase %d, %d s)",
              static_cast<int>(life[0].phase), life[0].hold_sec);
        SetP(J, kParamHold, 5); // ~13 s played already → fade out at once
        Run(J, Signal{std::vector<float>(4800, 0.f), std::vector<float>(4800, 0.f)}, 24);
        J.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase == TrailLifePhase::FadeOut, "Hold below the time played: fade-out starts at once (phase %d)",
              static_cast<int>(life[0].phase));
        SetP(J, kParamHold, 25); // back up during the fade: the Trail returns
        Run(J, quiet, 24);
        J.a->eng->capture.GetTrailLifeUi(life);
        CHECK(life[0].phase == TrailLifePhase::Hold && life[0].hold_sec >= 9 && life[0].hold_sec <= 11,
              "longer Hold during the fade-out brings the Trail back (phase %d, %d s)",
              static_cast<int>(life[0].phase), life[0].hold_sec);
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

    // --- Resonator -------------------------------------------------------------------
    {
        // Noise Trail, Swarm only, Resonator fully wet: the bank's root rings.
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        SetP(I, kParamCount, 1);
        SetP(I, kParamBuffer, 10);
        SetP(I, kParamHold, kHoldInf);
        SetP(I, kParamFadeIn, 1);
        SetP(I, kParamBlend, 100);
        SetP(I, kParamResoMix, 100);
        SetP(I, kParamResoDecay, 80);
        Rng    rng;
        Signal n;
        n.l.resize(48000 * 4);
        for(auto& x : n.l)
            x = rng.Next() * 3.f;
        n.r = n.l;
        const float c2 = 65.406f;
        // Energy on the bank's harmonics vs. halfway between them.
        auto OnGrid = [&](const std::vector<float>& x, float f0) {
            float on = 0.f, off = 0.f;
            for(int k = 1; k <= 6; ++k)
            {
                on += Goertzel(x, 3 * 48000, 48000, f0 * k);
                off += Goertzel(x, 3 * 48000, 48000, f0 * (k + 0.5f));
            }
            return on / (off + 1e-9f);
        };
        Signal      o  = Run(I, n, 24);
        const float g1 = OnGrid(o.l, c2);
        CHECK(g1 > 4.f, "Resonator rings on the harmonics of C2 (on/off grid %.1f)", g1);

        SetP(I, kParamResoPitch, 12);
        o              = Run(I, n, 24);
        const float g2 = OnGrid(o.l, 2.f * c2);
        CHECK(g2 > 4.f && OnGrid(o.l, c2) < 0.6f * g2,
              "Reso pitch +12 → grid on C3 (%.1f, C2 grid %.1f)", g2, OnGrid(o.l, c2));

        SetP(I, kParamResoPitch, 0);
        SetP(I, kParamResoVoct, 4);
        std::vector<float> one(n.l.size(), 1.f); // 1 V on bus 4
        // Run() puts rec on bus 3; build the V/Oct bus by hand.
        std::vector<float> bus(64 * 24);
        Signal             ov;
        ov.l.resize(n.l.size());
        for(size_t pos = 0; pos + 24 <= n.l.size(); pos += 24)
        {
            std::fill(bus.begin(), bus.end(), 0.f);
            for(int i = 0; i < 24; ++i)
            {
                bus[0 * 24 + i] = n.l[pos + i];
                bus[1 * 24 + i] = n.r[pos + i];
                bus[3 * 24 + i] = 1.f;
            }
            step(I.a, bus.data(), 6);
            for(int i = 0; i < 24; ++i)
                ov.l[pos + i] = bus[12 * 24 + i];
        }
        const float g3 = OnGrid(ov.l, 2.f * c2);
        CHECK(g3 > 4.f && OnGrid(ov.l, c2) < 0.6f * g3,
              "Reso V/Oct +1 V → grid on C3 (%.1f, C2 grid %.1f)", g3, OnGrid(ov.l, c2));
        CHECK(Finite(ov.l) && Peak(ov.l) <= 5.5f, "Resonator output finite, peak %.2f V", Peak(ov.l));

        // Spectra only: the Resonator is not in its path (as in the firmware).
        SetP(I, kParamResoVoct, 0);
        SetP(I, kParamBlend, 0);
        o              = Run(I, n, 24);
        const float g4 = OnGrid(o.l, c2);
        CHECK(g4 < 2.f, "Blend 0 %%: no Resonator ring (C2 grid %.1f)", g4);
        g_alg = A.a;
    }

    // --- Dry/Wet ---------------------------------------------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        SetP(I, kParamCapture, 0); // no cloud: only the dry side is heard
        Signal s = Tone(220.f, 0.5f, 2.f);
        for(size_t i = 0; i < s.r.size(); ++i)
            s.r[i] = -0.5f * s.l[i]; // distinct right channel
        Signal o = Run(I, s, 24);
        CHECK(Peak(o.l) < 1e-6f, "Dry/Wet 100 %% (default): cloud only, no dry signal");

        SetP(I, kParamDryWet, 0);
        o = Run(I, s, 24);
        float err = 0.f;
        for(size_t i = 0; i < s.l.size(); ++i)
        {
            const float xl = s.l[i] * 0.2f * 0.85f, xr = s.r[i] * 0.2f * 0.85f;
            const float el = SoftLimit(xl) * 5.f - o.l[i], er = SoftLimit(xr) * 5.f - o.r[i];
            err = std::max(err, std::max(std::fabs(el), std::fabs(er)));
        }
        CHECK(err < 1e-4f, "Dry/Wet 0 %%: clean stereo input at −1,4 dB (error %.1e V)", err);

        SetP(I, kParamDryWet, 50);
        Signal o50 = Run(I, s, 24);
        const float ratio = Rms(o50.l) / Rms(o.l);
        CHECK(std::fabs(ratio - 0.7071f) < 0.03f, "Dry/Wet 50 %%: dry at −3 dB (equal power, %.3f)", ratio);

        SetP(I, kParamInR, 0);
        Signal om = Run(I, s, 24);
        CHECK(std::fabs(Rms(om.r) - Rms(om.l)) < 1e-4f, "In R unpatched: dry In L on both outputs");
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

    // --- Clock in: synced mod LFOs ------------------------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        SetP(I, kParamClockIn, 3);
        SetP(I, kParamModSync1, 6);     // x1: one LFO cycle per pulse
        SetP(I, kParamModSync1 + 1, 3); // /4: one cycle over four pulses
        SetP(I, kParamModSync1 + 2, 9); // x4
        // 2 Hz clock (period 24000 samples), 5 ms pulses, 4 s.
        const size_t       len = 48000 * 4;
        std::vector<float> ck(len, 0.f);
        for(size_t i = 1000; i < len; i += 24000)
            for(size_t k = 0; k < 240 && i + k < len; ++k)
                ck[i + k] = 5.f;
        Signal s;
        s.l.assign(24, 0.f);
        s.r = s.l;
        std::vector<float> ph0, ph1, ph2;
        bool               valid_seen = false;
        for(size_t pos = 0; pos + 24 <= len; pos += 24)
        {
            Run(I, s, 24, ck.data() + pos);
            valid_seen = valid_seen || I.a->clock_valid;
            if(pos >= 1000 + 24000 * 2 && (pos - 1000) % 6000 < 24) // quarter-pulse marks
            {
                ph0.push_back(I.a->lfo_phase[0]);
                ph1.push_back(I.a->lfo_phase[1]);
                ph2.push_back(I.a->lfo_phase[2]);
            }
        }
        CHECK(valid_seen && std::fabs(I.a->clock_period - 24000.f) < 30.f,
              "Clock in: 2 Hz clock detected (period %.0f samples)", I.a->clock_period);
        // x1 at quarter steps between pulses: 0, .25, .5, .75 (±0.02).
        bool x1_ok = ph0.size() >= 8, d4_ok = ph1.size() >= 8, x4_ok = ph2.size() >= 8;
        for(size_t k = 0; k < ph0.size(); ++k)
        {
            const float want = 0.25f * static_cast<float>(k % 4);
            float       d    = std::fabs(ph0[k] - want);
            d                = std::min(d, 1.f - d);
            x1_ok            = x1_ok && d < 0.02f;
            float d2         = std::fabs(ph2[k]);
            d2               = std::min(d2, 1.f - d2);
            x4_ok            = x4_ok && d2 < 0.03f; // four cycles per pulse: whole cycles at the marks
        }
        for(size_t k = 1; k < ph1.size(); ++k)
        {
            float step = ph1[k] - ph1[k - 1];
            if(step < 0.f)
                step += 1.f;
            d4_ok = d4_ok && std::fabs(step - 0.0625f) < 0.01f; // 1/16 cycle per quarter pulse
        }
        CHECK(x1_ok, "Clock in: x1 LFO runs one cycle per pulse, locked to the pulses");
        CHECK(d4_ok, "Clock in: /4 LFO runs one cycle over four pulses");
        CHECK(x4_ok, "Clock in: x4 LFO runs four cycles per pulse");
        // Clock stops: after 4 periods (2 s) synced slots fall back to free.
        std::vector<float> none(48000 * 3, 0.f);
        Signal             q;
        q.l.assign(none.size(), 0.f);
        q.r = q.l;
        Run(I, q, 24, none.data());
        CHECK(!I.a->clock_valid, "Clock in: clock lost after 4 missing pulses, LFOs run free");
        g_textOob = g_shapeOob = 0;
        draw(I.a);
        CHECK(g_textOob == 0 && g_shapeOob == 0, "Clock in: display on screen");
        g_alg = A.a;
    }

    // --- 12 mod slots, mod the mod ------------------------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        auto Dest = [&](int param) {
            for(int k = 0; k < kNumModTargets; ++k)
                if(kModTargets[k] == param)
                    return k;
            return 0;
        };
        Signal s;
        s.l.assign(4800, 0.f); // 0.1 s
        s.r = s.l;
        // Slot 12: offset −40 % on Blend.
        SetP(I, ModParam(11, kModDest), Dest(kParamBlend));
        SetP(I, ModParam(11, kModOffset), -40);
        Run(I, s, 24);
        CHECK(std::fabs(I.a->blend - 0.1f) < 0.01f, "Mod 12 works like the others (Blend 50 → %.0f %%)",
              I.a->blend * 100.f);
        // Mod the mod: slot 2 pushes slot 1's LFO rate from 1 Hz up by +50 % of its travel.
        SetP(I, ModParam(0, kModDest), Dest(kParamScan));
        SetP(I, ModParam(0, kModAmount), 20);
        SetP(I, ModParam(0, kModRate), 100);
        float p0 = I.a->lfo_phase[0];
        Run(I, s, 24);
        float slow = I.a->lfo_phase[0] - p0;
        slow += slow < 0.f ? 1.f : 0.f;
        SetP(I, ModParam(1, kModDest), Dest(ModParam(0, kModRate)));
        SetP(I, ModParam(1, kModOffset), 50);
        Run(I, s, 24); // let the chain settle one block
        p0 = I.a->lfo_phase[0];
        Signal t;
        t.l.assign(480, 0.f); // 10 ms: under one cycle even at 11 Hz
        t.r = t.l;
        Run(I, t, 24);
        float fast = I.a->lfo_phase[0] - p0;
        fast += fast < 0.f ? 1.f : 0.f;
        CHECK(std::fabs(slow - 0.1f) < 0.01f && std::fabs(fast / 0.01f - 11.f) < 0.5f,
              "Mod the mod: slot 2 raises slot 1's LFO from %.1f Hz to %.1f Hz", slow / 0.1f, fast / 0.01f);
        CHECK(kNumModTargets == 85, "mod targets incl. amount / offset / rate of all 12 slots (%d)", kNumModTargets);
        g_alg = A.a;
    }

    // --- Mod mode, overview reset, menu marks, free + reset sync ---------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        auto Dest = [&](int param) {
            for(int k = 0; k < kNumModTargets; ++k)
                if(kModTargets[k] == param)
                    return k;
            return 0;
        };
        Signal s;
        s.l.assign(960, 0.f);
        s.r = s.l;
        CHECK(I.v[kParamClockIn] == 3, "Clock in defaults to In 3");
        SetP(I, kParamBlend, 80);
        SetP(I, ModParam(0, kModDest), Dest(kParamBlend));
        SetP(I, ModParam(0, kModOffset), 20);
        Run(I, s, 24);
        const float around = I.a->blend;
        SetP(I, ModModeParam(0), 1);
        Run(I, s, 24);
        const float over = I.a->blend;
        CHECK(std::fabs(around - 1.f) < 0.01f && std::fabs(over - 0.6f) < 0.01f,
              "Mod mode: Around base 80 %% + 20 %% = %.0f %%, Override ignores the 80 %%: %.0f %%",
              around * 100.f, over * 100.f);
        // Menu marks: the modulated parameter's name gets " ~", back after a reset.
        g_paramDefUpdates = 0;
        draw(I.a);
        CHECK(!std::strcmp(I.a->parameters[kParamBlend].name, "Blend ~") && g_paramDefUpdates == 1,
              "menu mark: '%s' while modulated (%d update)", I.a->parameters[kParamBlend].name, g_paramDefUpdates);
        draw(I.a);
        CHECK(g_paramDefUpdates == 1, "menu mark: no further updates while nothing changes");
        SetP(I, ModParam(5, kModDest), Dest(kParamScan));
        SetP(I, ModParam(5, kModAmount), 30);
        SetP(I, ModSyncParam(5), 3);
        SetP(I, kParamModReset, 1);
        Run(I, s, 24);
        bool clean = I.v[kParamModReset] == 0;
        for(int m = 0; m < kNumModSlots; ++m)
            clean = clean && I.v[ModParam(m, kModDest)] == 0 && I.v[ModParam(m, kModAmount)] == 0
                    && I.v[ModParam(m, kModOffset)] == 0 && I.v[ModSyncParam(m)] == 0 && I.v[ModModeParam(m)] == 0;
        draw(I.a);
        CHECK(clean && I.a->mod_active == 0 && std::fabs(I.a->blend - 0.8f) < 0.01f
                  && !std::strcmp(I.a->parameters[kParamBlend].name, "Blend"),
              "Reset all mods: every slot off, Blend back to its 80 %%, name '%s'", I.a->parameters[kParamBlend].name);
        // Free rate, restarted by the clock: Rst x1 and Rst x2 at a 2 Hz clock,
        // own rate 0.5 Hz (phase grows 1/16 per quarter pulse).
        SetP(I, ModSyncParam(0), kModSyncLocked + 6); // Rst x1
        SetP(I, ModSyncParam(1), kModSyncLocked + 7); // Rst x2
        SetP(I, ModParam(0, kModRate), 50);
        SetP(I, ModParam(1, kModRate), 50);
        const size_t       len = 48000 * 3;
        std::vector<float> ck(len, 0.f);
        for(size_t i = 1000; i < len; i += 24000)
            for(size_t k = 0; k < 240 && i + k < len; ++k)
                ck[i + k] = 5.f;
        Signal one;
        one.l.assign(24, 0.f);
        one.r = one.l;
        bool r1 = true, r2 = true;
        int  marks = 0;
        for(size_t pos = 0; pos + 24 <= len; pos += 24)
        {
            Run(I, one, 24, ck.data() + pos);
            if(pos >= 1000 + 24000 && (pos - 1000) % 6000 < 24)
            {
                const int   q  = static_cast<int>(((pos - 1000) / 6000) % 4); // quarter of the pulse
                const float w1 = 0.0625f * q;
                const float w2 = 0.0625f * (q % 2);
                r1 = r1 && std::fabs(I.a->lfo_phase[0] - w1) < 0.02f;
                r2 = r2 && std::fabs(I.a->lfo_phase[1] - w2) < 0.02f;
                ++marks;
            }
        }
        CHECK(marks >= 6 && r1, "Rst x1: own rate, restarted on every pulse");
        CHECK(marks >= 6 && r2, "Rst x2: own rate, restarted twice per pulse");
        g_alg = A.a;
    }

    // --- Mod overview: only the slots in use (dest + amount) and the next free one -----
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        const _NT_parameterPage& pg = I.a->parameterPages->pages[kModOverviewPage];
        CHECK(!std::strcmp(pg.name, "Mod overview"), "Mod overview page index (%s)", pg.name);
        draw(I.a);
        CHECK(pg.numParams == 1 && pg.params[0] == kParamModReset,
              "Mod overview, nothing set: only the reset (%d entries)", pg.numParams);
        g_pageUpdates = 0;
        SetP(I, ModParam(0, kModDest), 11);
        SetP(I, ModParam(3, kModDest), 12);
        draw(I.a);
        const bool ok = pg.numParams == 5 && pg.params[1] == ModParam(0, kModDest) && pg.params[2] == ModParam(0, kModAmount)
                        && pg.params[3] == ModParam(3, kModDest) && pg.params[4] == ModParam(3, kModAmount);
        CHECK(ok && g_pageUpdates == 1, "Mod overview: only the active slots 1 and 4, each dest + amount (%d entries, %d update)",
              pg.numParams, g_pageUpdates);
        draw(I.a);
        CHECK(g_pageUpdates == 1, "Mod overview: no host update while nothing changes");
        g_alg = A.a;
    }

    // --- live slot output in the amount entry -------------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        char b1[64], b2[64];
        parameterString(I.a, ModParam(2, kModAmount), 40, b1);
        CHECK(!std::strcmp(b1, "40 %"), "amount of an idle slot reads '%s'", b1);
        SetP(I, ModParam(2, kModDest), 11);
        SetP(I, ModParam(2, kModAmount), 40);
        SetP(I, ModParam(2, kModRate), 200); // 2 Hz
        Signal s;
        s.l.assign(4800, 0.f);
        s.r = s.l;
        Run(I, s, 24);
        parameterString(I.a, ModParam(2, kModAmount), 40, b1);
        Run(I, s, 24);
        Run(I, s, 24);
        parameterString(I.a, ModParam(2, kModAmount), 40, b2);
        CHECK(!std::strncmp(b1, "40 % > ", 7) && std::strcmp(b1, b2) != 0,
              "amount of a working slot shows its output live: '%s' … '%s'", b1, b2);
        g_alg = A.a;
    }

    // --- slot settings from the menu take effect after a pause ------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        Signal s;
        s.l.assign(4800, 0.f); // 0.1 s
        s.r = s.l;
        SetP(I, ModParam(0, kModOffset), 30);
        Run(I, s, 24);
        // Scroll the destination through a few targets in the menu.
        for(int k = 1; k <= 12; ++k)
        {
            SetMenu(I, ModParam(0, kModDest), k);
            Run(I, s, 24);
        }
        CHECK(I.a->mod_active == 0, "menu scroll through destinations: nothing is modulated on the way");
        g_textOob = g_shapeOob = 0;
        draw(I.a);
        CHECK(g_textOob == 0, "footer shows the waiting slot (MOD*)");
        for(int t = 0; t < 16; ++t) // 1.6 s without change
            Run(I, s, 24);
        CHECK(I.a->mod_active == 1 && I.a->mod_armed[0][kArmDest] == 12,
              "after 1.5 s standing still the last destination takes effect");
        SetMenu(I, ModParam(0, kModDest), 11);
        Run(I, s, 24);
        _NT_uiData d = Ui();
        customUi(I.a, d); // back on the plug-in screen
        Run(I, s, 24);
        CHECK(I.a->mod_armed[0][kArmDest] == 11, "back on the plug-in screen: takes effect at once");
        SetMenu(I, ModParam(0, kModAmount), 40);
        Run(I, s, 24);
        CHECK(std::fabs(I.a->mod_sum[kModTargets[11]] - 0.3f) < 0.45f && I.v[ModParam(0, kModAmount)] == 40,
              "amount / offset / rate act at once");
        g_alg = A.a;
    }

    // --- mod slots ---------------------------------------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        auto M = [&](int slot, int field) { return kParamMod1 + slot * kModParams + field; };
        auto Dest = [&](int param) {
            for(int k = 0; k < kNumModTargets; ++k)
                if(kModTargets[k] == param)
                    return k;
            return 0;
        };
        Signal s;
        s.l.assign(4800, 0.f);
        s.r = s.l;
        std::vector<float> cv(s.l.size(), 5.f); // +5 V on bus 3 ("Rec trig in" bus, unused here)

        // CV +5 V, Amount +50 % on Blend (base 50 %) → 100 %; stored value stays.
        SetP(I, M(0, kModIn), 3);
        SetP(I, M(0, kModDest), Dest(kParamBlend));
        SetP(I, M(0, kModAmount), 50);
        Run(I, s, 24, cv.data());
        CHECK(std::fabs(I.a->blend - 1.f) < 0.02f && I.v[kParamBlend] == 50,
              "CV +5 V, amount 50 %% on Blend → %.2f (stored %d %%)", I.a->blend, I.v[kParamBlend]);

        // Inverted: amount −50 % → 0 %.
        SetP(I, M(0, kModAmount), -50);
        Run(I, s, 24, cv.data());
        CHECK(I.a->blend < 0.02f, "amount −50 %% inverts → Blend %.2f", I.a->blend);

        // Offset only (no cable → LFO, amount 0): −50 % on Pitch Spectra → −24 HT.
        SetP(I, M(0, kModIn), 0);
        SetP(I, M(0, kModDest), Dest(kParamPitchSpectra));
        SetP(I, M(0, kModAmount), 0);
        SetP(I, M(0, kModOffset), -50);
        Run(I, s, 24);
        CHECK(std::fabs(I.a->spectra_p.pitch_spectra + 1.f) < 1e-4f && I.a->blend > 0.49f,
              "offset −50 %% on Pitch Spectra → %.0f HT, Blend back to its stored value",
              I.a->spectra_p.pitch_spectra * 24.f);

        // Internal LFO, 1 Hz, amount 100 % on Scan (base 20 %): sweeps the range.
        SetP(I, M(1, kModDest), Dest(kParamScan));
        SetP(I, M(1, kModAmount), 100);
        SetP(I, M(1, kModRate), 100);
        float lo = 1.f, hi = 0.f;
        for(int k = 0; k < 50; ++k)
        {
            Signal b;
            b.l.assign(960, 0.f);
            b.r = b.l;
            Run(I, b, 24);
            lo = std::min(lo, I.a->swarm_p.scan);
            hi = std::max(hi, I.a->swarm_p.scan);
        }
        CHECK(lo < 0.02f && hi > 0.98f, "LFO 1 Hz on Scan sweeps %.2f … %.2f", lo, hi);

        // Switches and counts: offset +100 % locks Trail 1, +10 % on Size adds 2 grains.
        SetP(I, M(2, kModDest), Dest(kParamLock1));
        SetP(I, M(2, kModOffset), 100);
        SetP(I, M(3, kModDest), Dest(kParamSize));
        SetP(I, M(3, kModOffset), 10);
        Run(I, s, 24);
        CHECK(I.a->mixer[0].locked && I.a->swarm_p.size == 18.f && I.a->mod_active == 4,
              "switch and count destinations (Lock 1 on, Size %.0f), 4 slots active",
              I.a->swarm_p.size);

        // Two slots on one destination add up; changing a destination releases the old one.
        SetP(I, M(3, kModDest), Dest(kParamLock1));
        SetP(I, M(3, kModOffset), -100);
        Run(I, s, 24);
        CHECK(!I.a->mixer[0].locked && I.a->swarm_p.size == 16.f,
              "two slots sum on Lock 1 (+100 − 100 = off), Size back to 16");

        // All slots off → everything back to the stored values.
        for(int m = 0; m < kNumModSlots; ++m)
            SetP(I, M(m, kModDest), 0);
        Run(I, s, 24);
        CHECK(std::fabs(I.a->spectra_p.pitch_spectra) < 1e-6f && std::fabs(I.a->swarm_p.scan - 0.2f) < 1e-4f
                  && I.a->mod_active == 0,
              "all slots off → stored values again");

        // Destination list: every entry is a real parameter with a name.
        bool names_ok = true;
        for(int k = 1; k < kNumModTargets; ++k)
            names_ok = names_ok && !std::strcmp(kModTargetNames[k], kParameterDefs[kModTargets[k]].name);
        CHECK(names_ok, "mod destination names match the parameter names (%d targets)", kNumModTargets - 1);

        // Display with MOD indicator.
        SetP(I, M(0, kModDest), Dest(kParamBlend));
        SetP(I, M(0, kModOffset), 10);
        Run(I, s, 24);
        g_textOob = g_shapeOob = 0;
        draw(I.a);
        CHECK(g_textOob == 0 && g_shapeOob == 0, "display with MOD indicator on screen");
        g_alg = A.a;
    }

    // --- controls --------------------------------------------------------------
    for(int emu = 0; emu < 2; ++emu)
    {
        Inst I = Make(10, g_params);
        g_alg  = I.a;
        const char* host = emu ? "nt_emu" : "NT";

        _NT_uiData d = Ui();
        Turn(I, 0, 0.3f);
        Turn(I, 1, 0.75f);
        Turn(I, 2, 1.f);
        CHECK(I.v[kParamBlend] == 30 && I.v[kParamScan] == 75 && I.v[kParamSize] == 24,
              "%s: pots → Blend %d, Scan %d, Size %d", host, I.v[kParamBlend], I.v[kParamScan],
              I.v[kParamSize]);
#ifndef NT_EMU_WIN // pot presses are ignored in the nt_emu build (own block below)
        Press(I, kNT_potButtonR, emu);
        Turn(I, 2, 0.f);
        CHECK(I.v[kParamAtmosphere] == -100 && I.v[kParamSize] == 24,
              "%s: Pot R press switches to Atmosphere (%d)", host, I.v[kParamAtmosphere]);
        Press(I, kNT_potButtonC, emu);
        Turn(I, 1, 0.4f);
        CHECK(I.v[kParamResoMix] == 40 && I.v[kParamScan] == 75,
              "%s: Pot C press switches to Reso mix (%d %%)", host, I.v[kParamResoMix]);
        Press(I, kNT_potButtonC, emu);
        Press(I, kNT_potButtonL, emu);
        Turn(I, 0, 0.6f);
        CHECK(I.v[kParamDryWet] == 60 && I.v[kParamBlend] == 30,
              "%s: Pot L press switches to Dry/Wet (%d %%), Blend stays %d %%", host,
              I.v[kParamDryWet], I.v[kParamBlend]);
        g_textOob = g_shapeOob = 0;
        draw(I.a);
        CHECK(g_textOob == 0 && g_shapeOob == 0, "%s: footer DRY/WET on screen", host);
        Press(I, kNT_potButtonL, emu);
        d.controls = kNT_potL;
        d.pots[0]  = 0.3f;
        _NT_float3 pots;
        setupUi(I.a, pots);
        CHECK(std::fabs(pots[0] - 0.3f) < 1e-6f && std::fabs(pots[2]) < 1e-6f,
              "%s: setupUi reports the pot targets", host);
#else
        {
            _NT_float3 pots;
            setupUi(I.a, pots);
            CHECK(std::fabs(pots[0] - 0.3f) < 1e-6f && std::fabs(pots[2] - 1.f) < 1e-6f,
                  "%s: setupUi reports the pot targets", host);
        }
#endif

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

    // --- pot catch-up: no jump after switching or after a menu edit ---------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
#ifdef NT_EMU_WIN
        const bool emu_ev = true;
#else
        const bool emu_ev = false;
#endif
        auto Move = [&](int k, float pos) {
            const uint32_t bits[3] = {kNT_potL, kNT_potC, kNT_potR};
            _NT_uiData     d       = Ui();
            d.controls             = static_cast<uint16_t>(bits[k]);
            d.pots[k]              = pos;
            customUi(I.a, d);
        };
        Turn(I, 0, 0.05f); // Blend 5 %
        Press(I, kPotLSwitch, emu_ev);
        for(float p = 0.05f; p < 1.f; p += 0.01f) // Dry/Wet: pot from 5 % up to 100 %
            Move(0, p);
        Move(0, 1.f);
        const int dw = I.v[kParamDryWet];
        Press(I, kPotLSwitch, emu_ev); // back to Blend, pot still at 100 %
        Move(0, 0.97f);
        Move(0, 0.9f);
        CHECK(I.v[kParamBlend] == 5 && dw == 100, "catch-up: Blend stays 5 %% while the pot is far away (%d %%)",
              I.v[kParamBlend]);
        g_textOob = g_shapeOob = 0;
        draw(I.a);
        CHECK(g_textOob == 0, "catch-up: footer drawn while waiting");
        for(float p = 0.9f; p > 0.02f; p -= 0.01f) // turn down past 5 %
            Move(0, p);
        Move(0, 0.2f);
        CHECK(I.v[kParamBlend] == 20, "catch-up: after passing 5 %% the pot takes over (Blend %d %%)",
              I.v[kParamBlend]);
        // Jumping across the value in one event also counts as passing it.
        Turn(I, 2, 0.2f); // Size 8
        Press(I, kPotRSwitch, emu_ev);
        Move(2, 0.3f);    // Atmosphere 0 (= 0.5), pot below: wait
        CHECK(I.v[kParamAtmosphere] == 0, "catch-up: Atmosphere waits (%d)", I.v[kParamAtmosphere]);
        Move(2, 0.8f);    // jumped across 0.5 → caught
        Move(2, 0.3f);
        CHECK(I.v[kParamAtmosphere] == -40, "catch-up: crossing the value picks it up (%d)", I.v[kParamAtmosphere]);
        // A change from the parameter menu: the pot waits for the new value.
        SetP(I, kParamAtmosphere, 60);
        Move(2, 0.32f);
        CHECK(I.v[kParamAtmosphere] == 60, "catch-up: menu edit is kept until the pot gets there (%d)",
              I.v[kParamAtmosphere]);
        for(float p = 0.32f; p < 0.86f; p += 0.01f)
            Move(2, p);
        CHECK(I.v[kParamAtmosphere] >= 66 && I.v[kParamAtmosphere] <= 72,
              "catch-up: then follows the pot again (%d)", I.v[kParamAtmosphere]);
        _NT_float3 pots;
        setupUi(I.a, pots);
        CHECK(std::fabs(pots[2] - (I.v[kParamAtmosphere] + 100) * 0.005f) < 1e-6f,
              "catch-up: setupUi still reports the target for the NT's own soft takeover");
        g_alg = A.a;
    }

#ifdef NT_EMU_WIN
    // --- nt_emu: buttons 1/2 switch the pot targets ------------------------------
    {
        Inst I = Make(4, g_params);
        g_alg  = I.a;
        // nt_emu's own button events: press = bit in controls and lastButtons,
        // release = bit in controls only.
        auto EmuButton = [&](uint32_t bit) {
            _NT_uiData d  = Ui();
            d.controls    = static_cast<uint16_t>(bit);
            d.lastButtons = static_cast<uint16_t>(bit);
            customUi(I.a, d);
            d.lastButtons = 0;
            customUi(I.a, d);
        };
        CHECK((hasCustomUi(I.a) & (kNT_button1 | kNT_button2)) == (kNT_button1 | kNT_button2),
              "nt_emu build claims buttons 1/2");
        EmuButton(kNT_button1);
        Turn(I, 0, 0.2f);
        CHECK(I.a->pot_l_mix && I.v[kParamDryWet] == 20 && I.v[kParamBlend] == 50,
              "nt_emu: button 1 → Pot L sets Dry/Wet (%d %%)", I.v[kParamDryWet]);
        EmuButton(kNT_button2);
        Turn(I, 2, 1.f);
        CHECK(I.a->pot_r_atmo && I.v[kParamAtmosphere] == 100,
              "nt_emu: button 2 → Pot R sets Atmosphere (%d %%)", I.v[kParamAtmosphere]);
        Turn(I, 1, 0.9f);
        CHECK(I.a->pot_c_reso && I.v[kParamResoMix] == 90,
              "nt_emu: button 2 also puts Pot C on Reso mix (%d %%)", I.v[kParamResoMix]);
        // nt_emu sends a pot "press" when a pot drag starts: it must not flip the target.
        for(uint32_t pb : {kNT_potButtonL, kNT_potButtonC, kNT_potButtonR})
        {
            _NT_uiData p = Ui();
            p.controls   = static_cast<uint16_t>(pb);
            customUi(I.a, p);
            p.lastButtons = static_cast<uint16_t>(pb);
            customUi(I.a, p);
            p.controls = 0;
            customUi(I.a, p);
        }
        Turn(I, 0, 0.35f);
        Turn(I, 1, 0.45f);
        CHECK(I.a->pot_l_mix && I.a->pot_c_reso && I.a->pot_r_atmo && I.v[kParamDryWet] == 35
                  && I.v[kParamResoMix] == 45 && I.v[kParamBlend] == 50,
              "nt_emu: pot drag/press keeps the target (Dry/Wet %d, Reso %d, Blend %d)",
              I.v[kParamDryWet], I.v[kParamResoMix], I.v[kParamBlend]);
        CHECK((hasCustomUi(I.a) & (kNT_potButtonL | kNT_potButtonC | kNT_potButtonR)) == 0,
              "nt_emu build does not claim pot presses");
        EmuButton(kNT_button1);
        EmuButton(kNT_button2);
        CHECK(!I.a->pot_l_mix && !I.a->pot_r_atmo && !I.a->pot_c_reso, "nt_emu: buttons 1/2 switch back");
        EmuButton(kNT_button3);
        Signal s = Tone(200.f, 0.2f, 2.f);
        SetP(I, kParamThreshold, 100);
        Run(I, s, 24);
        CHECK(I.a->eng->capture.RecActive(), "nt_emu: button 3 (on release) starts recording");
        g_alg = A.a;
    }
#else
    {
        Inst I = Make(4, g_params);
        CHECK((hasCustomUi(I.a) & (kNT_button1 | kNT_button2)) == 0,
              "NT build leaves buttons 1/2 to the NT");
    }
#endif

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
        {
            // Header: version label must end left of the REC box (nt_emu font widths).
            int vw = 0;
            for(const char* c = kVersion; *c; ++c)
                vw += 4;
            CHECK(48 + vw <= 69 && 72 + 35 <= 108,
                  "header: version (48…%d) clear of REC box (69…104), 'next 5' clear of IN", 48 + vw);
        }
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
