// Plays draw() against logging stubs: every drawing call becomes one line on
// stdout ("F title", "T …", "S …"), render.py turns that into PNGs.
#include "../test/stubs.h"

#include "../perseids_nt.cpp"
#include "../test/stubs.cpp"

#include <cmath>
#include <cstring>
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
        // The NT redraws continuously; keep the display's animation state
        // current between the logged frames (silently, every ~50 ms).
        if(smp % 2432 < 128)
        {
            g_logDraw = false;
            draw(A);
            g_logDraw = true;
        }
    }
}

// One screen: the draw calls (for checks) and then NT_screen as nt_emu shows
// it, 64 rows of 256 hex digits (one per pixel, 0–15).
static void Frame(const char* title)
{
    printf("F %s\n", title);
    std::memset(NT_screen, 0, sizeof(NT_screen));
    draw(A);
    for(int y = 0; y < 64; ++y)
    {
        char row[257];
        for(int x = 0; x < 256; ++x)
        {
            const uint8_t b = NT_screen[y * 128 + x / 2];
            row[x]          = "0123456789abcdef"[(x & 1) ? (b & 15) : (b >> 4)];
        }
        row[256] = 0;
        printf("P %s\n", row);
    }
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
    Frame("Trail 1 recording, Rec style PLR: embers travel left to right up to the take");
    Run(0.15f, true);
    Frame("... 150 ms later (each ember has its own speed)");
    Run(0.45f, true);
    Frame("Take finished: embers burn out (200 ms), then Fade In fills the bar");

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
    Frame("Hold 2 s ran out: Trails fade out (OUT), the bar empties left to right");

    // The other two recording styles.
    SetP(kParamCount, 1);
    SetP(kParamLock1, 0);
    SetP(kParamClear, 1);
    Run(0.05f, false);
    SetP(kParamContRec, 0);
    SetP(kParamRecStyle, 0);
    A->eng->capture.RequestManualTrigger(); // Button 3
    Run(0.9f, true);
    Frame("Rec style PRS: embers grow from the centre");
    SetP(kParamClear, 1);
    Run(0.05f, false);
    SetP(kParamRecStyle, 2);
    A->eng->capture.RequestManualTrigger();
    Run(0.9f, true);
    Frame("Rec style CTR: solid bar from the centre");

    // Mod view: Encoder L turned past the last Trail.
    auto Dest = [](int param) {
        for(int k = 0; k < kNumModTargets; ++k)
            if(kModTargets[k] == param)
                return k;
        return 0;
    };
    SetP(ModParam(0, kModDest), Dest(kParamBlend));
    SetP(ModParam(0, kModAmount), 40);
    SetP(ModParam(0, kModRate), 50);
    SetP(kParamScan, 100); // one-sided: from 100 % down to about 40 %
    SetP(ModParam(1, kModDest), Dest(kParamScan));
    SetP(ModParam(1, kModOffset), -30);
    SetP(ModParam(1, kModAmount), 30);
    SetP(ModParam(1, kModRate), 120);
    SetP(ModParam(2, kModDest), Dest(kParamResoMix));
    SetP(ModParam(2, kModAmount), 25);
    SetP(ModParam(2, kModRate), 300);
    SetP(ModParam(4, kModDest), Dest(kParamPitchSwarm));
    SetP(ModParam(4, kModAmount), 10);
    SetP(ModParam(4, kModRate), 20);
    SetP(ModParam(6, kModDest), Dest(kParamAtmosphere));
    SetP(ModModeParam(6), 1);
    SetP(ModParam(6, kModAmount), 80);
    SetP(ModParam(6, kModRate), 70);
    A->mod_arm_now = true;
    A->mod_view    = true;
    A->mod_sel     = 1;
    Run(2.5f, false);
    Frame("Mod view (Encoder L past the last Trail): one tile per active slot, scope of the destination, base dotted");
    SetP(kParamModView, 1);
    Run(0.2f, false);
    Frame("Mod view as numbers (Display → Mod view): base > now, offset, ovr = Override");
    return 0;
}
