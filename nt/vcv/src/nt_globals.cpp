// NT_globals, writable: the plug-in sees `extern const _NT_globals`, the host
// sets the sample rate. Deliberately without distingnt/api.h (that header
// declares the object const); the layout is the API's _NT_globals.
#include <cstdint>

namespace
{
struct GlobalsLayout
{
    uint32_t sampleRate;
    uint32_t maxFramesPerStep;
    float*   workBuffer;
    uint32_t workBufferSizeBytes;
    uint32_t streamSizeBytes;
    uint32_t streamBufferSizeBytes;
};
} // namespace

extern "C" {
GlobalsLayout NT_globals = {48000, 16, nullptr, 0, 0, 0};
}

namespace nt_host
{
void SetSampleRate(uint32_t sr) { NT_globals.sampleRate = sr; }
void SetMaxFrames(uint32_t n) { NT_globals.maxFramesPerStep = n; }
} // namespace nt_host
