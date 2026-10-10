// Minimal disting NT host for native tests: parameter table, bus memory,
// drawing checks (text must fit the 256×64 screen).
#pragma once

#include <distingnt/api.h>

#include <cstdint>

extern int16_t  g_params[256];
extern int      g_draws;
extern int      g_textOob;
extern int      g_shapeOob;
extern _NT_algorithm* g_alg;
extern int      g_paramDefUpdates;
extern void (*g_parameterChanged)(_NT_algorithm*, int);
extern bool     g_logDraw; // print every draw call (renderer)
