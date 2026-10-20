// Perseids as a native VCV Rack module: the disting NT algorithm
// (nt/perseids_nt.cpp, unchanged) in a small NT host (nt_host). The panel
// follows the NT: 3 pots with push, 2 encoders with push, buttons, the
// 256 × 64 display 1:1. Every control is a Rack parameter, so MIDI-Map can
// learn each one; the encoders also have ◀ / ▶ buttons for note mapping.
//
// Panel "Large" (22 + 12 HP) adds 12 assignable knobs: right-click a knob to
// put any Perseids parameter on it, then MIDI-Map the knob to the controller.
#include <atomic>
#include <cmath>
#include <mutex>

#include "nt_host.hpp"
#include "panel_layout.hpp"
#include "perseids_plugin.hpp"

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);

namespace
{
const _NT_factory* Factory()
{
    return reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo, 0));
}

constexpr float kStepsPerTurn = 24.f; // encoder detents per full turn (and per MIDI CC sweep)
constexpr int   kUiDivider    = 32;   // controls → host every 32 frames
constexpr float kDrawHz       = 30.f;
constexpr float kRepeatDelayS = 0.4f, kRepeatRateS = 0.08f; // ◀ / ▶ held

// Factory assignment of the assignable knobs (by parameter name), row by row:
// Spectra, pitch, Swarm, resonator + time. Not on the panel otherwise.
using panel_layout::kNumSlots;
const char* const     kSlotDefaults[kNumSlots] = {
    "Partials",   "Waveshape",     "Umbra/Aurora",
    "Ensemble",   "Pitch Spectra", "Pitch Swarm",
    "Spread",     "Scatter",       "Direction",
    "Reso decay", "Reso pitch",    "Hold",
};

// Parameters a knob may take: no routing (in/out busses, output mode), no
// one-shot actions (Clear trails, Reset all mods).
bool Assignable(const _NT_parameter& d)
{
    switch(d.unit)
    {
        case kNT_unitAudioInput:
        case kNT_unitCvInput:
        case kNT_unitAudioOutput:
        case kNT_unitCvOutput:
        case kNT_unitOutputMode:
        case kNT_unitConfirm: return false;
        default: return true;
    }
}
} // namespace

struct Perseids : Module
{
    enum ParamId
    {
        POT_L_PARAM,
        POT_C_PARAM,
        POT_R_PARAM,
        ENC_L_PARAM,
        ENC_R_PARAM,
        POT_L_PUSH_PARAM,
        POT_C_PUSH_PARAM,
        POT_R_PUSH_PARAM,
        ENC_L_PUSH_PARAM,
        ENC_R_PUSH_PARAM,
        ENC_L_DOWN_PARAM,
        ENC_L_UP_PARAM,
        ENC_R_DOWN_PARAM,
        ENC_R_UP_PARAM,
        REC_PARAM,
        HOLD_PARAM,
        MENU_PARAM,
        SLOT_1_PARAM, // … 12 assignable knobs (Large panel); appended, old patches still load
        PARAMS_LEN = SLOT_1_PARAM + kNumSlots
    };
    enum InputId
    {
        IN_L_INPUT,
        IN_R_INPUT,
        CV_1_INPUT, // … CV 10: NT busses 3…12
        INPUTS_LEN = CV_1_INPUT + 10
    };
    enum OutputId
    {
        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        OUTPUTS_LEN
    };

    // Encoders turn endlessly: the parameter is the position within one turn.
    struct EncoderQuantity : ParamQuantity
    {
        std::string getDisplayValueString() override { return "endless"; }
    };

    // Assignable knob: 0…1 for MIDI; label and value are its target's.
    struct SlotQuantity : ParamQuantity
    {
        int         Slot() const { return paramId - SLOT_1_PARAM; }
        std::string getLabel() override
        {
            Perseids* m = static_cast<Perseids*>(module);
            if(!m)
                return string::f("Knob %d", Slot() + 1);
            const std::string n = m->SlotTargetName(Slot());
            return n.empty() ? string::f("Knob %d (unassigned, right-click to assign)", Slot() + 1) : n;
        }
        std::string getDisplayValueString() override
        {
            Perseids* m = static_cast<Perseids*>(module);
            return m ? m->SlotValueString(Slot()) : "";
        }
        std::string getUnit() override { return ""; }
    };

    nt_host::Host host;
    std::mutex    host_mutex; // held while the host is rebuilt (UI thread)
    float         sample_rate = 0.f;

    // Changes from the UI thread to the audio thread (single producer).
    struct Cmd
    {
        int p, v; // p >= 0: set parameter p to v; kCmdReset: all to defaults
    };
    static constexpr int kCmdReset = -100, kQueueLen = 64;
    Cmd              queue[kQueueLen];
    std::atomic<int> q_head{0}, q_tail{0};
    void             Push(int p, int v)
    {
        const int h = q_head.load(std::memory_order_relaxed), n = (h + 1) % kQueueLen;
        if(n == q_tail.load(std::memory_order_acquire))
            return; // full: drop (cannot happen at human speed)
        queue[h] = {p, v};
        q_head.store(n, std::memory_order_release);
    }

    // Assignable knobs: target parameter (host index, -1 = nothing).
    std::atomic<int>  slot_dest[kNumSlots];
    nt_host::KnobLink slot_link[kNumSlots];
    std::atomic<bool> large{false};          // panel choice, stored in the patch

    float enc_last[2] = {-1.f, -1.f};
    float enc_acc[2]  = {0.f, 0.f};
    int   step_held[2][2]   = {};   // ◀ / ▶ state
    float step_timer[2][2]  = {};
    int   ui_div            = 0;
    float draw_phase        = 0.f;

    Perseids()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, 0);
        configParam(POT_L_PARAM, 0.f, 1.f, 0.5f, "Pot L (Blend / push: Dry/Wet)", "%", 0.f, 100.f);
        configParam(POT_C_PARAM, 0.f, 1.f, 0.2f, "Pot C (Scan / push: Reso mix)", "%", 0.f, 100.f);
        configParam(POT_R_PARAM, 0.f, 1.f, 0.6f, "Pot R (Size / push: Atmosphere)", "%", 0.f, 100.f);
        configParam<EncoderQuantity>(ENC_L_PARAM, 0.f, 1.f, 0.f, "Encoder L (Trail / Mod view; menu: parameter)");
        configParam<EncoderQuantity>(ENC_R_PARAM, 0.f, 1.f, 0.f, "Encoder R (Level / amount; menu: value)");
        configButton(POT_L_PUSH_PARAM, "Pot L push");
        configButton(POT_C_PUSH_PARAM, "Pot C push");
        configButton(POT_R_PUSH_PARAM, "Pot R push");
        configButton(ENC_L_PUSH_PARAM, "Encoder L push (Solo; menu: next page)");
        configButton(ENC_R_PUSH_PARAM, "Encoder R push (Lock; menu: confirm, hold = ×10)");
        configButton(ENC_L_DOWN_PARAM, "Encoder L ◀");
        configButton(ENC_L_UP_PARAM, "Encoder L ▶");
        configButton(ENC_R_DOWN_PARAM, "Encoder R ◀");
        configButton(ENC_R_UP_PARAM, "Encoder R ▶");
        configButton(REC_PARAM, "Record (NT button 3)");
        configButton(HOLD_PARAM, "Hold INF (NT button 4)");
        configButton(MENU_PARAM, "Menu (parameter pages)");
        for(int k = 0; k < kNumSlots; ++k)
        {
            configParam<SlotQuantity>(SLOT_1_PARAM + k, 0.f, 1.f, 0.f, string::f("Knob %d", k + 1));
            getParamQuantity(SLOT_1_PARAM + k)->resetEnabled     = false; // follows its target instead
            getParamQuantity(SLOT_1_PARAM + k)->randomizeEnabled = false;
            slot_dest[k].store(-1);
        }
        for(int k = 0; k < 2; ++k)
            getParamQuantity(ENC_L_PARAM + k)->snapEnabled = false;
        configInput(IN_L_INPUT, "In L (NT input 1)");
        configInput(IN_R_INPUT, "In R (NT input 2)");
        for(int k = 0; k < 10; ++k)
            configInput(CV_1_INPUT + k, string::f("CV %d (NT input %d)", 1 + k, 3 + k));
        configOutput(OUT_L_OUTPUT, "Out L (NT output 1)");
        configOutput(OUT_R_OUTPUT, "Out R (NT output 2)");
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);
        Rebuild({}, {}, APP->engine->getSampleRate());
        for(int k = 0; k < kNumSlots; ++k)
            slot_dest[k].store(DefaultSlot(k));
    }

    int DefaultSlot(int k) { return host.FindParam(kSlotDefaults[k]); }

    // Name of knob k's target (UI thread; while the host is being rebuilt the
    // last name seen is kept).
    std::string slot_name_cache[kNumSlots];
    std::string SlotTargetName(int k)
    {
        const int                    p = slot_dest[k].load();
        std::unique_lock<std::mutex> l(host_mutex, std::try_to_lock);
        if(l.owns_lock())
            slot_name_cache[k] = p >= 0 && p < host.NumParams() ? host.ParamName(p) : "";
        return slot_name_cache[k];
    }

    std::string SlotValueString(int k)
    {
        const int                    p = slot_dest[k].load();
        std::unique_lock<std::mutex> l(host_mutex, std::try_to_lock);
        if(!l.owns_lock() || !host.Ok() || p < 0 || p >= host.NumParams())
            return "";
        return host.ValueString(p, host.ParamValue(p));
    }

    // New algorithm instance (other Trail length, sample rate, patch load).
    // keep: carry the current parameter values over.
    void Rebuild(std::vector<int32_t> specs, std::vector<std::pair<std::string, int>> values, float sr, bool keep = false)
    {
        std::lock_guard<std::mutex> l(host_mutex);
        if(keep)
            values = host.Values();
        if(specs.empty())
            specs = host.Specs();
        sample_rate = sr;
        host.Create(Factory(), specs, static_cast<uint32_t>(sr + 0.5f), values);
        // The menu names the busses after the panel jacks.
        std::vector<std::string> names = {"In L", "In R"};
        for(int k = 1; k <= 10; ++k)
            names.push_back(string::f("CV %d", k));
        names.push_back("Out L");
        names.push_back("Out R");
        host.SetBusNames(names);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override
    {
        if(std::fabs(e.sampleRate - sample_rate) > 0.5f)
            Rebuild({}, {}, e.sampleRate, true);
    }

    void onReset(const ResetEvent& e) override
    {
        Module::onReset(e);
        Push(kCmdReset, 0); // parameters to defaults, in the audio thread
        for(int k = 0; k < kNumSlots; ++k)
            slot_dest[k].store(DefaultSlot(k));
    }

    // Assignable knobs, at control rate: turned → set the target; otherwise
    // follow it (menu, preset, CV mapping may change it).
    void ProcessSlots()
    {
        for(int k = 0; k < kNumSlots; ++k)
        {
            const float show = nt_host::Link(host, slot_link[k], slot_dest[k].load(), params[SLOT_1_PARAM + k].getValue());
            if(show >= 0.f)
                params[SLOT_1_PARAM + k].setValue(show);
        }
    }

    int Steps(int k, float dt)
    {
        // Endless knob / MIDI CC: shortest way round the turn.
        const float v = params[ENC_L_PARAM + k].getValue();
        if(enc_last[k] < 0.f)
            enc_last[k] = v;
        float d = v - enc_last[k];
        if(d > 0.5f)
            d -= 1.f;
        else if(d < -0.5f)
            d += 1.f;
        enc_last[k] = v;
        enc_acc[k] += d * kStepsPerTurn;
        int n = static_cast<int>(enc_acc[k]);
        enc_acc[k] -= n;
        // ◀ / ▶: one step per press, repeating while held.
        for(int dir = 0; dir < 2; ++dir)
        {
            const bool down = params[ENC_L_DOWN_PARAM + 2 * k + dir].getValue() > 0.5f;
            const int  s    = dir ? 1 : -1;
            if(down && !step_held[k][dir])
            {
                n += s;
                step_timer[k][dir] = kRepeatDelayS;
            }
            else if(down && (step_timer[k][dir] -= dt) <= 0.f)
            {
                n += s;
                step_timer[k][dir] = kRepeatRateS;
            }
            step_held[k][dir] = down;
        }
        return n;
    }

    void process(const ProcessArgs& args) override
    {
        std::unique_lock<std::mutex> l(host_mutex, std::try_to_lock);
        if(!l.owns_lock() || !host.Ok())
        {
            outputs[OUT_L_OUTPUT].setVoltage(0.f);
            outputs[OUT_R_OUTPUT].setVoltage(0.f);
            return;
        }
        // UI-thread changes first.
        for(int t = q_tail.load(std::memory_order_relaxed); t != q_head.load(std::memory_order_acquire);
            t = (t + 1) % kQueueLen)
        {
            const Cmd c = queue[t];
            if(c.p == kCmdReset)
            {
                host.ResetParams();
                for(int k = 0; k < kNumSlots; ++k)
                    slot_link[k] = nt_host::KnobLink{};
            }
            else
                host.SetParam(c.p, c.v);
            q_tail.store((t + 1) % kQueueLen, std::memory_order_release);
        }
        float in[kNT_numInputBusses];
        for(int k = 0; k < kNT_numInputBusses; ++k)
            in[k] = inputs[k].getVoltage();
        float out[2];
        host.Frame(in, out);
        outputs[OUT_L_OUTPUT].setVoltage(out[0]);
        outputs[OUT_R_OUTPUT].setVoltage(out[1]);

        if(++ui_div >= kUiDivider)
        {
            ui_div = 0;
            const float       dt = kUiDivider * args.sampleTime;
            nt_host::Controls c;
            for(int k = 0; k < 3; ++k)
                c.pots[k] = params[POT_L_PARAM + k].getValue();
            c.encoders[0] = Steps(0, dt);
            c.encoders[1] = Steps(1, dt);
            auto Bit = [&](int id, uint16_t bit) {
                if(params[id].getValue() > 0.5f)
                    c.buttons |= bit;
            };
            Bit(POT_L_PUSH_PARAM, kNT_potButtonL);
            Bit(POT_C_PUSH_PARAM, kNT_potButtonC);
            Bit(POT_R_PUSH_PARAM, kNT_potButtonR);
            Bit(ENC_L_PUSH_PARAM, kNT_encoderButtonL);
            Bit(ENC_R_PUSH_PARAM, kNT_encoderButtonR);
            Bit(REC_PARAM, kNT_button3);
            Bit(HOLD_PARAM, kNT_button4);
            c.menu = params[MENU_PARAM].getValue() > 0.5f;
            host.Ui(c);
            ProcessSlots();
        }
        draw_phase += args.sampleTime * kDrawHz;
        if(draw_phase >= 1.f)
        {
            draw_phase -= 1.f;
            host.Draw();
        }
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* sp   = json_array();
        for(int32_t s : host.Specs())
            json_array_append_new(sp, json_integer(s));
        json_object_set_new(root, "specifications", sp);
        // Parameter values by name (the NT preset keys), not by index.
        json_t* vals = json_object();
        for(const auto& nv : host.Values())
            json_object_set_new(vals, nv.first.c_str(), json_integer(nv.second));
        json_object_set_new(root, "values", vals);
        // Knob targets by parameter name ("" = nothing), and the panel.
        json_t* slots = json_array();
        for(int k = 0; k < kNumSlots; ++k)
            json_array_append_new(slots, json_string(SlotTargetName(k).c_str()));
        json_object_set_new(root, "knobs", slots);
        json_object_set_new(root, "large", json_boolean(large.load()));
        return root;
    }

    void dataFromJson(json_t* root) override
    {
        std::vector<int32_t> specs;
        if(json_t* sp = json_object_get(root, "specifications"))
            for(size_t i = 0; i < json_array_size(sp); ++i)
                specs.push_back(static_cast<int32_t>(json_integer_value(json_array_get(sp, i))));
        std::vector<std::pair<std::string, int>> values;
        if(json_t* vals = json_object_get(root, "values"))
        {
            const char* key;
            json_t*     v;
            json_object_foreach(vals, key, v) values.emplace_back(key, static_cast<int>(json_integer_value(v)));
        }
        Rebuild(specs, values, sample_rate > 0.f ? sample_rate : APP->engine->getSampleRate());
        if(json_t* slots = json_object_get(root, "knobs"))
            for(int k = 0; k < kNumSlots && k < static_cast<int>(json_array_size(slots)); ++k)
            {
                const char* n = json_string_value(json_array_get(slots, k));
                slot_dest[k].store(n && *n ? host.FindParam(n) : -1);
                slot_link[k] = nt_host::KnobLink{};
            }
        if(json_t* l = json_object_get(root, "large"))
            large.store(json_boolean_value(l));
    }
};

// The NT display, pixel for pixel: 256 × 64, 16 grey levels, lit in the dark.
struct NtDisplay : Widget
{
    Perseids* module = nullptr;
    uint8_t   screen[nt_host::kScreenBytes] = {};

    void draw(const DrawArgs& args) override
    {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(0, 0, 0));
        nvgFill(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if(layer == 1 && module)
        {
            module->host.CopyScreen(screen);
            const float sx = box.size.x / 256.f, sy = box.size.y / 64.f;
            // One path per grey level, runs of equal pixels as one rectangle.
            for(int level = 1; level < 16; ++level)
            {
                bool any = false;
                nvgBeginPath(args.vg);
                for(int y = 0; y < 64; ++y)
                {
                    int run = -1;
                    for(int x = 0; x <= 256; ++x)
                    {
                        int v = 0;
                        if(x < 256)
                        {
                            const uint8_t b = screen[y * 128 + x / 2];
                            v               = (x & 1) ? (b & 15) : (b >> 4);
                        }
                        if(v == level && run < 0)
                            run = x;
                        else if(v != level && run >= 0)
                        {
                            nvgRect(args.vg, run * sx, y * sy, (x - run) * sx, sy);
                            run = -1;
                            any = true;
                        }
                    }
                }
                if(any)
                {
                    nvgFillColor(args.vg, nvgRGBA(255, 255, 255, level * 17));
                    nvgFill(args.vg);
                }
            }
        }
        Widget::drawLayer(args, layer);
    }
};

// Endless encoder: dragging and scrolling wrap round instead of stopping.
struct EndlessKnob : RoundLargeBlackKnob
{
    EndlessKnob()
    {
        minAngle = 0.f;
        maxAngle = 2.f * float(M_PI);
    }

    void Turn(float d)
    {
        if(ParamQuantity* pq = getParamQuantity())
            pq->setValue(math::eucMod(pq->getValue() + d, 1.f));
    }

    void onDragMove(const DragMoveEvent& e) override
    {
        if(e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        const float fine = (APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL ? 0.25f : 1.f;
        Turn(-e.mouseDelta.y * 0.004f * fine);
    }

    void onHoverScroll(const HoverScrollEvent& e) override
    {
        Turn((e.scrollDelta.y > 0.f ? 1.f : -1.f) / kStepsPerTurn);
        e.consume(this);
    }
};

// One assignable knob: right-click → "Assign this knob to" → page → parameter.
struct SlotKnob : RoundSmallBlackKnob
{
    Perseids* m = nullptr;
    int       k = 0;

    void appendContextMenu(Menu* menu) override
    {
        if(!m)
            return;
        // Snapshot of the pages (the plug-in may rebuild them while the menu is open).
        struct Entry
        {
            std::string             page;
            std::vector<std::pair<int, std::string>> params;
        };
        std::vector<Entry> pages;
        {
            std::unique_lock<std::mutex> l(m->host_mutex, std::try_to_lock);
            if(!l.owns_lock() || !m->host.Ok())
                return;
            for(int pg = 0; pg < m->host.NumPages(); ++pg)
            {
                Entry e{m->host.PageName(pg), {}};
                if(e.page == "Mod overview") // the same amounts as on the Mod n pages
                    continue;
                for(int i = 0; i < m->host.PageSize(pg); ++i)
                {
                    const int p = m->host.PageParam(pg, i);
                    if(p >= 0 && p < m->host.NumParams() && Assignable(m->host.ParamDef(p)))
                        e.params.emplace_back(p, m->host.ParamName(p));
                }
                if(!e.params.empty())
                    pages.push_back(e);
            }
        }
        Perseids* mm = m;
        const int kk = k;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Assign this knob to"));
        menu->addChild(createCheckMenuItem("Nothing", "", [=]() { return mm->slot_dest[kk].load() < 0; },
                                           [=]() { mm->slot_dest[kk].store(-1); }));
        for(const Entry& e : pages)
            menu->addChild(createSubmenuItem(e.page, "", [=](Menu* sub) {
                for(const auto& pr : e.params)
                {
                    const int p = pr.first;
                    sub->addChild(createCheckMenuItem(pr.second, "", [=]() { return mm->slot_dest[kk].load() == p; },
                                                      [=]() { mm->slot_dest[kk].store(p); }));
                }
            }));
        menu->addChild(createMenuItem("Default", kSlotDefaults[kk], [=]() { mm->slot_dest[kk].store(mm->DefaultSlot(kk)); }));
    }
};

// Names under the assignable knobs: they change, so they are drawn here, in
// the panel's font (cap height as on the SVG: nanovg size = cap / 0.729).
struct SlotLabels : Widget
{
    Perseids* m = nullptr;

    void draw(const DrawArgs& args) override
    {
        std::shared_ptr<window::Font> f =
            APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/DejaVuSansCondensed-Bold.ttf"));
        if(!f)
            return;
        using namespace panel_layout;
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, f->handle);
        nvgFontSize(vg, mm2px(kSlotLabelCap / 0.729f));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
        nvgFillColor(vg, nvgRGB(0x8f, 0xb8, 0xff));
        const float maxw = mm2px(kSlotLabelMaxW);
        auto Width = [&](const std::string& t) { return nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr); };
        auto Fit   = [&](std::string t) {
            while(t.size() > 1 && Width(t) > maxw)
                t.pop_back();
            return t;
        };
        for(int k = 0; k < kNumSlots; ++k)
        {
            std::string t = m ? m->SlotTargetName(k) : kSlotDefaults[k];
            t             = t.empty() ? "-" : string::uppercase(t);
            const Vec c   = Slot(k);
            const float y = c.y + mm2px(kSlotLabelDy);
            if(Width(t) <= maxw)
            {
                nvgText(vg, c.x, y, t.c_str(), nullptr);
                continue;
            }
            // Two lines: break at the space or slash nearest the middle.
            size_t best = std::string::npos;
            for(size_t i = 1; i + 1 < t.size(); ++i)
                if((t[i] == ' ' || t[i] == '/')
                   && (best == std::string::npos || std::abs(int(i) - int(t.size() / 2)) < std::abs(int(best) - int(t.size() / 2))))
                    best = i;
            std::string a = t, b;
            if(best != std::string::npos)
            {
                a = t.substr(0, t[best] == '/' ? best + 1 : best);
                b = t.substr(best + 1);
            }
            nvgText(vg, c.x, y, Fit(a).c_str(), nullptr);
            if(!b.empty())
                nvgText(vg, c.x, y + mm2px(kSlotLabelLine), Fit(b).c_str(), nullptr);
        }
    }
};

struct PerseidsWidget : ModuleWidget
{
    std::vector<Widget*> large_only; // extension field: hidden on the Performance panel
    Widget*              screw_r    = nullptr;
    bool                 shown_large = false;

    PerseidsWidget(Perseids* module)
    {
        using namespace panel_layout;
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Perseids.svg")));
        addChild(createWidgetCentered<ScrewBlack>(Screw(0)));
        screw_r = createWidgetCentered<ScrewBlack>(Screw(1));
        addChild(screw_r);

        NtDisplay* d = createWidget<NtDisplay>(DisplayPos());
        d->box.size  = DisplaySize();
        d->module    = module;
        addChild(d);

        for(int k = 0; k < 3; ++k)
        {
            addParam(createParamCentered<RoundBigBlackKnob>(Pot(k), module, Perseids::POT_L_PARAM + k));
            addParam(createParamCentered<TL1105>(PotPush(k), module, Perseids::POT_L_PUSH_PARAM + k));
        }
        for(int k = 0; k < 2; ++k)
        {
            addParam(createParamCentered<EndlessKnob>(Enc(k), module, Perseids::ENC_L_PARAM + k));
            addParam(createParamCentered<TL1105>(EncStep(k, -1), module, Perseids::ENC_L_DOWN_PARAM + 2 * k));
            addParam(createParamCentered<TL1105>(EncStep(k, 1), module, Perseids::ENC_L_UP_PARAM + 2 * k));
            addParam(createParamCentered<TL1105>(EncPush(k), module, Perseids::ENC_L_PUSH_PARAM + k));
        }
        addParam(createParamCentered<VCVButton>(CenterButton(0), module, Perseids::REC_PARAM));
        addParam(createParamCentered<VCVButton>(CenterButton(1), module, Perseids::HOLD_PARAM));
        addParam(createParamCentered<VCVButton>(CenterButton(2), module, Perseids::MENU_PARAM));
        for(int k = 0; k < Perseids::INPUTS_LEN; ++k)
            addInput(createInputCentered<PJ301MPort>(InputPos(k), module, k));
        for(int k = 0; k < Perseids::OUTPUTS_LEN; ++k)
            addOutput(createOutputCentered<PJ301MPort>(OutputPos(k), module, k));
        for(int k = 0; k < kNumSlots; ++k)
        {
            SlotKnob* s = createParamCentered<SlotKnob>(Slot(k), module, Perseids::SLOT_1_PARAM + k);
            s->m        = module;
            s->k        = k;
            addParam(s);
            large_only.push_back(s);
        }
        SlotLabels* labels = createWidget<SlotLabels>(Vec(0, 0));
        labels->box.size   = mm2px(Vec(kHpLarge * 5.08f, 128.5f));
        labels->m          = module;
        addChild(labels);
        large_only.push_back(labels);
        Layout(module && module->large.load());
    }

    // Performance (22 HP) or Large (22 + 12 HP).
    void Layout(bool lg)
    {
        shown_large    = lg;
        const Vec old  = box.size;
        setPanel(createPanel(asset::plugin(pluginInstance, lg ? "res/Perseids-Large.svg" : "res/Perseids.svg")));
        for(Widget* w : large_only)
            w->visible = lg; // the knobs keep working while hidden
        screw_r->box.pos = panel_layout::ScrewRight(lg).minus(screw_r->box.size.div(2));
        if(lg && box.size.x > old.x && APP->scene && APP->scene->rack && parent)
            APP->scene->rack->setModulePosForce(this, box.pos); // neighbours on the right move aside
    }

    void step() override
    {
        Perseids* m = getModule<Perseids>();
        if(m && m->large.load() != shown_large)
            Layout(m->large.load());
        ModuleWidget::step();
    }

    void appendContextMenu(Menu* menu) override
    {
        Perseids* m = getModule<Perseids>();
        if(!m)
            return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createIndexSubmenuItem(
            "Panel", {"Performance (22 HP)", "Large (22 + 12 HP): 12 assignable knobs"},
            [=]() { return m->large.load() ? size_t(1) : size_t(0); }, [=](size_t i) { m->large.store(i == 1); }));
        menu->addChild(createMenuItem("Assignable knobs: factory targets", "", [=]() {
            for(int k = 0; k < kNumSlots; ++k)
                m->slot_dest[k].store(m->DefaultSlot(k));
        }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Trail length (rebuilds; recorded Trails are cleared)"));
        for(int s : {5, 10, 15, 20, 30})
            menu->addChild(createCheckMenuItem(
                string::f("%d s per Trail", s), "",
                [=]() { return !m->host.Specs().empty() && m->host.Specs()[0] == s; },
                [=]() { m->Rebuild({s}, {}, m->sample_rate, true); }));
        menu->addChild(createMenuLabel("Recorded Trails are not stored in the patch."));
    }
};

Model* modelPerseids = createModel<Perseids, PerseidsWidget>("Perseids");
