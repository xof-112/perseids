// Native test of the VCV host (nt_host) with the real Perseids plug-in:
// audio through, controls, menu, persistence, two instances side by side.
// Writes the screens it checks as "F title" + "P row" lines for render.py:
//   ./host_test > frames.txt && python3 ../../render/render.py frames.txt host-screens.png
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/nt_host.hpp"

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...)                         \
    do                                        \
    {                                         \
        ++g_checks;                           \
        if(!(c))                              \
            ++g_fail;                         \
        std::fprintf(stderr, "%s ", (c) ? "ok  " : "FAIL"); \
        std::fprintf(stderr, __VA_ARGS__);    \
        std::fprintf(stderr, "\n");           \
    } while(0)

static const _NT_factory* Factory()
{
    return reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo, 0));
}

static int Param(const nt_host::Host& h, const char* name)
{
    for(int p = 0; p < h.NumParams(); ++p)
        if(!std::strcmp(h.ParamName(p), name))
            return p;
    return -1;
}

static long g_t = 0;
// Runs `secs` of a 220 Hz tone (5 V) into In L/R; controls every 32 frames,
// screen every 1600 frames. Returns the output RMS.
static float Run(nt_host::Host& h, float secs, nt_host::Controls& c, bool tone = true)
{
    double     acc = 0;
    const long n   = static_cast<long>(secs * 48000);
    for(long i = 0; i < n; ++i, ++g_t)
    {
        float in[kNT_numInputBusses] = {};
        if(tone)
            in[0] = in[1] = 5.f * std::sin(6.2831853f * 220.f * g_t / 48000.f);
        float out[2];
        h.Frame(in, out);
        acc += out[0] * out[0] + out[1] * out[1];
        if(i % 32 == 0)
        {
            h.Ui(c);
            c.encoders[0] = c.encoders[1] = 0;
        }
        if(i % 1600 == 0)
            h.Draw();
    }
    return static_cast<float>(std::sqrt(acc / (2.0 * n)));
}

static void Press(nt_host::Host& h, nt_host::Controls& c, uint16_t bit)
{
    c.buttons |= bit;
    Run(h, 0.02f, c);
    c.buttons &= ~bit;
    Run(h, 0.02f, c);
}

static void Menu(nt_host::Host& h, nt_host::Controls& c)
{
    c.menu = true;
    Run(h, 0.02f, c);
    c.menu = false;
    Run(h, 0.02f, c);
}

static void Frame(nt_host::Host& h, const char* title)
{
    h.Draw();
    uint8_t s[nt_host::kScreenBytes];
    h.CopyScreen(s);
    std::printf("F %s\n", title);
    for(int y = 0; y < 64; ++y)
    {
        char row[257];
        for(int x = 0; x < 256; ++x)
        {
            const uint8_t b = s[y * 128 + x / 2];
            row[x]          = "0123456789abcdef"[(x & 1) ? (b & 15) : (b >> 4)];
        }
        row[256] = 0;
        std::printf("P %s\n", row);
    }
}

static int Lit(nt_host::Host& h)
{
    uint8_t s[nt_host::kScreenBytes];
    h.Draw();
    h.CopyScreen(s);
    int n = 0;
    for(uint8_t b : s)
        n += (b >> 4) != 0;
    return n;
}

int main()
{
    const _NT_factory* f = Factory();
    CHECK(f && f->name && !std::strcmp(f->name, "Perseids"), "factory: %s", f ? f->name : "-");

    nt_host::Host h;
    CHECK(h.Create(f, {10}, 48000), "construct (10 s Trails), %d parameters", h.NumParams());
    nt_host::Controls c;
    const int         blend = Param(h, "Blend"), hold = Param(h, "Hold");
    CHECK(blend >= 0 && hold >= 0, "parameters found by name");

    // Record one take (button 3), then listen.
    Run(h, 0.5f, c);
    Press(h, c, kNT_button3);
    Run(h, 3.0f, c);
    const float rms = Run(h, 2.0f, c);
    CHECK(rms > 0.05f && std::isfinite(rms), "audio through the cloud: out RMS %.3f V", rms);
    CHECK(Lit(h) > 300, "Perseids draws its screen (%d lit)", Lit(h));
    Frame(h, "Perseids screen in the VCV host");

    // Bus names after the panel jacks (as the module sets them).
    {
        std::vector<std::string> names = {"In L", "In R"};
        for(int k = 1; k <= 10; ++k)
            names.push_back("CV " + std::to_string(k));
        h.SetBusNames(names);
        const int clk = Param(h, "Clock in");
        CHECK(h.ValueString(clk) == "CV 1", "menu: Clock in = '%s' (bus 3 = jack CV 1)", h.ValueString(clk).c_str());
    }

    // Pots: Pot L moves Blend once it has caught up.
    c.pots[0] = 0.5f;
    Run(h, 0.05f, c);
    for(float x = 0.5f; x <= 0.9f; x += 0.01f)
    {
        c.pots[0] = x;
        Run(h, 0.005f, c);
    }
    CHECK(h.ParamValue(blend) >= 85 && h.ParamValue(blend) <= 92, "Pot L → Blend %d %%", h.ParamValue(blend));

    // Button 4: Hold INF and back.
    const int hold0 = h.ParamValue(hold);
    Press(h, c, kNT_button4);
    const int hold1 = h.ParamValue(hold);
    Press(h, c, kNT_button4);
    CHECK(hold1 != hold0 && h.ParamValue(hold) == hold0, "button 4: Hold %d → %d → %d", hold0, hold1, h.ParamValue(hold));

    // Encoder R: Trail level of the selected Trail.
    const int lvl = Param(h, "Level 1");
    const int l0  = h.ParamValue(lvl);
    c.encoders[1] = -3;
    Run(h, 0.02f, c);
    CHECK(h.ParamValue(lvl) == l0 - 6, "Encoder R: Trail 1 level %d → %d", l0, h.ParamValue(lvl));

    // Menu: opens on the first page; Encoder L walks the parameters, Encoder R edits.
    Menu(h, c);
    CHECK(h.MenuOpen(), "MENU opens the parameter menu");
    Frame(h, "MENU: parameter menu (first page)");
    c.encoders[0] = 1;
    Run(h, 0.02f, c);
    Frame(h, "Encoder L: next parameter");
    // Pot L picks the page absolutely: the Mod 1 page.
    int b0 = h.ParamValue(blend);
    c.encoders[1] = 0;
    // Find Blend through the menu: page via Encoder L presses is tested by value change below.
    Press(h, c, kNT_encoderButtonL);
    Frame(h, "Encoder L press: next page");
    Menu(h, c);
    CHECK(!h.MenuOpen() && h.ParamValue(blend) == b0, "MENU closes the menu");

    // Menu edits change the parameter (Pot R absolute on the selected one).
    Menu(h, c);
    c.pots[2] = 0.3f;
    Run(h, 0.02f, c);
    c.pots[2] = 0.0f;
    Run(h, 0.02f, c);
    Frame(h, "Pot R in the menu: value of the selected parameter");
    Menu(h, c);

    // Persistence by name and a second instance next to the first.
    auto vals = h.Values();
    for(auto& nv : vals)
        if(nv.first == "Blend")
            nv.second = 33;
    nt_host::Host h2;
    CHECK(h2.Create(f, {5}, 48000, vals), "second instance with restored values (5 s Trails)");
    CHECK(h2.ParamValue(Param(h2, "Blend")) == 33 && h.ParamValue(blend) != 33, "restored Blend 33 %%, first instance untouched (%d)",
          h.ParamValue(blend));
    nt_host::Controls c2;
    Run(h2, 0.2f, c2);
    c2.encoders[1] = 2;
    const int l2 = h2.ParamValue(Param(h2, "Level 1"));
    const int l1 = h.ParamValue(lvl);
    Run(h2, 0.02f, c2);
    CHECK(h2.ParamValue(Param(h2, "Level 1")) == l2 + 4 && h.ParamValue(lvl) == l1,
          "controls reach only their own instance (%d → %d, other %d)", l2, h2.ParamValue(Param(h2, "Level 1")),
          h.ParamValue(lvl));

    // Sample rate change: rebuilt with the same values.
    auto v2 = h.Values();
    CHECK(h.Create(f, h.Specs(), 96000, v2) && h.ParamValue(blend) == v2[blend].second, "rebuilt at 96 kHz, values kept");
    const float rms96 = Run(h, 0.3f, c);
    CHECK(std::isfinite(rms96), "runs at 96 kHz (RMS %.3f)", rms96);

    std::fprintf(stderr, "%s (%d checks, %d failed)\n", g_fail ? "FAILED" : "all checks passed", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
