#include "nt_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#define NT_RASTER_SCREEN (::nt_host::Current()->Screen())
#include "nt_raster.h"

namespace nt_host
{
void SetMaxFrames(uint32_t n);
}

namespace nt_host
{
// --- registry: NT_algorithmIndex() and friends find their host here -----------
namespace
{
constexpr int          kMaxHosts = 256;
std::atomic<Host*>     g_hosts[kMaxHosts];
std::mutex             g_reg;
thread_local Host*     g_current = nullptr;

struct Guard
{
    Host* prev;
    explicit Guard(Host* h) : prev(g_current) { g_current = h; }
    ~Guard() { g_current = prev; }
};

void* AllocZero(uint32_t bytes)
{
    if(bytes == 0)
        return nullptr;
    const size_t n = (static_cast<size_t>(bytes) + 63) & ~static_cast<size_t>(63);
    void*        p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(n, 64);
#else
    if(posix_memalign(&p, 64, n) != 0)
        p = nullptr;
#endif
    if(p)
        std::memset(p, 0, n);
    return p;
}

void FreeAligned(void* p)
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

Host* Current() { return g_current; }

Host* ByAlgorithm(const _NT_algorithm* alg)
{
    if(g_current && g_current->Alg() == alg)
        return g_current;
    for(auto& h : g_hosts)
    {
        Host* p = h.load(std::memory_order_acquire);
        if(p && p->Alg() == alg)
            return p;
    }
    return g_current; // during construct() the algorithm is not known yet
}

Host* ByIndex(int i)
{
    if(i < 0 || i >= kMaxHosts)
        return g_current;
    Host* h = g_hosts[i].load(std::memory_order_acquire);
    return h ? h : g_current;
}

int IndexOf(const Host* h)
{
    for(int i = 0; i < kMaxHosts; ++i)
        if(g_hosts[i].load(std::memory_order_acquire) == h)
            return i;
    return -1;
}

// --- host ---------------------------------------------------------------------
Host::Host()
{
    std::lock_guard<std::mutex> l(g_reg);
    for(int i = 0; i < kMaxHosts; ++i)
    {
        Host* expected = nullptr;
        if(g_hosts[i].compare_exchange_strong(expected, this))
        {
            slot_ = i;
            break;
        }
    }
}

Host::~Host()
{
    Destroy();
    std::lock_guard<std::mutex> l(g_reg);
    if(slot_ >= 0)
        g_hosts[slot_].store(nullptr);
}

bool Host::Create(const _NT_factory* factory, std::vector<int32_t> specs, uint32_t sample_rate,
                  const std::vector<std::pair<std::string, int>>& values)
{
    Destroy();
    SetSampleRate(sample_rate);
    SetMaxFrames(kBlockFrames);
    factory_ = factory;
    // Missing specifications take their defaults.
    for(uint32_t k = static_cast<uint32_t>(specs.size()); k < factory->numSpecifications; ++k)
        specs.push_back(factory->specifications[k].def);
    for(uint32_t k = 0; k < factory->numSpecifications; ++k)
        specs[k] = std::max(factory->specifications[k].min, std::min(factory->specifications[k].max, specs[k]));
    specs_ = specs;

    Guard                     g(this);
    _NT_algorithmRequirements req{};
    factory->calculateRequirements(req, specs_.data());
    mem_[0] = AllocZero(req.sram);
    mem_[1] = AllocZero(req.dram);
    mem_[2] = AllocZero(req.dtc);
    mem_[3] = AllocZero(req.itc);
    if((req.sram && !mem_[0]) || (req.dram && !mem_[1]) || (req.dtc && !mem_[2]) || (req.itc && !mem_[3]))
    {
        Destroy();
        return false;
    }
    values_.assign(req.numParameters, 0);
    audio_changed_.assign(req.numParameters, 0);
    _NT_algorithmMemoryPtrs ptrs{static_cast<uint8_t*>(mem_[0]), static_cast<uint8_t*>(mem_[1]),
                                 static_cast<uint8_t*>(mem_[2]), static_cast<uint8_t*>(mem_[3])};
    alg_ = factory->construct(ptrs, req, specs_.data());
    if(!alg_)
    {
        Destroy();
        return false;
    }
    names_.clear();
    for(int p = 0; p < NumParams(); ++p)
    {
        values_[p] = alg_->parameters[p].def;
        names_.emplace_back(alg_->parameters[p].name ? alg_->parameters[p].name : "");
    }
    for(const auto& nv : values)
        for(int p = 0; p < NumParams(); ++p)
            if(nv.first == names_[p])
            {
                const _NT_parameter& d = alg_->parameters[p];
                values_[p]             = static_cast<int16_t>(std::max<int>(d.min, std::min<int>(d.max, nv.second)));
                break;
            }
    alg_->v = alg_->vIncludingCommon = values_.data();
    if(factory_->parameterChanged)
        for(int p = 0; p < NumParams(); ++p)
            factory_->parameterChanged(alg_, p);
    custom_    = factory_->hasCustomUi ? factory_->hasCustomUi(alg_) : 0;
    pos_       = 0;
    setup_ui_  = true;
    menu_open_ = false;
    std::memset(in_, 0, sizeof(in_));
    std::memset(out_, 0, sizeof(out_));
    return true;
}

void Host::Destroy()
{
    alg_ = nullptr; // NT plug-ins have no destructor: the memory is simply released
    for(auto& m : mem_)
    {
        FreeAligned(m);
        m = nullptr;
    }
    values_.clear();
    names_.clear();
    audio_changed_.clear();
}

const char* Host::ParamName(int p) const { return p >= 0 && p < static_cast<int>(names_.size()) ? names_[p].c_str() : ""; }

void Host::SetParam(int p, int v)
{
    if(!alg_ || p < 0 || p >= NumParams())
        return;
    const _NT_parameter& d = alg_->parameters[p];
    values_[p]             = static_cast<int16_t>(std::max<int>(d.min, std::min<int>(d.max, v)));
    Guard g(this);
    if(factory_->parameterChanged)
        factory_->parameterChanged(alg_, p);
}

void Host::SetFromAudio(int p, int v)
{
    if(!alg_ || p < 0 || p >= NumParams())
        return;
    const _NT_parameter& d = alg_->parameters[p];
    values_[p]             = static_cast<int16_t>(std::max<int>(d.min, std::min<int>(d.max, v)));
    audio_changed_[p]      = 1;
}

void Host::FlushAudioChanges()
{
    for(int p = 0; p < NumParams(); ++p)
        if(audio_changed_[p])
        {
            audio_changed_[p] = 0;
            if(factory_->parameterChanged)
                factory_->parameterChanged(alg_, p);
        }
}

void Host::ResetParams()
{
    if(!alg_)
        return;
    for(int p = 0; p < NumParams(); ++p)
        SetParam(p, alg_->parameters[p].def);
}

std::vector<std::pair<std::string, int>> Host::Values() const
{
    std::vector<std::pair<std::string, int>> out;
    for(int p = 0; p < NumParams(); ++p)
        out.emplace_back(names_[p], values_[p]);
    return out;
}

// --- audio ---------------------------------------------------------------------
void Host::Frame(const float in[11], float out[2])
{
    for(int k = 0; k < 11; ++k)
        in_[k][pos_] = in[k];
    out[0] = out_[0][pos_];
    out[1] = out_[1][pos_];
    if(++pos_ == kBlockFrames)
    {
        pos_ = 0;
        Step();
    }
}

void Host::Step()
{
    if(!alg_)
    {
        std::memset(out_, 0, sizeof(out_));
        return;
    }
    std::memset(bus_, 0, sizeof(bus_));
    for(int k = 0; k < 11; ++k)
        std::memcpy(bus_ + k * kBlockFrames, in_[k], sizeof(in_[k]));
    {
        Guard g(this);
        factory_->step(alg_, bus_, kBlockFrames / 4);
        FlushAudioChanges();
    }
    // Output 1/2 = busses 13/14.
    std::memcpy(out_[0], bus_ + 12 * kBlockFrames, sizeof(out_[0]));
    std::memcpy(out_[1], bus_ + 13 * kBlockFrames, sizeof(out_[1]));
}

// --- controls --------------------------------------------------------------------
void Host::CallCustomUi(uint16_t controls, const int8_t enc[2])
{
    _NT_uiData d{};
    for(int k = 0; k < 3; ++k)
        d.pots[k] = pots_[k] < 0.f ? 0.5f : pots_[k];
    d.controls    = controls;
    d.lastButtons = last_buttons_;
    d.encoders[0] = enc[0];
    d.encoders[1] = enc[1];
    Guard g(this);
    factory_->customUi(alg_, d);
    last_buttons_ = controls & 0x01FF; // buttons and presses (bits 0…8)
}

void Host::Ui(const Controls& c)
{
    if(!alg_)
        return;
    const bool menu_press = c.menu && !last_menu_;
    last_menu_            = c.menu;
    if(menu_press)
    {
        menu_open_ = !menu_open_;
        confirm_p_ = -1;
        if(menu_open_)
        {
            page_ = 0; // like the NT and nt_emu: the menu starts at the first page
            item_ = 0;
        }
        else
            setup_ui_ = true;
    }

    bool moved[3];
    for(int k = 0; k < 3; ++k)
    {
        moved[k] = pots_[k] >= 0.f && std::fabs(c.pots[k] - pots_[k]) > 1e-4f;
        pots_[k] = c.pots[k];
    }

    if(menu_open_)
    {
        MenuUi(c, moved);
        last_buttons_ = c.buttons;
        return;
    }
    if(!factory_->customUi || !custom_)
    {
        last_buttons_ = c.buttons;
        return;
    }
    if(setup_ui_ && factory_->setupUi)
    {
        // The host's soft takeover would start here; Perseids catches pots itself.
        _NT_float3 p = {0.f, 0.f, 0.f};
        Guard      g(this);
        factory_->setupUi(alg_, p);
    }
    setup_ui_ = false;

    uint16_t pot_bits = 0;
    if(moved[0])
        pot_bits |= kNT_potL;
    if(moved[1])
        pot_bits |= kNT_potC;
    if(moved[2])
        pot_bits |= kNT_potR;
    pot_bits &= custom_;
    const uint16_t buttons = c.buttons & custom_ & 0x01FF;
    int            e[2]    = {(custom_ & kNT_encoderL) ? c.encoders[0] : 0, (custom_ & kNT_encoderR) ? c.encoders[1] : 0};
    bool           first   = true;
    do
    {
        int8_t d[2];
        for(int i = 0; i < 2; ++i)
        {
            d[i] = static_cast<int8_t>(e[i] > 0 ? 1 : (e[i] < 0 ? -1 : 0));
            e[i] -= d[i];
        }
        const bool btn_change = buttons != (last_buttons_ & custom_);
        if((first && pot_bits) || btn_change || d[0] || d[1])
            CallCustomUi(static_cast<uint16_t>(buttons | (first ? pot_bits : 0)), d);
        first = false;
    } while(e[0] || e[1]);
    last_buttons_ = buttons;
}

// --- parameter menu ------------------------------------------------------------
const _NT_parameterPages* Host::Pages() const { return alg_ ? alg_->parameterPages : nullptr; }

int Host::NumPages() const
{
    const _NT_parameterPages* pp = Pages();
    return pp ? static_cast<int>(pp->numPages) : 1;
}

int Host::PageSize(int page) const
{
    const _NT_parameterPages* pp = Pages();
    if(!pp)
        return NumParams();
    return page >= 0 && page < static_cast<int>(pp->numPages) ? pp->pages[page].numParams : 0;
}

int Host::PageParam(int page, int i) const
{
    const _NT_parameterPages* pp = Pages();
    if(!pp)
        return i;
    const _NT_parameterPage& pg = pp->pages[page];
    return pg.params ? pg.params[i] : i;
}

void Host::ClampMenu()
{
    const int np = NumPages();
    page_        = std::max(0, std::min(np - 1, page_));
    const int n  = PageSize(page_);
    item_        = std::max(0, std::min(n - 1, item_));
}

void Host::MenuUi(const Controls& c, bool moved[3])
{
    ClampMenu();
    const int np = NumPages();
    if(np <= 0)
        return;
    const uint16_t press = c.buttons & ~last_buttons_;
    const int      old_p = PageSize(page_) > 0 ? PageParam(page_, item_) : -1;

    // Encoder L: parameter, on into the next/previous page.
    for(int s = c.encoders[0]; s != 0; s += s > 0 ? -1 : 1)
    {
        item_ += s > 0 ? 1 : -1;
        if(item_ >= PageSize(page_))
        {
            if(page_ < np - 1)
            {
                ++page_;
                item_ = 0;
            }
            else
                item_ = PageSize(page_) - 1;
        }
        else if(item_ < 0)
        {
            if(page_ > 0)
            {
                --page_;
                item_ = PageSize(page_) - 1;
            }
            else
                item_ = 0;
        }
    }
    // Encoder L press: next page. Pot L: page, Pot C: parameter (absolute).
    if(press & kNT_encoderButtonL)
    {
        page_ = (page_ + 1) % np;
        item_ = 0;
    }
    if(moved[0])
    {
        page_ = std::min(np - 1, static_cast<int>(c.pots[0] * np));
        item_ = std::min(item_, std::max(0, PageSize(page_) - 1));
    }
    if(moved[1] && PageSize(page_) > 0)
        item_ = std::min(PageSize(page_) - 1, static_cast<int>(c.pots[1] * PageSize(page_)));
    ClampMenu();
    if(PageSize(page_) <= 0)
        return;
    const int p = PageParam(page_, item_);
    if(p != old_p)
        confirm_p_ = -1;

    const _NT_parameter& d       = alg_->parameters[p];
    const bool           confirm = d.unit == kNT_unitConfirm;
    auto                 Apply   = [&](int v) {
        v = std::max<int>(d.min, std::min<int>(d.max, v));
        if(confirm)
        {
            confirm_p_ = p;
            confirm_v_ = v;
        }
        else
            SetParam(p, v);
    };
    // Encoder R: value (×10 while Encoder R is held). Pot R: value (absolute).
    if(c.encoders[1])
    {
        const int base = confirm_p_ == p ? confirm_v_ : values_[p];
        const int step = (c.buttons & kNT_encoderButtonR) ? 10 : 1;
        fast_          = step > 1;
        Apply(base + c.encoders[1] * step);
    }
    if(moved[2])
        Apply(d.min + static_cast<int>(std::lround(c.pots[2] * (d.max - d.min))));
    // Encoder R press (without turning): confirm.
    const bool released = (last_buttons_ & kNT_encoderButtonR) && !(c.buttons & kNT_encoderButtonR);
    if(released && !fast_ && confirm_p_ == p)
    {
        SetParam(p, confirm_v_);
        confirm_p_ = -1;
    }
    if(released)
        fast_ = false;
}

int Host::FormatValue(int p, int v, char* buff) const
{
    const _NT_parameter& d = alg_->parameters[p];
    buff[0]                = 0;
    if((d.unit == kNT_unitHasStrings || d.unit == kNT_unitConfirm) && factory_->parameterString)
    {
        Guard     g(const_cast<Host*>(this));
        const int n = factory_->parameterString(alg_, p, v, buff);
        if(n > 0)
            return n;
    }
    switch(d.unit)
    {
        case kNT_unitEnum:
            if(d.enumStrings)
                return std::snprintf(buff, kNT_parameterStringSize, "%s", d.enumStrings[v]);
            break;
        case kNT_unitAudioInput:
        case kNT_unitCvInput:
        case kNT_unitAudioOutput:
        case kNT_unitCvOutput:
            if(v <= 0)
                return std::snprintf(buff, kNT_parameterStringSize, "None");
            if(v <= kNT_numInputBusses)
                return std::snprintf(buff, kNT_parameterStringSize, "Input %d", v);
            if(v <= kNT_numInputBusses + kNT_numOutputBusses)
                return std::snprintf(buff, kNT_parameterStringSize, "Output %d", v - kNT_numInputBusses);
            return std::snprintf(buff, kNT_parameterStringSize, "Aux %d", v - kNT_numInputBusses - kNT_numOutputBusses);
        case kNT_unitOutputMode: return std::snprintf(buff, kNT_parameterStringSize, "%s", v ? "Replace" : "Add");
        default: break;
    }
    if(d.unit == kNT_unitDb_minInf && v == d.min)
        return std::snprintf(buff, kNT_parameterStringSize, "-inf dB");
    const int   div   = d.scaling == kNT_scaling10 ? 10 : (d.scaling == kNT_scaling100 ? 100 : (d.scaling == kNT_scaling1000 ? 1000 : 1));
    const int   dec   = div == 10 ? 1 : (div == 100 ? 2 : (div == 1000 ? 3 : 0));
    const bool  sign  = d.unit == kNT_unitSemitones || d.unit == kNT_unitDb || d.unit == kNT_unitDb_minInf || d.unit == kNT_unitCents;
    const char* u     = "";
    switch(d.unit)
    {
        case kNT_unitDb:
        case kNT_unitDb_minInf: u = " dB"; break;
        case kNT_unitPercent: u = "%"; break;
        case kNT_unitHz: u = " Hz"; break;
        case kNT_unitSemitones: u = " st"; break;
        case kNT_unitCents: u = " c"; break;
        case kNT_unitMs: u = " ms"; break;
        case kNT_unitSeconds: u = " s"; break;
        case kNT_unitFrames: u = " fr"; break;
        case kNT_unitMillivolts: u = " mV"; break;
        case kNT_unitVolts: u = " V"; break;
        case kNT_unitBPM: u = " BPM"; break;
        case kNT_unitDegrees: u = " deg"; break;
        case kNT_unitMIDINote:
        {
            static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            return std::snprintf(buff, kNT_parameterStringSize, "%s%d", kNames[((v % 12) + 12) % 12], v / 12 - 1);
        }
        default: break;
    }
    const int  av = v < 0 ? -v : v;
    const char* s = v < 0 ? "-" : (sign && v > 0 ? "+" : "");
    if(dec == 0)
        return std::snprintf(buff, kNT_parameterStringSize, "%s%d%s", s, av, u);
    return std::snprintf(buff, kNT_parameterStringSize, "%s%d.%0*d%s", s, av / div, dec, av % div, u);
}

namespace
{
int TextWidth(const char* s, _NT_textSize z)
{
    int w = 0;
    for(; *s; ++s)
        w += nt_raster::CharWidth(*s, z);
    return w;
}

// Shortens `s` (in place) until it fits `w` pixels.
void Fit(char* s, int w, _NT_textSize z)
{
    int n = static_cast<int>(std::strlen(s));
    while(n > 0 && TextWidth(s, z) > w)
        s[--n] = 0;
}
} // namespace

void Host::DrawMenu()
{
    ClampMenu();
    const int np = NumPages();
    // Left: the pages (current one bright), right: its parameters and values.
    constexpr int kRows = 5, kRowH = 12, kBase = 10, kSplit = 64;
    int           first = std::max(0, std::min(page_ - 2, np - kRows));
    for(int r = 0; r < kRows && first + r < np; ++r)
    {
        const int pg = first + r;
        char      name[40];
        std::snprintf(name, sizeof(name), "%s", Pages() ? Pages()->pages[pg].name : "Parameters");
        Fit(name, kSplit - 4, kNT_textNormal);
        nt_raster::Text(1, kBase + r * kRowH, name, pg == page_ ? 15 : 5, kNT_textLeft, kNT_textNormal);
    }
    nt_raster::Line(kSplit, 0, kSplit, 63, 3);
    const int n = PageSize(page_);
    if(n <= 0)
        return;
    first = std::max(0, std::min(item_ - 2, n - kRows));
    for(int r = 0; r < kRows && first + r < n; ++r)
    {
        const int  i   = first + r;
        const int  p   = PageParam(page_, i);
        const bool sel = i == item_;
        const int  y   = kBase + r * kRowH;
        char       val[kNT_parameterStringSize + 4];
        if(sel && confirm_p_ == p)
        {
            FormatValue(p, confirm_v_, val);
            std::strncat(val, "?", 2); // waits for Encoder R press
        }
        else
            FormatValue(p, values_[p], val);
        Fit(val, 255 - kSplit - 30, kNT_textNormal);
        const int vw = TextWidth(val, kNT_textNormal);
        char      name[48];
        std::snprintf(name, sizeof(name), "%s", alg_->parameters[p].name);
        Fit(name, 255 - kSplit - 6 - vw - 6, kNT_textNormal);
        if(sel)
            nt_raster::Shape(kNT_rectangle, kSplit + 2, y - 9, 255, y + 2, 2);
        nt_raster::Text(kSplit + 4, y, name, sel ? 15 : 7, kNT_textLeft, kNT_textNormal);
        nt_raster::Text(254, y, val, sel ? 15 : 9, kNT_textRight, kNT_textNormal);
    }
    // More above / below.
    if(first > 0)
        nt_raster::Shape(kNT_rectangle, 252, 0, 253, 1, 8);
    if(first + kRows < n)
        nt_raster::Shape(kNT_rectangle, 252, 62, 253, 63, 8);
}

void Host::Draw()
{
    if(!alg_)
        return;
    Guard g(this);
    std::memset(screen_, 0, sizeof(screen_));
    if(menu_open_)
        DrawMenu();
    else if(factory_->draw)
        factory_->draw(alg_);
    while(lock_.test_and_set(std::memory_order_acquire))
    {
    }
    std::memcpy(front_, screen_, sizeof(front_));
    lock_.clear(std::memory_order_release);
}

void Host::CopyScreen(uint8_t dst[kScreenBytes]) const
{
    while(lock_.test_and_set(std::memory_order_acquire))
    {
    }
    std::memcpy(dst, front_, sizeof(front_));
    lock_.clear(std::memory_order_release);
}
} // namespace nt_host
