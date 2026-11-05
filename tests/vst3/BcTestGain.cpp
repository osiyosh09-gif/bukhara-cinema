// A tiny VST 3 audio effect for the tests (phase 15c): "Gain" multiplies the
// sound by 2 x its normalized value (0.5 = unchanged) and "Invert" turns the
// polarity over. The processor takes its values only from the parameter
// changes the host sends, so the tests see whether the host sends them.
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr ParamID kGain = 0;
constexpr ParamID kInvert = 1;

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
        return kResultOk;
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
