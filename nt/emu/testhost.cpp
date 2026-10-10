// Windows smoke test (under Wine or on Windows): exports the NT API like
// nt_emu (--export-all-symbols), loads perseids.dll through its shim and runs
// it: construct with a specification, audio in → cloud out, draw, controls.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <distingnt/api.h>

extern "C" {
const _NT_globals NT_globals = {48000, 128, 0, 0, 0, 0};
uint8_t           NT_screen[128 * 64];
static int        draws = 0, params = 0;
int16_t           vals[256];
uint32_t NT_getCpuCycleCount(void) { return 42; }
int32_t  NT_algorithmIndex(const _NT_algorithm*) { return 0; }
uint32_t NT_parameterOffset(void) { return 0; }
void     NT_setParameterFromUi(uint32_t, uint32_t p, int16_t v)
{
    vals[p] = v;
    ++params;
}
void NT_setParameterFromAudio(uint32_t, uint32_t p, int16_t v)
{
    vals[p] = v;
    ++params;
}
void NT_drawText(int, int, const char*, int, _NT_textAlignment, _NT_textSize) { ++draws; }
void NT_drawShapeI(_NT_shape, int, int, int, int, int) { ++draws; }
int  NT_intToString(char* b, int32_t v) { return sprintf(b, "%d", static_cast<int>(v)); }
}

int main()
{
    HMODULE h = LoadLibraryA("perseids.dll");
    if(!h)
    {
        printf("LoadLibrary failed %lu\n", GetLastError());
        return 1;
    }
    typedef uintptr_t (*PE)(_NT_selector, uint32_t);
    PE pe = reinterpret_cast<PE>(GetProcAddress(h, "pluginEntry"));
    printf("pluginEntry %p, API version %u\n", reinterpret_cast<void*>(pe),
           static_cast<unsigned>(pe(kNT_selector_version, 0)));
    const _NT_factory* f = reinterpret_cast<const _NT_factory*>(pe(kNT_selector_factoryInfo, 0));
    printf("factory: %s (%s), %u specification(s)\n", f->name, f->description,
           static_cast<unsigned>(f->numSpecifications));

    int32_t                   spec[1] = {f->specifications[0].def};
    _NT_algorithmRequirements req;
    f->calculateRequirements(req, spec);
    std::vector<uint8_t>    sram(req.sram + 64), dram(req.dram + 64);
    _NT_algorithmMemoryPtrs ptrs = {sram.data(), dram.data(), 0, 0};
    _NT_algorithm*          a    = f->construct(ptrs, req, spec);
    for(unsigned i = 0; i < req.numParameters; ++i)
        vals[i] = a->parameters[i].def;
    a->v                = vals;
    a->vIncludingCommon = vals;
    for(unsigned i = 0; i < req.numParameters; ++i)
        f->parameterChanged(a, i);
    printf("%u parameters, %u bytes SRAM, %.2f MB DRAM\n", static_cast<unsigned>(req.numParameters),
           static_cast<unsigned>(req.sram), req.dram / 1e6);

    // 8 s of plucked 196 Hz tones on In L/R, read Out L/R (busses 13/14).
    std::vector<float> bus(64 * 128);
    double             out_e = 0;
    float              peak  = 0;
    for(int blk = 0; blk < 3000; ++blk)
    {
        std::fill(bus.begin(), bus.end(), 0.f);
        for(int i = 0; i < 128; ++i)
        {
            const float t   = (blk * 128 + i) / 48000.f;
            const float seg = std::fmod(t, 2.f);
            bus[i]          = seg < 1.2f ? 5.f * std::exp(-2.f * seg) * std::sin(6.2831853f * 196.f * t) : 0.f;
            bus[128 + i]    = bus[i];
        }
        f->step(a, bus.data(), 32);
        if(blk > 1500)
            for(int i = 0; i < 128; ++i)
            {
                const float y = bus[12 * 128 + i];
                out_e += y * y;
                peak = std::fabs(y) > peak ? std::fabs(y) : peak;
            }
    }
    const double rms = std::sqrt(out_e / (1499.0 * 128));
    printf("audio: Out L %.3f V rms, peak %.2f V\n", rms, peak);

    const bool custom = f->draw(a);
    printf("draw(): %d calls, %s the parameter line\n", draws, custom ? "replaces" : "keeps");

    _NT_uiData ui = {};
    ui.controls   = kNT_potL;
    ui.pots[0]    = 0.25f;
    f->customUi(a, ui);
    printf("customUi: Pot L → Blend %d (written to own v[]: %s)\n", vals[18], params == 0 ? "yes" : "no");

    const bool ok = rms > 0.01 && peak < 10.f && draws > 20 && vals[18] == 25;
    printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
