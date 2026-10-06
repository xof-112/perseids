---
description: Perseids shared core — portability rules for the Daisy firmware and the VCV Rack port
alwaysApply: true
---
Perseids builds for two platforms from one core: the Daisy firmware (src/, PlatformIO) and a
VCV Rack 2 plugin (vcv/). Engines, ParameterRegistry, mod system, page model, controller and
renderer are shared source — never copy or fork them into vcv/.

1. The firmware must build and behave identically after every change.
2. Platform differences go through seams (memory arena, tick source, display, panel I/O,
   analog I/O). `#ifdef PERSEIDS_VCV` only in platform-layer files, never in shared core.
3. No mutable file-scope or static state reachable from the core — everything lives in the
   PerseidsCore instance, so multiple VCV instances never share state.
4. Audio path: no allocation, locks, logging or string formatting. Allocate once at
   construction.
5. Registry IDs are stable; persistence keys use the string ID, never the index.
6. Panel coordinates come only from panel_def.py via the generated header — never typed by
   hand.
7. Never guess Rack SDK, libDaisy or DaisySP APIs — grep the headers first.
8. Do not edit ARCHITECTURE.md or PANEL_and_PINOUT.md; report MATCHES / MISMATCHES / OPEN
   QUESTIONS instead.
