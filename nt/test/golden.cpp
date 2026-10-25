// Golden test: the same scenario through two versions of the plug-in must give
// bit-identical audio (new features at their defaults change nothing).
//
//   make golden GOLDEN_REV=<git rev>   builds this file against nt/perseids_nt.cpp
//                                      and against the one in <rev>, compares
//
// Scenario: plucked tones in, a clock on In 3 (2.1 Hz, 10 ms pulses), three
// synced mod slots (x1 on Blend, /4 on Scan, Rst /2 on Spread), a take by
// button 3, the clock stops for 3 s and comes back. Uses only what every
// version has: parameters found by name, mod destinations by name.
#include "stubs.h"

#include PERSEIDS_SRC

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int Param(const char* name)
{
    for(int p = 0; p < kNumParams; ++p)
        if(!std::strcmp(kParameterDefs[p].name, name))
            return p;
    std::fprintf(stderr, "no parameter %s\n", name);
    std::exit(2);
}

int Dest(const char* name)
{
    for(int k = 0; k < kNumModTargets; ++k)
        if(kModTargetNames[k] && !std::strcmp(kModTargetNames[k], name))
            return k;
    std::fprintf(stderr, "no mod target %s\n", name);
    std::exit(2);
}
} // namespace

int main(int argc, char** argv)
{
    if(argc < 3)
        return 2;
    const int frames = std::atoi(argv[1]);
    FILE*     out    = std::fopen(argv[2], "wb");

    static int16_t       v[kNumParams];
    int32_t              spec[1] = {10};
    _NT_algorithmRequirements req{};
    calculateRequirements(req, spec);
    std::vector<uint8_t> sram(req.sram + 16), dram(req.dram + 16);
    _NT_algorithmMemoryPtrs ptrs{sram.data(), dram.data(), nullptr, nullptr};
    auto* a = static_cast<PerseidsAlgorithm*>(construct(ptrs, req, spec));
    for(int p = 0; p < kNumParams; ++p)
        v[p] = a->parameters[p].def;
    a->v = a->vIncludingCommon = v;
    g_alg                      = a;
    for(int p = 0; p < kNumParams; ++p)
        parameterChanged(a, p);
    auto Set = [&](const char* name, int value) {
        const int p = Param(name);
        v[p]        = static_cast<int16_t>(value);
        parameterChanged(a, p);
        a->mod_arm_now = true;
    };
    Set("Mod 1 dest", Dest("Blend"));
    Set("Mod 1 amount", 40);
    Set("Mod 1 sync", 6); // x1
    Set("Mod 2 dest", Dest("Scan"));
    Set("Mod 2 amount", 30);
    Set("Mod 2 sync", 3); // /4
    Set("Mod 3 dest", Dest("Spread"));
    Set("Mod 3 amount", 50);
    Set("Mod 3 sync", 15); // Rst /2
    Set("Mod 3 shape", 4);

    const float        sr  = 48000.f;
    const long         len = static_cast<long>(12.f * sr);
    std::vector<float> bus(64 * 256);
    for(long pos = 0; pos + frames <= len; pos += frames)
    {
        std::fill(bus.begin(), bus.begin() + 64 * frames, 0.f);
        for(int i = 0; i < frames; ++i)
        {
            const long  t   = pos + i;
            const float s   = t / sr;
            const float seg = std::fmod(s, 1.7f);
            bus[0 * frames + i] = 4.f * std::exp(-seg * 3.f) * std::sin(6.2831853f * 220.f * s);
            bus[1 * frames + i] = 4.f * std::exp(-seg * 3.f) * std::sin(6.2831853f * 330.f * s);
            const bool  clock_on = s < 5.f || s > 8.f;
            const float ph       = std::fmod(s * 2.1f, 1.f);
            bus[2 * frames + i]  = clock_on && ph < 0.021f ? 5.f : 0.f;
        }
        if(pos <= static_cast<long>(0.3f * sr) && pos + frames > static_cast<long>(0.3f * sr))
            a->eng->capture.RequestManualTrigger();
        step(a, bus.data(), frames / 4);
        std::fwrite(bus.data() + 12 * frames, sizeof(float), frames, out);
        std::fwrite(bus.data() + 13 * frames, sizeof(float), frames, out);
    }
    std::fclose(out);
    return 0;
}
