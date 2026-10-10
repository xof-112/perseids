// Pixel-exact NT screen for the renderer: draws into NT_screen exactly as
// nt_emu does, so the screenshots in the guide are what nt_emu (and the NT)
// shows. Text and shapes follow nt_emu's vcv-plugin/src/fonts_vcv.cpp and
// vcv-plugin/src/api/NTApiWrapper.cpp (MIT, (c) 2026 Neal Sanche) with the
// fonts from NT_EMU_PATH/fonts.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "pixelmix_baseline.h"
#include "selawik_aa.h"
#include "tom_thumb_4x6.h"

namespace nt_raster
{
constexpr int W = 256, H = 64;

inline void Pixel(int x, int y, int c)
{
    if(x < 0 || x >= W || y < 0 || y >= H)
        return;
    const int i = y * 128 + x / 2;
    c &= 0x0F;
    NT_screen[i] = (x & 1) ? static_cast<uint8_t>((NT_screen[i] & 0xF0) | c)
                           : static_cast<uint8_t>((NT_screen[i] & 0x0F) | (c << 4));
}

inline int OutCode(int x, int y)
{
    int code = 0;
    if(x < 0)
        code |= 1;
    else if(x >= W)
        code |= 2;
    if(y < 0)
        code |= 4;
    else if(y >= H)
        code |= 8;
    return code;
}

inline bool Clip(int& x0, int& y0, int& x1, int& y1)
{
    int o0 = OutCode(x0, y0), o1 = OutCode(x1, y1);
    while(true)
    {
        if(!(o0 | o1))
            return true;
        if(o0 & o1)
            return false;
        const int out = o1 > o0 ? o1 : o0;
        int       x = 0, y = 0;
        if(out & 8)
        {
            x = x0 + (x1 - x0) * (H - 1 - y0) / (y1 - y0);
            y = H - 1;
        }
        else if(out & 4)
        {
            x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0);
            y = 0;
        }
        else if(out & 2)
        {
            y = y0 + (y1 - y0) * (W - 1 - x0) / (x1 - x0);
            x = W - 1;
        }
        else if(out & 1)
        {
            y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
            x = 0;
        }
        if(out == o0)
        {
            x0 = x;
            y0 = y;
            o0 = OutCode(x0, y0);
        }
        else
        {
            x1 = x;
            y1 = y;
            o1 = OutCode(x1, y1);
        }
    }
}

inline void Line(int x0, int y0, int x1, int y1, int c)
{
    if(!Clip(x0, y0, x1, y1))
        return;
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int       err = dx - dy;
    while(true)
    {
        Pixel(x0, y0, c);
        if(x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if(e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if(e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

inline void Shape(_NT_shape sh, int x0, int y0, int x1, int y1, int c)
{
    switch(sh)
    {
        case kNT_line: Line(x0, y0, x1, y1, c); break;
        case kNT_box:
            Line(x0, y0, x1, y0, c);
            Line(x1, y0, x1, y1, c);
            Line(x1, y1, x0, y1, c);
            Line(x0, y1, x0, y0, c);
            break;
        case kNT_rectangle:
        {
            const int cx0 = std::max(0, std::min(x0, x1)), cy0 = std::max(0, std::min(y0, y1));
            const int cx1 = std::min(W - 1, std::max(x0, x1)), cy1 = std::min(H - 1, std::max(y0, y1));
            for(int y = cy0; y <= cy1; ++y)
                Line(cx0, y, cx1, y, c);
            break;
        }
        default: Pixel(x0, y0, c); break;
    }
}

inline int CharWidth(char ch, _NT_textSize z)
{
    const int i = static_cast<unsigned char>(ch) - 32;
    if(z == kNT_textTiny)
        return 4;
    if(i < 0 || i > 94)
        return 0;
    return z == kNT_textLarge ? fonts::selawik_aaWidths[i] : fonts::pixelmix_baselineWidths[i];
}

inline void Char(int x, int y, char ch, _NT_textSize z, int c)
{
    if(c < 0 || c > 15)
        c = 15;
    const int i = static_cast<unsigned char>(ch) - 32;
    if(i < 0 || i > 94)
        return;
    if(z == kNT_textTiny)
    {
        const int top = y - 5;
        for(int r = 0; r < 6; ++r)
            for(int col = 0; col < 4; ++col)
                if(fonts::tomThumb4x6Font[i][r] & (0x80 >> col))
                    Pixel(x + col + 2, top + r, c); // nt_emu: +2 px left bearing
    }
    else if(z == kNT_textNormal)
    {
        const int top = y - 8;
        for(int r = 0; r < fonts::PIXELMIX_BASELINE_HEIGHT; ++r)
            for(int col = 0; col < 8; ++col)
                if(fonts::pixelmix_baselineFont[i][r] & (0x80 >> col))
                    Pixel(x + col, top + r, c);
    }
    else
    {
        constexpr int BW  = fonts::SELAWIK_AA_WIDTH;
        const int     top = y - fonts::SELAWIK_AA_ASCENT;
        for(int r = 0; r < fonts::SELAWIK_AA_HEIGHT; ++r)
            for(int col = 0; col < BW; ++col)
            {
                const int g = fonts::selawik_aaFont[i][r * BW + col];
                if(g > 0)
                    Pixel(x + col, top + r, (g * c + 7) / 15);
            }
    }
}

inline void Text(int x, int y, const char* s, int c, _NT_textAlignment al, _NT_textSize z)
{
    if(!s || x < 0 || x >= W || y < -20 || y > 80)
        return; // nt_emu draws nothing then
    int w = 0;
    for(const char* p = s; *p; ++p)
        w += CharWidth(*p, z);
    int x0 = al == kNT_textCentre ? x - w / 2 : (al == kNT_textRight ? x - w : x);
    for(; *s; ++s)
    {
        Char(x0, y, *s, z, c);
        x0 += CharWidth(*s, z);
    }
}
} // namespace nt_raster
