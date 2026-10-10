// Brücke zwischen einem disting-NT-Plug-in und dem Windows-Build von nt_emu.
// (Übernommen aus disting-nt-plugins/duett-nt/emu, Präfix perseids_, plus NT_requestSetupUi.)
// Sucht im laufenden Prozess das Modul, das die NT-API exportiert (nt_emu, mit
// --export-all-symbols gebaut), und leitet jede API-Funktion dorthin weiter.
#include <windows.h>
#include <psapi.h>
#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <distingnt/wav.h>
#include <distingnt/microtuning.h>

static _NT_globals s_dummyGlobals = { 48000, 128, 0, 0, 0, 0 };
static uint8_t s_dummyScreen[128*64];

extern "C" {
const _NT_globals* perseids_NT_globals = &s_dummyGlobals;
uint8_t (*perseids_NT_screen)[128*64] = &s_dummyScreen;
}

static HMODULE g_ntHost = NULL;

static HMODULE findHost()
{
	HMODULE mods[1024];
	DWORD needed = 0;
	if ( !EnumProcessModules( GetCurrentProcess(), mods, sizeof(mods), &needed ) )
		return NULL;
	for ( DWORD i=0; i < needed / sizeof(HMODULE) && i < 1024; ++i )
		if ( GetProcAddress( mods[i], "NT_drawText" ) && GetProcAddress( mods[i], "NT_globals" ) )
			return mods[i];
	return NULL;
}

template <typename F> static F sym( const char* name )
{
	return g_ntHost ? (F)(void*)GetProcAddress( g_ntHost, name ) : (F)NULL;
}

#define FWD( ret, name, params, args, fallback ) \
	static ret (*p_##name) params = NULL; \
	extern "C" ret name params { return p_##name ? p_##name args : fallback; }
#define FWDV( name, params, args ) \
	static void (*p_##name) params = NULL; \
	extern "C" void name params { if ( p_##name ) p_##name args; }

FWD( uint32_t, NT_getCpuCycleCount, (void), (), (uint32_t)GetTickCount() )
FWD( int32_t, NT_algorithmIndex, (const _NT_algorithm* a), (a), 0 )
FWD( uint32_t, NT_parameterOffset, (void), (), 0 )
FWDV( NT_setParameterFromAudio, (uint32_t i, uint32_t p, int16_t v), (i, p, v) )
FWDV( NT_setParameterFromUi, (uint32_t i, uint32_t p, int16_t v), (i, p, v) )
FWDV( NT_setParameterGrayedOut, (uint32_t i, uint32_t p, bool g), (i, p, g) )
FWDV( NT_updateParameterDefinition, (uint32_t i, uint32_t p), (i, p) )
FWDV( NT_updateParameterPages, (uint32_t i), (i) )
FWDV( NT_requestSetupUi, (void), () )
FWDV( NT_drawText, (int x, int y, const char* s, int c, _NT_textAlignment a, _NT_textSize z), (x, y, s, c, a, z) )
FWDV( NT_drawShapeI, (_NT_shape s, int x0, int y0, int x1, int y1, int c), (s, x0, y0, x1, y1, c) )
FWDV( NT_drawShapeF, (_NT_shape s, float x0, float y0, float x1, float y1, float c), (s, x0, y0, x1, y1, c) )
FWD( int, NT_intToString, (char* b, int32_t v), (b, v), 0 )
FWD( int, NT_floatToString, (char* b, float v, int d), (b, v, d), 0 )
FWDV( NT_sendMidiByte, (uint32_t d, uint8_t b0), (d, b0) )
FWDV( NT_sendMidi2ByteMessage, (uint32_t d, uint8_t b0, uint8_t b1), (d, b0, b1) )
FWDV( NT_sendMidi3ByteMessage, (uint32_t d, uint8_t b0, uint8_t b1, uint8_t b2), (d, b0, b1, b2) )

// SD-Karte: Samples und Scala-Dateien (in nt_emu als virtuelle SD-Karte umgesetzt)
FWD( bool, NT_isSdCardMounted, (void), (), false )
FWD( uint32_t, NT_getNumSampleFolders, (void), (), 0 )
FWDV( NT_getSampleFolderInfo, (uint32_t f, _NT_wavFolderInfo& i), (f, i) )
FWDV( NT_getSampleFileInfo, (uint32_t f, uint32_t s, _NT_wavInfo& i), (f, s, i) )
FWD( bool, NT_readSampleFrames, (const _NT_wavRequest& r), (r), false )
FWD( uint32_t, NT_getNumScl, (void), (), 0 )
FWDV( NT_getSclInfo, (uint32_t i, _NT_sclInfo& n), (i, n) )
FWD( bool, NT_readScl, (_NT_sclRequest& r), (r), false )

// JSON-Klassen: Mitgliedsfunktionen werden als Funktion mit this als erstem Argument aufgerufen
// (gleiche MinGW-x64-ABI auf beiden Seiten).
static void (*s_openArray)( _NT_jsonStream* ) = NULL;
static void (*s_closeArray)( _NT_jsonStream* ) = NULL;
static void (*s_openObject)( _NT_jsonStream* ) = NULL;
static void (*s_closeObject)( _NT_jsonStream* ) = NULL;
static void (*s_addMemberName)( _NT_jsonStream*, const char* ) = NULL;
static void (*s_addNumberI)( _NT_jsonStream*, int ) = NULL;
static void (*s_addNumberF)( _NT_jsonStream*, float ) = NULL;
static void (*s_addString)( _NT_jsonStream*, const char* ) = NULL;
static void (*s_addFourCC)( _NT_jsonStream*, uint32_t ) = NULL;
static void (*s_addBoolean)( _NT_jsonStream*, bool ) = NULL;
static void (*s_addNull)( _NT_jsonStream* ) = NULL;
static bool (*s_numArr)( _NT_jsonParse*, int& ) = NULL;
static bool (*s_numObj)( _NT_jsonParse*, int& ) = NULL;
static bool (*s_match)( _NT_jsonParse*, const char* ) = NULL;
static bool (*s_skip)( _NT_jsonParse* ) = NULL;
static bool (*s_numI)( _NT_jsonParse*, int& ) = NULL;
static bool (*s_numF)( _NT_jsonParse*, float& ) = NULL;
static bool (*s_bool)( _NT_jsonParse*, bool& ) = NULL;

void _NT_jsonStream::openArray(void)				{ if ( s_openArray ) s_openArray( this ); }
void _NT_jsonStream::closeArray(void)				{ if ( s_closeArray ) s_closeArray( this ); }
void _NT_jsonStream::openObject(void)				{ if ( s_openObject ) s_openObject( this ); }
void _NT_jsonStream::closeObject(void)				{ if ( s_closeObject ) s_closeObject( this ); }
void _NT_jsonStream::addMemberName( const char* s )	{ if ( s_addMemberName ) s_addMemberName( this, s ); }
void _NT_jsonStream::addNumber( int v )				{ if ( s_addNumberI ) s_addNumberI( this, v ); }
void _NT_jsonStream::addNumber( float v )			{ if ( s_addNumberF ) s_addNumberF( this, v ); }
void _NT_jsonStream::addString( const char* s )		{ if ( s_addString ) s_addString( this, s ); }
void _NT_jsonStream::addFourCC( uint32_t v )		{ if ( s_addFourCC ) s_addFourCC( this, v ); }
void _NT_jsonStream::addBoolean( bool v )			{ if ( s_addBoolean ) s_addBoolean( this, v ); }
void _NT_jsonStream::addNull(void)					{ if ( s_addNull ) s_addNull( this ); }
bool _NT_jsonParse::numberOfArrayElements( int& n )	{ return s_numArr && s_numArr( this, n ); }
bool _NT_jsonParse::numberOfObjectMembers( int& n )	{ return s_numObj && s_numObj( this, n ); }
bool _NT_jsonParse::matchName( const char* s )		{ return s_match && s_match( this, s ); }
bool _NT_jsonParse::skipMember(void)				{ return s_skip && s_skip( this ); }
bool _NT_jsonParse::number( int& v )				{ return s_numI && s_numI( this, v ); }
bool _NT_jsonParse::number( float& v )				{ return s_numF && s_numF( this, v ); }
bool _NT_jsonParse::boolean( bool& v )				{ return s_bool && s_bool( this, v ); }
bool _NT_jsonParse::string( const char*& s )		{ s = ""; return false; }		// in nt_emu nicht exportiert
bool _NT_jsonParse::null(void)						{ return false; }

static void resolve()
{
	if ( g_ntHost )
		return;
	g_ntHost = findHost();
	if ( !g_ntHost )
		return;
	if ( const _NT_globals* g = sym<const _NT_globals*>( "NT_globals" ) ) perseids_NT_globals = g;
	typedef uint8_t ScreenT[128*64];
	ScreenT* scr = sym<ScreenT*>( "NT_screen" );
	if ( scr ) perseids_NT_screen = scr;
#define R( name ) p_##name = sym<decltype(p_##name)>( #name )
	R( NT_getCpuCycleCount ); R( NT_algorithmIndex ); R( NT_parameterOffset );
	R( NT_setParameterFromAudio ); R( NT_setParameterFromUi ); R( NT_setParameterGrayedOut );
	R( NT_updateParameterDefinition ); R( NT_updateParameterPages ); R( NT_requestSetupUi );
	R( NT_drawText ); R( NT_drawShapeI ); R( NT_drawShapeF ); R( NT_intToString ); R( NT_floatToString );
	R( NT_sendMidiByte ); R( NT_sendMidi2ByteMessage ); R( NT_sendMidi3ByteMessage );
	R( NT_isSdCardMounted ); R( NT_getNumSampleFolders ); R( NT_getSampleFolderInfo ); R( NT_getSampleFileInfo );
	R( NT_readSampleFrames ); R( NT_getNumScl ); R( NT_getSclInfo ); R( NT_readScl );
#undef R
#define M( var, mangled ) var = sym<decltype(var)>( mangled )
	M( s_openArray, "_ZN14_NT_jsonStream9openArrayEv" );
	M( s_closeArray, "_ZN14_NT_jsonStream10closeArrayEv" );
	M( s_openObject, "_ZN14_NT_jsonStream10openObjectEv" );
	M( s_closeObject, "_ZN14_NT_jsonStream11closeObjectEv" );
	M( s_addMemberName, "_ZN14_NT_jsonStream13addMemberNameEPKc" );
	M( s_addNumberI, "_ZN14_NT_jsonStream9addNumberEi" );
	M( s_addNumberF, "_ZN14_NT_jsonStream9addNumberEf" );
	M( s_addString, "_ZN14_NT_jsonStream9addStringEPKc" );
	M( s_addFourCC, "_ZN14_NT_jsonStream9addFourCCEj" );
	M( s_addBoolean, "_ZN14_NT_jsonStream10addBooleanEb" );
	M( s_addNull, "_ZN14_NT_jsonStream7addNullEv" );
	M( s_numArr, "_ZN13_NT_jsonParse21numberOfArrayElementsERi" );
	M( s_numObj, "_ZN13_NT_jsonParse21numberOfObjectMembersERi" );
	M( s_match, "_ZN13_NT_jsonParse9matchNameEPKc" );
	M( s_skip, "_ZN13_NT_jsonParse10skipMemberEv" );
	M( s_numI, "_ZN13_NT_jsonParse6numberERi" );
	M( s_numF, "_ZN13_NT_jsonParse6numberERf" );
	M( s_bool, "_ZN13_NT_jsonParse7booleanERb" );
#undef M
}

// Der einzige Export dieser DLL: löst beim ersten Aufruf alles auf und reicht dann weiter
#undef pluginEntry
extern "C" __declspec(dllexport) uintptr_t pluginEntry( _NT_selector selector, uint32_t data )
{
	resolve();
	return perseids_pluginEntry( selector, data );
}
