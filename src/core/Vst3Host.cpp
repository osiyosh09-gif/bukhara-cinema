#include "core/Vst3Host.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#ifdef BC_HAVE_VST3
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/moduleinfo/moduleinfocreator.h"
#include "public.sdk/source/vst/moduleinfo/moduleinfoparser.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#endif

namespace bc::vst3 {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool isEffect(const std::string &category, const std::string &subCategories)
{
    // Audio effects only: instruments need notes, which a video editor has none of.
    return category == "Audio Module Class" && subCategories.find("Instrument") == std::string::npos;
}

} // namespace

std::vector<std::string> findBundles(const std::string &folder)
{
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path root = fs::u8path(folder);
    if (!fs::is_directory(root, ec))
        return out;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        if (lower(p.extension().u8string()) == ".vst3") {
            out.push_back(p.u8string());
            if (it->is_directory(ec))
                it.disable_recursion_pending(); // a bundle: its insides are not plug-ins
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

#ifdef BC_HAVE_VST3

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

class Host : public HostApplication
{
public:
    tresult PLUGIN_API getName(String128 name) override
    {
        const std::u16string n = u"BUKHARA CINEMA";
        std::fill(name, name + 128, 0);
        std::copy(n.begin(), n.end(), name);
        return kResultTrue;
    }
};

std::once_flag s_hostOnce;
std::mutex s_moduleMutex;
std::map<std::string, VST3::Hosting::Module::Ptr> s_modules; // loaded modules stay loaded

void ensureHost()
{
    std::call_once(s_hostOnce, [] {
        static Host *host = new Host; // lives as long as the program
        PluginContextFactory::instance().setPluginContext(host);
    });
}

VST3::Hosting::Module::Ptr moduleFor(const std::string &path, std::string *error)
{
    ensureHost();
    std::lock_guard<std::mutex> lock(s_moduleMutex);
    auto it = s_modules.find(path);
    if (it != s_modules.end())
        return it->second;
    std::string err;
    VST3::Hosting::Module::Ptr m = VST3::Hosting::Module::create(path, err);
    if (!m) {
        if (error)
            *error = err.empty() ? "the module could not be loaded" : err;
        return nullptr;
    }
    s_modules.emplace(path, m);
    return m;
}

std::string text(const TChar *s)
{
    std::u16string u;
    for (int i = 0; i < 128 && s[i]; ++i)
        u.push_back(char16_t(s[i]));
    return StringConvert::convert(u);
}

bool sameUid(const VST3::UID &id, const std::string &uid)
{
    const std::string want = lower(uid);
    return lower(id.toString(true)) == want || lower(id.toString(false)) == want;
}

} // namespace

bool available()
{
    return true;
}

std::vector<std::string> standardBundles()
{
    ensureHost();
    std::vector<std::string> out = VST3::Hosting::Module::getModulePaths();
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<PluginInfo> describeBundle(const std::string &path, bool allowLoad, bool *loaded, std::string *error)
{
    std::vector<PluginInfo> out;
    if (loaded)
        *loaded = false;
    if (auto infoPath = VST3::Hosting::Module::getModuleInfoPath(path)) {
        std::ifstream f(fs::u8path(*infoPath), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string json = ss.str();
        if (auto info = ModuleInfoLib::parseJson(json, nullptr)) {
            for (const auto &c : info->classes) {
                std::string sub;
                for (const std::string &s : c.subCategories)
                    sub += (sub.empty() ? "" : "|") + s;
                if (!isEffect(c.category, sub))
                    continue;
                out.push_back({path, c.cid, c.name, c.vendor.empty() ? info->factoryInfo.vendor : c.vendor, c.version, sub});
            }
            return out;
        }
    }
    if (!allowLoad) {
        if (error)
            *error = "no moduleinfo.json";
        return out;
    }
    VST3::Hosting::Module::Ptr m = moduleFor(path, error);
    if (loaded)
        *loaded = true;
    if (!m)
        return out;
    const auto &factory = m->getFactory();
    for (const auto &c : factory.classInfos()) {
        if (!isEffect(c.category(), c.subCategoriesString()))
            continue;
        out.push_back({path, c.ID().toString(), c.name(),
                       c.vendor().empty() ? factory.info().vendor() : c.vendor(), c.version(), c.subCategoriesString()});
    }
    return out;
}

std::string moduleInfoJson(const std::string &path, std::string *error)
{
    VST3::Hosting::Module::Ptr m = moduleFor(path, error);
    if (!m)
        return {};
    std::ostringstream out;
    ModuleInfoLib::outputJson(ModuleInfoLib::createModuleInfo(*m, false), out);
    return out.str();
}

struct Plugin::Impl
{
    PluginInfo info;
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    FUnknownPtr<IAudioProcessor> processor;

    HostProcessData data;
    ParameterChanges inChanges{64};
    ProcessContext context{};
    bool prepared = false;
    bool active = false;
    int maxBlock = 0;
    int channels = 0;

    std::mutex paramMutex;
    std::map<uint32_t, double> values;  // every value set so far
    std::map<uint32_t, double> pending; // not yet given to the processor

    void stop()
    {
        if (!active)
            return;
        if (processor)
            processor->setProcessing(false);
        component->setActive(false);
        active = false;
    }
};

Plugin::Plugin() : d(new Impl) {}

Plugin::~Plugin()
{
    d->stop();
    d->data.unprepare();
    d->processor = nullptr;
    d->controller = nullptr;
    d->component = nullptr;
    d->provider = nullptr; // terminates and releases the plug-in
}

std::unique_ptr<Plugin> Plugin::create(const std::string &path, const std::string &uid, std::string *error)
{
    VST3::Hosting::Module::Ptr module = moduleFor(path, error);
    if (!module)
        return nullptr;
    const auto &factory = module->getFactory();
    for (const auto &c : factory.classInfos()) {
        if (c.category() != kVstAudioEffectClass || !sameUid(c.ID(), uid))
            continue;
        std::unique_ptr<Plugin> p(new Plugin);
        p->d->module = module;
        p->d->provider = owned(new PlugProvider(factory, c, true));
        if (!p->d->provider->initialize()) {
            if (error)
                *error = "the plug-in could not be started";
            return nullptr;
        }
        p->d->component = p->d->provider->getComponentPtr();
        p->d->controller = p->d->provider->getControllerPtr();
        p->d->processor = FUnknownPtr<IAudioProcessor>(p->d->component);
        if (!p->d->component || !p->d->processor) {
            if (error)
                *error = "the plug-in has no audio processor";
            return nullptr;
        }
        if (p->d->processor->canProcessSampleSize(kSample32) != kResultTrue) {
            if (error)
                *error = "the plug-in cannot process 32-bit float samples";
            return nullptr;
        }
        p->d->info = {path, c.ID().toString(), c.name(),
                      c.vendor().empty() ? factory.info().vendor() : c.vendor(), c.version(), c.subCategoriesString()};
        // Start from the controller's defaults, so the processor and the controller agree.
        for (const ParamInfo &pi : p->parameters())
            p->d->values[pi.id] = p->parameter(pi.id);
        return p;
    }
    if (error)
        *error = "no such audio effect in the module";
    return nullptr;
}

const PluginInfo &Plugin::info() const
{
    return d->info;
}

std::vector<ParamInfo> Plugin::parameters() const
{
    std::vector<ParamInfo> out;
    if (!d->controller)
        return out;
    const int32 n = d->controller->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        ParameterInfo pi{};
        if (d->controller->getParameterInfo(i, pi) != kResultOk)
            continue;
        ParamInfo p;
        p.id = pi.id;
        p.title = text(pi.title);
        p.units = text(pi.units);
        p.defaultValue = pi.defaultNormalizedValue;
        p.stepCount = pi.stepCount;
        p.readOnly = pi.flags & ParameterInfo::kIsReadOnly;
        p.hidden = pi.flags & ParameterInfo::kIsHidden;
        p.bypass = pi.flags & ParameterInfo::kIsBypass;
        out.push_back(p);
    }
    return out;
}

double Plugin::parameter(uint32_t id) const
{
    return d->controller ? d->controller->getParamNormalized(id) : 0.0;
}

std::string Plugin::valueText(uint32_t id, double normalized) const
{
    String128 s{};
    if (d->controller && d->controller->getParamStringByValue(id, normalized, s) == kResultOk)
        return text(s);
    std::ostringstream o;
    o.precision(3);
    o << normalized;
    return o.str();
}

bool Plugin::prepare(double sampleRate, int channels, int maxBlock)
{
    Impl &p = *d;
    p.stop();
    p.prepared = false;
    channels = std::clamp(channels, 1, 2);
    const int32 nIn = p.component->getBusCount(kAudio, kInput);
    const int32 nOut = p.component->getBusCount(kAudio, kOutput);
    if (nOut < 1)
        return false;
    // The main buses get the clip's layout; the others (side chains) keep theirs, switched off.
    const SpeakerArrangement want = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    std::vector<SpeakerArrangement> ins(static_cast<size_t>(std::max<int32>(nIn, 0)));
    std::vector<SpeakerArrangement> outs(static_cast<size_t>(nOut));
    for (int32 i = 0; i < nIn; ++i)
        p.processor->getBusArrangement(kInput, i, ins[size_t(i)]);
    for (int32 i = 0; i < nOut; ++i)
        p.processor->getBusArrangement(kOutput, i, outs[size_t(i)]);
    if (nIn > 0)
        ins[0] = want;
    outs[0] = want;
    p.processor->setBusArrangements(ins.empty() ? nullptr : ins.data(), nIn, outs.data(), nOut); // may refuse: then theirs
    for (int32 i = 0; i < nIn; ++i)
        p.component->activateBus(kAudio, kInput, i, i == 0);
    for (int32 i = 0; i < nOut; ++i)
        p.component->activateBus(kAudio, kOutput, i, i == 0);

    ProcessSetup setup{kRealtime, kSample32, maxBlock, sampleRate};
    if (p.processor->setupProcessing(setup) != kResultOk)
        return false;
    if (!p.data.prepare(*p.component, maxBlock, kSample32))
        return false;
    p.data.processMode = kRealtime;
    p.data.inputParameterChanges = &p.inChanges;
    p.context = ProcessContext{};
    p.context.sampleRate = sampleRate;
    p.context.state = ProcessContext::kPlaying;
    p.data.processContext = &p.context;
    if (p.component->setActive(true) != kResultOk)
        return false;
    p.active = true;
    p.processor->setProcessing(true); // some plug-ins answer kNotImplemented
    p.maxBlock = maxBlock;
    p.channels = channels;
    {
        std::lock_guard<std::mutex> lock(p.paramMutex);
        p.pending = p.values; // the processor starts from every known value
    }
    p.prepared = true;
    return true;
}

bool Plugin::isPrepared() const
{
    return d->prepared;
}

void Plugin::setParameter(uint32_t id, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (d->controller)
        d->controller->setParamNormalized(id, normalized);
    std::lock_guard<std::mutex> lock(d->paramMutex);
    d->values[id] = normalized;
    d->pending[id] = normalized;
}

void Plugin::setParameters(const std::map<uint32_t, double> &values)
{
    for (const auto &v : values)
        setParameter(v.first, v.second);
}

void Plugin::process(float *samples, int frames, int channels)
{
    Impl &p = *d;
    if (!p.prepared || !samples || frames <= 0 || channels <= 0)
        return;
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, p.maxBlock);
        float *block = samples + size_t(done) * size_t(channels);
        // Main input: the clip's channels (a missing one repeats the last); side chains silent.
        for (int32 b = 0; b < p.data.numInputs; ++b) {
            AudioBusBuffers &bus = p.data.inputs[b];
            bus.silenceFlags = 0;
            for (int32 c = 0; c < bus.numChannels; ++c) {
                float *dst = bus.channelBuffers32[c];
                if (b == 0) {
                    const int src = std::min<int>(c, channels - 1);
                    for (int i = 0; i < n; ++i)
                        dst[i] = block[i * channels + src];
                } else {
                    std::fill(dst, dst + n, 0.0f);
                }
            }
        }
        {
            std::unique_lock<std::mutex> lock(p.paramMutex, std::try_to_lock);
            if (lock.owns_lock()) { // never wait in the audio thread: the rest goes with the next block
                for (const auto &v : p.pending) {
                    int32 index = 0;
                    if (IParamValueQueue *q = p.inChanges.addParameterData(v.first, index)) {
                        int32 point = 0;
                        q->addPoint(0, v.second, point);
                    }
                }
                p.pending.clear();
            }
        }
        p.data.numSamples = n;
        const bool ok = p.processor->process(p.data) == kResultOk;
        p.inChanges.clearQueue();
        p.context.projectTimeSamples += n;
        if (ok && p.data.numOutputs > 0) {
            const AudioBusBuffers &out = p.data.outputs[0];
            if (out.numChannels > 0)
                for (int c = 0; c < channels; ++c) {
                    const float *src = out.channelBuffers32[std::min<int32>(c, out.numChannels - 1)];
                    for (int i = 0; i < n; ++i)
                        block[i * channels + c] = src[i];
                }
        }
        done += n;
    }
}

void Plugin::reset()
{
    Impl &p = *d;
    if (!p.prepared)
        return;
    p.processor->setProcessing(false);
    p.component->setActive(false);
    p.component->setActive(true);
    p.processor->setProcessing(true);
    p.context.projectTimeSamples = 0;
}

int Plugin::latency() const
{
    return d->processor ? int(d->processor->getLatencySamples()) : 0;
}

#else // !BC_HAVE_VST3: this build cannot host plug-ins

struct Plugin::Impl
{
    PluginInfo info;
};

bool available()
{
    return false;
}
std::vector<std::string> standardBundles()
{
    return {};
}
std::vector<PluginInfo> describeBundle(const std::string &, bool, bool *loaded, std::string *error)
{
    if (loaded)
        *loaded = false;
    if (error)
        *error = "VST 3 support is not built in";
    return {};
}
std::string moduleInfoJson(const std::string &, std::string *error)
{
    if (error)
        *error = "VST 3 support is not built in";
    return {};
}
Plugin::Plugin() : d(new Impl) {}
Plugin::~Plugin() = default;
std::unique_ptr<Plugin> Plugin::create(const std::string &, const std::string &, std::string *error)
{
    if (error)
        *error = "VST 3 support is not built in";
    return nullptr;
}
const PluginInfo &Plugin::info() const { return d->info; }
std::vector<ParamInfo> Plugin::parameters() const { return {}; }
double Plugin::parameter(uint32_t) const { return 0; }
std::string Plugin::valueText(uint32_t, double) const { return {}; }
bool Plugin::prepare(double, int, int) { return false; }
bool Plugin::isPrepared() const { return false; }
void Plugin::setParameter(uint32_t, double) {}
void Plugin::setParameters(const std::map<uint32_t, double> &) {}
void Plugin::process(float *, int, int) {}
void Plugin::reset() {}
int Plugin::latency() const { return 0; }

#endif

} // namespace bc::vst3
