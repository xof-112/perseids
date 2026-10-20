#include <utility>
// NT API stubs for the native simulation. Text widths use nt_emu's font
// tables (NT_EMU_PATH/fonts), so "fits on the screen" means what nt_emu shows.
#include "stubs.h"

#include <cstdio>
#include <cstring>

#include "pixelmix_baseline.h"
#include "selawik_aa.h"
#ifdef RENDER_LOG
#include "../render/nt_raster.h" // pixel-exact NT_screen like nt_emu
#endif

const _NT_globals NT_globals = {48000, 128, nullptr, 0, 0, 0};
uint8_t           NT_screen[128 * 64];

int16_t        g_params[256];
int            g_draws    = 0;
int            g_textOob  = 0;
int            g_shapeOob = 0;
_NT_algorithm* g_alg      = nullptr;
void (*g_parameterChanged)(_NT_algorithm*, int) = nullptr;
bool g_logDraw = false;

static uint32_t s_cycles = 0;
uint32_t NT_getCpuCycleCount(void) { return s_cycles += 1000; }
int32_t  NT_algorithmIndex(const _NT_algorithm*) { return 0; }
uint32_t NT_parameterOffset(void) { return 0; }
void     NT_requestSetupUi(void) {}
int      g_paramDefUpdates = 0;
void     NT_updateParameterDefinition(uint32_t, uint32_t) { ++g_paramDefUpdates; }
int      g_pageUpdates = 0;
void     NT_updateParameterPages(uint32_t) { ++g_pageUpdates; }

void NT_setParameterFromUi(uint32_t, uint32_t p, int16_t v)
{
    g_params[p] = v;
    if(g_parameterChanged && g_alg)
        g_parameterChanged(g_alg, static_cast<int>(p));
}
void NT_setParameterFromAudio(uint32_t i, uint32_t p, int16_t v)
{
    NT_setParameterFromUi(i, p, v);
}

static int textWidth(const char* s, _NT_textSize z)
{
    int w = 0;
    for(; *s; ++s)
    {
        int c = static_cast<unsigned char>(*s) - 32;
        if(c < 0 || c > 94)
            c = 0;
        w += z == kNT_textTiny
                 ? 4
                 : (z == kNT_textLarge ? fonts::selawik_aaWidths[c]
                                       : fonts::pixelmix_baselineWidths[c]);
    }
    return w;
}

void NT_drawText(int x, int y, const char* s, int c, _NT_textAlignment al, _NT_textSize z)
{
    ++g_draws;
    if(g_logDraw)
        printf("T %d %d %d %d %d %s\n", x, y, c, static_cast<int>(al), static_cast<int>(z), s);
#ifdef RENDER_LOG
    nt_raster::Text(x, y, s, c, al, z);
#endif
    const int w   = textWidth(s, z);
    const int x0  = al == kNT_textCentre ? x - w / 2 : (al == kNT_textRight ? x - w : x);
    const int asc = z == kNT_textTiny ? 5 : (z == kNT_textLarge ? 18 : 8);
    if(x0 < 0 || x0 + w > 256 || y - asc < -1 || y > 64)
    {
        if(!g_logDraw)
            printf("TEXT OOB '%s' x0=%d w=%d y=%d\n", s, x0, w, y);
        ++g_textOob;
    }
}

void NT_drawShapeI(_NT_shape sh, int x0, int y0, int x1, int y1, int c)
{
    ++g_draws;
    if(g_logDraw)
        printf("S %d %d %d %d %d %d\n", static_cast<int>(sh), x0, y0, x1, y1, c);
#ifdef RENDER_LOG
    nt_raster::Shape(sh, x0, y0, x1, y1, c);
#endif
    if(sh == kNT_line) // lines may run in any direction
    {
        if(x1 < x0) std::swap(x0, x1);
        if(y1 < y0) std::swap(y0, y1);
    }
    if(x0 < 0 || x1 > 255 || y0 < 0 || y1 > 63 || x1 < x0 || y1 < y0)
    {
        if(!g_logDraw)
            printf("SHAPE OOB %d %d %d %d\n", x0, y0, x1, y1);
        ++g_shapeOob;
    }
}

int NT_intToString(char* b, int32_t v) { return sprintf(b, "%d", static_cast<int>(v)); }
