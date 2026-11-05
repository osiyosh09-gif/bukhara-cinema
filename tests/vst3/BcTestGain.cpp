// A tiny VST 3 audio effect for the tests (phase 15c): "Gain" multiplies the
// sound by 2 x its normalized value (0.5 = unchanged) and "Invert" turns the
// polarity over. The processor takes its values only from the parameter
// changes the host sends, so the tests see whether the host sends them.
// Phase 15c-2: hidden "Crash" and "Hang" parameters make the processor crash
// or stop answering, and on Windows a small window changes "Gain" to 0.75 as
// soon as it is opened (as if the user turned the knob).
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr ParamID kGain = 0;
constexpr ParamID kInvert = 1;
constexpr ParamID kCrash = 2;
constexpr ParamID kHang = 3;

class TestGain;

// The plug-in's window: a label, and one turn of the "Gain" knob.
class TestView : public CPluginView
{
public:
    TestView(EditController *controller) : CPluginView(nullptr), m_controller(controller)
    {
        ViewRect r(0, 0, 320, 120);
        setRect(r);
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#ifdef _WIN32
        return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#else
        (void)type;
        return kResultFalse;
#endif
    }

    tresult PLUGIN_API attached(void *parent, FIDString type) override
    {
        const tresult r = CPluginView::attached(parent, type);
#ifdef _WIN32
        m_label = CreateWindowExW(0, L"STATIC", L"BC Test Gain", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 40, 320, 40,
                                  static_cast<HWND>(parent), nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
        m_controller->beginEdit(kGain);
        m_controller->setParamNormalized(kGain, 0.75);
        m_controller->performEdit(kGain, 0.75);
        m_controller->endEdit(kGain);
        return r;
    }

    tresult PLUGIN_API removed() override
    {
#ifdef _WIN32
        if (m_label)
            DestroyWindow(m_label);
        m_label = nullptr;
#endif
        return CPluginView::removed();
    }

private:
    EditController *m_controller;
#ifdef _WIN32
    HWND m_label = nullptr;
#endif
};

class TestGain : public SingleComponentEffect
{
public:
    static FUnknown *createInstance(void *) { return static_cast<IAudioProcessor *>(new TestGain); }

    tresult PLUGIN_API initialize(FUnknown *context) override
    {
        const tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        parameters.addParameter(STR16("Gain"), STR16("x"), 0, 0.5, ParameterInfo::kCanAutomate, kGain);
        parameters.addParameter(STR16("Invert"), nullptr, 1, 0, ParameterInfo::kCanAutomate, kInvert);
        parameters.addParameter(STR16("Crash"), nullptr, 1, 0, ParameterInfo::kIsHidden, kCrash);
        parameters.addParameter(STR16("Hang"), nullptr, 1, 0, ParameterInfo::kIsHidden, kHang);
        return kResultOk;
    }

    IPlugView *PLUGIN_API createView(FIDString name) override
    {
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new TestView(this);
        return nullptr;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns, SpeakerArrangement *outputs,
                                          int32 numOuts) override
    {
        if (numIns != 1 || numOuts != 1 || inputs[0] != outputs[0]
            || (inputs[0] != SpeakerArr::kMono && inputs[0] != SpeakerArr::kStereo))
            return kResultFalse;
        removeAudioBusses();
        addAudioInput(STR16("In"), inputs[0]);
        addAudioOutput(STR16("Out"), outputs[0]);
        return kResultTrue;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API process(ProcessData &data) override
    {
        if (IParameterChanges *changes = data.inputParameterChanges) {
            for (int32 i = 0; i < changes->getParameterCount(); ++i) {
                IParamValueQueue *q = changes->getParameterData(i);
                if (!q || q->getPointCount() <= 0)
                    continue;
                int32 offset = 0;
                ParamValue v = 0;
                if (q->getPoint(q->getPointCount() - 1, offset, v) != kResultTrue)
                    continue;
                if (q->getParameterId() == kGain)
                    m_gain = v;
                else if (q->getParameterId() == kInvert)
                    m_invert = v >= 0.5;
                else if (q->getParameterId() == kCrash && v >= 0.5)
                    *static_cast<volatile int *>(nullptr) = 1; // on purpose: the host must survive this
                else if (q->getParameterId() == kHang && v >= 0.5)
                    for (;;)
                        std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1)
            return kResultOk;
        const float g = float(m_gain * 2.0) * (m_invert ? -1.0f : 1.0f);
        const AudioBusBuffers &in = data.inputs[0];
        AudioBusBuffers &out = data.outputs[0];
        const int32 ch = std::min(in.numChannels, out.numChannels);
        for (int32 c = 0; c < ch; ++c)
            for (int32 i = 0; i < data.numSamples; ++i)
                out.channelBuffers32[c][i] = in.channelBuffers32[c][i] * g;
        out.silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream *state) override
    {
        IBStreamer s(state, kLittleEndian);
        float g = 0.5f;
        int32 inv = 0;
        if (!s.readFloat(g) || !s.readInt32(inv))
            return kResultFalse;
        m_gain = g;
        m_invert = inv != 0;
        setParamNormalized(kGain, m_gain);
        setParamNormalized(kInvert, m_invert ? 1 : 0);
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream *state) override
    {
        IBStreamer s(state, kLittleEndian);
        s.writeFloat(float(m_gain));
        s.writeInt32(m_invert ? 1 : 0);
        return kResultOk;
    }

private:
    double m_gain = 0.5;
    bool m_invert = false;
};

} // namespace

BEGIN_FACTORY_DEF("BUKHARA CINEMA tests", "https://github.com/osiyosh09-gif/bukhara-cinema", "")
DEF_CLASS2(INLINE_UID(0x6B1C3A10, 0x4E2D47A1, 0x9F2B5C8D, 0x3E7A1B42), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "BC Test Gain", 0, "Fx|Dynamics", "1.0.0", kVstVersionString,
           TestGain::createInstance)
END_FACTORY
