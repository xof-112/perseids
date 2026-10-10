// Plays draw() against logging stubs: every drawing call becomes one line on
// stdout ("F title", "T …", "S …"), render.py turns that into PNGs.
#include "../test/stubs.h"

#include "../perseids_nt.cpp"
#include "../test/stubs.cpp"

#include <cmath>
#include <cstdio>
#include <vector>

static PerseidsAlgorithm* A;
static std::vector<float> bus(64 * 128);
static long               smp = 0;

static void SetP(int p, int v)
{
    g_params[p] = static_cast<int16_t>(v);
    parameterChanged(A, p);
}

// Plucked tones with gaps, 5 V peak.
static void Run(float secs, bool sound)
{
    const long end = smp + static_cast<long>(secs * 48000.f);
    for(; smp < end; smp += 128)
    {
        std::fill(bus.begin(), bus.end(), 0.f);
        for(int i = 0; i < 128; ++i)
        {
            const float t   = (smp + i) / 48000.f;
            const float seg = std::fmod(t, 2.f);
            const float env = sound && seg < 1.2f ? std::exp(-seg * 2.f) : 0.f;
            bus[i]          = 5.f * env * std::sin(6.2831853f * 196.f * t);
            bus[128 + i]    = bus[i];
        }
        step(A, bus.data(), 32);
    }
}

static void Frame(const char* title)
{
    printf("F %s\n", title);
    draw(A);
}

int main()
{
    static std::vector<uint8_t> sram, dram;
    int32_t                     spec[1] = {10};
    _NT_algorithmRequirements   req{};
    calculateRequirements(req, spec);
    sram.assign(req.sram + 16, 0);
    dram.assign(req.dram + 16, 0);
    _NT_algorithmMemoryPtrs ptrs{sram.data(), dram.data(), nullptr, nullptr};
    A = static_cast<PerseidsAlgorithm*>(construct(ptrs, req, spec));
    for(int p = 0; p < kNumParams; ++p)
        g_params[p] = A->parameters[p].def;
    A->v = A->vIncludingCommon = g_params;
    g_alg              = A;
    g_parameterChanged = parameterChanged;
    g_logDraw          = true;

    Frame("Start: nothing recorded yet, Trail 1 is next");

    SetP(kParamBuffer, 15);
    SetP(kParamHold, 8);
    SetP(kParamFadeIn, 10);
    Run(1.0f, true);
    Frame("Trail 1 recording (Threshold crossed), meter shows the input");

    SetP(kParamCount, 5);
    SetP(kParamContRec, 1);
    Run(5.3f, true);
    SetP(kParamLock1, 1);
    SetP(kParamTrailLevel1 + 1, 80);
    A->selected = 1;
    Frame("Count 5, Cont. Rec: Trails fade in, hold (seconds left), Trail 1 locked");

    SetP(kParamHold, kHoldInf);
    SetP(kParamSolo1 + 2, 1);
    SetP(kParamBlend, 80);
    A->selected   = 2;
    A->pot_r_atmo = true;
    SetP(kParamAtmosphere, -40);
    Run(3.f, false);
    Frame("Button 4: Hold INF · Trail 3 solo · Pot R on Atmosphere (press Pot R)");

    SetP(kParamHold, 2);
    SetP(kParamFadeOut, 40);
    SetP(kParamSolo1 + 2, 0);
    Run(4.4f, false);
    Frame("Hold 2 s ran out: Trails fade out (OUT)");
    return 0;
}
