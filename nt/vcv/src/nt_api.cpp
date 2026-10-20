// The disting NT API as the plug-in sees it, implemented on top of nt_host.
// Calls go to the host whose algorithm runs on this thread, or the one the
// index / algorithm names, so several modules never mix.
#include <cstdio>
#include <cstring>

#include "nt_host.hpp"

#define NT_RASTER_SCREEN (::nt_host::Current()->Screen())
#include "nt_raster.h"

using nt_host::Host;

extern "C" {
uint8_t NT_screen[128 * 64]; // not used by Perseids; the host draws into its own screen

void NT_drawText(int x, int y, const char* str, int colour, _NT_textAlignment align, _NT_textSize size)
{
    if(nt_host::Current())
        nt_raster::Text(x, y, str, colour, align, size);
}

void NT_drawShapeI(_NT_shape shape, int x0, int y0, int x1, int y1, int colour)
{
    if(nt_host::Current())
        nt_raster::Shape(shape, x0, y0, x1, y1, colour);
}

void NT_drawShapeF(_NT_shape shape, float x0, float y0, float x1, float y1, float colour)
{
    NT_drawShapeI(shape, static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1), static_cast<int>(y1),
                  static_cast<int>(colour));
}

int NT_intToString(char* buffer, int32_t value) { return std::sprintf(buffer, "%d", static_cast<int>(value)); }

int NT_floatToString(char* buffer, float value, int decimalPlaces)
{
    return std::sprintf(buffer, "%.*f", decimalPlaces, static_cast<double>(value));
}

uint32_t NT_getCpuCycleCount(void) { return 0; }

int32_t NT_algorithmIndex(const _NT_algorithm* algorithm)
{
    return nt_host::IndexOf(nt_host::ByAlgorithm(algorithm));
}

uint32_t NT_algorithmCount(void) { return 1; }
uint32_t NT_parameterOffset(void) { return 0; }

void NT_setParameterFromAudio(uint32_t algorithmIndex, uint32_t parameter, int16_t value)
{
    if(Host* h = nt_host::ByIndex(static_cast<int>(algorithmIndex)))
        h->SetFromAudio(static_cast<int>(parameter), value);
}

void NT_setParameterFromUi(uint32_t algorithmIndex, uint32_t parameter, int16_t value)
{
    if(Host* h = nt_host::ByIndex(static_cast<int>(algorithmIndex)))
        h->SetParam(static_cast<int>(parameter), value);
}

void NT_setParameterGrayedOut(uint32_t, uint32_t, bool) {}
void NT_updateParameterDefinition(uint32_t, uint32_t) {} // the menu reads the definitions live
void NT_updateParameterPages(uint32_t) {}                 // same for the pages

void NT_requestSetupUi(void)
{
    if(Host* h = nt_host::Current())
        h->RequestSetupUi();
}

void NT_setParameterRange(_NT_parameter*, float, float, float, float) {}

void NT_sendMidiByte(uint32_t, uint8_t) {}
void NT_sendMidi2ByteMessage(uint32_t, uint8_t, uint8_t) {}
void NT_sendMidi3ByteMessage(uint32_t, uint8_t, uint8_t, uint8_t) {}
void NT_sendMidiSysEx(uint32_t, const uint8_t*, uint32_t, bool) {}
}
