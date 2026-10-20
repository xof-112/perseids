// Perseids as a native VCV Rack module: the disting NT algorithm
// (nt/perseids_nt.cpp, unchanged) in a small NT host (nt_host). The panel
// follows the NT: 3 pots with push, 2 encoders with push, buttons, the
// 256 × 64 display 1:1. Every control is a Rack parameter, so MIDI-Map can
// learn each one; the encoders also have ◀ / ▶ buttons for note mapping.
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
        PARAMS_LEN
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

    nt_host::Host host;
    std::mutex    host_mutex; // held while the host is rebuilt (UI thread)
    float         sample_rate = 0.f;

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
        std::lock_guard<std::mutex> l(host_mutex);
        host.ResetParams();
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

struct PerseidsWidget : ModuleWidget
{
    PerseidsWidget(Perseids* module)
    {
        using namespace panel_layout;
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Perseids.svg")));
        for(int k = 0; k < kNumScrews; ++k)
            addChild(createWidgetCentered<ScrewBlack>(Screw(k)));

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
    }

    void appendContextMenu(Menu* menu) override
    {
        Perseids* m = getModule<Perseids>();
        if(!m)
            return;
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
