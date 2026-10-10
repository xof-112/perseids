// Wird beim Windows-Build für nt_emu vor jeder Quelle eingebunden (-include).
// Unter Windows kann eine DLL keine offenen Symbole vom Host "erben" wie unter macOS/Linux.
// Deshalb werden die beiden Daten-Symbole der NT-API auf Zeiger umgelenkt, die die Brücke
// (nt_win_shim.cpp) beim ersten pluginEntry()-Aufruf auf die Exporte von nt_emu setzt.
#pragma once
#define NT_globals	(*perseids_NT_globals)
#define NT_screen	(*perseids_NT_screen)
#define pluginEntry	perseids_pluginEntry
#define NT_EMU_WIN 1
