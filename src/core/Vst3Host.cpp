#include "core/Vst3Host.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

#ifdef BC_HAVE_VST3
#include "base/source/fobject.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
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
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
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

std::mutex s_settingsMutex;
std::string s_hostProgram;
std::map<std::string, std::string> s_crashes; // "path|uid" -> what happened
// Plug-ins that would not start: not tried again for a while (playback asks every frame).
std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> s_failures;

} // namespace

// --- settings shared with Vst3Remote.cpp

void setHostProgram(const std::string &path)
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    s_hostProgram = path;
}

std::string hostProgram()
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    return s_hostProgram;
}

std::string crashKey(const std::string &path, const std::string &uid)
{
    return path + "|" + lower(uid);
}

std::string crashMessage(const std::string &path, const std::string &uid)
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    auto it = s_crashes.find(crashKey(path, uid));
    return it == s_crashes.end() ? std::string() : it->second;
}

void forgetCrash(const std::string &path, const std::string &uid)
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    s_crashes.erase(crashKey(path, uid));
    s_failures.erase(crashKey(path, uid));
}

// Used by Vst3Remote.cpp: a start that failed, and whether one failed lately.
void noteFailure(const std::string &path, const std::string &uid, const std::string &what)
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    s_failures[crashKey(path, uid)] = {std::chrono::steady_clock::now(), what};
}

bool recentFailure(const std::string &path, const std::string &uid, std::string *what)
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    auto it = s_failures.find(crashKey(path, uid));
    if (it == s_failures.end())
        return false;
    if (std::chrono::steady_clock::now() - it->second.first > std::chrono::seconds(10)) {
        s_failures.erase(it);
        return false;
    }
    if (what)
        *what = it->second.second;
    return true;
}

// Used by the remote plug-ins (Vst3Remote.cpp).
void noteCrash(const std::string &path, const std::string &uid, const std::string &what)
{
    std::lock_guard<std::mutex> lock(s_settingsMutex);
    s_crashes[crashKey(path, uid)] = what.empty() ? std::string("the plug-in stopped") : what;
}

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

void Plugin::setParameters(const std::map<uint32_t, double> &values)
{
    for (const auto &v : values)
        setParameter(v.first, v.second);
}

bool Plugin::openEditor(uint64_t, const std::string &, std::string *error)
{
    if (error)
        *error = "this plug-in has no window of its own here";
    return false;
}

void Plugin::setEditorCallbacks(EditFn, ClosedFn) {}

Plugin::~Plugin() = default;

std::unique_ptr<Plugin> createRemote(const std::string &path, const std::string &uid, std::string *error); // Vst3Remote.cpp

std::unique_ptr<Plugin> Plugin::create(const std::string &path, const std::string &uid, std::string *error)
{
    if (!hostProgram().empty())
        return createRemote(path, uid, error);
    return createLocal(path, uid, error);
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

// What the plug-in's window changes: performEdit and "many values changed".
class Handler : public FObject, public IComponentHandler
{
public:
    std::function<void(ParamID, ParamValue)> edited;
    std::function<void()> reloaded;

    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override
    {
        if (edited)
            edited(id, v);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        if ((flags & kParamValuesChanged) && reloaded)
            reloaded();
        return kResultOk;
    }

    OBJ_METHODS(Handler, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};

#ifdef _WIN32
constexpr DWORD kEditorStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD kEditorExStyle = WS_EX_DLGMODALFRAME;

class Frame : public FObject, public IPlugFrame
{
public:
    HWND hwnd = nullptr;

    tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *r) override
    {
        if (!hwnd || !view || !r)
            return kInvalidArgument;
        RECT rc{0, 0, r->getWidth(), r->getHeight()};
        AdjustWindowRectEx(&rc, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                           DWORD(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
        SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        view->onSize(r);
        return kResultTrue;
    }

    OBJ_METHODS(Frame, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugFrame)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};
#endif

class LocalPlugin;

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

namespace {

// A plug-in running in this process.
class LocalPlugin : public Plugin
{
public:
    PluginInfo m_info;
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    FUnknownPtr<IAudioProcessor> processor;
    IPtr<Handler> handler;

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

    EditFn editedFn;
    ClosedFn closedFn;
    int editorKnown = -1; // -1 not asked yet
#ifdef _WIN32
    IPtr<IPlugView> view;
    IPtr<Frame> frame;
    HWND window = nullptr;
#endif

    ~LocalPlugin() override
    {
        closeEditor();
        stop();
        data.unprepare();
        if (controller)
            controller->setComponentHandler(nullptr);
        processor = nullptr;
        controller = nullptr;
        component = nullptr;
        provider = nullptr; // terminates and releases the plug-in
    }

    void stop()
    {
        if (!active)
            return;
        if (processor)
            processor->setProcessing(false);
        component->setActive(false);
        active = false;
    }

    const PluginInfo &info() const override { return m_info; }

    std::vector<ParamInfo> parameters() const override
    {
        std::vector<ParamInfo> out;
        if (!controller)
            return out;
        const int32 n = controller->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            ParameterInfo pi{};
            if (controller->getParameterInfo(i, pi) != kResultOk)
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

    double parameter(uint32_t id) const override { return controller ? controller->getParamNormalized(id) : 0.0; }

    std::string valueText(uint32_t id, double normalized) const override
    {
        String128 s{};
        if (controller && controller->getParamStringByValue(id, normalized, s) == kResultOk)
            return text(s);
        std::ostringstream o;
        o.precision(3);
        o << normalized;
        return o.str();
    }

    bool prepare(double sampleRate, int ch, int block) override
    {
        stop();
        prepared = false;
        ch = std::clamp(ch, 1, 2);
        const int32 nIn = component->getBusCount(kAudio, kInput);
        const int32 nOut = component->getBusCount(kAudio, kOutput);
        if (nOut < 1)
            return false;
        // The main buses get the clip's layout; the others (side chains) keep theirs, switched off.
        const SpeakerArrangement want = ch == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        std::vector<SpeakerArrangement> ins(static_cast<size_t>(std::max<int32>(nIn, 0)));
        std::vector<SpeakerArrangement> outs(static_cast<size_t>(nOut));
        for (int32 i = 0; i < nIn; ++i)
            processor->getBusArrangement(kInput, i, ins[size_t(i)]);
        for (int32 i = 0; i < nOut; ++i)
            processor->getBusArrangement(kOutput, i, outs[size_t(i)]);
        if (nIn > 0)
            ins[0] = want;
        outs[0] = want;
        processor->setBusArrangements(ins.empty() ? nullptr : ins.data(), nIn, outs.data(), nOut); // may refuse: then theirs
        for (int32 i = 0; i < nIn; ++i)
            component->activateBus(kAudio, kInput, i, i == 0);
        for (int32 i = 0; i < nOut; ++i)
            component->activateBus(kAudio, kOutput, i, i == 0);

        ProcessSetup setup{kRealtime, kSample32, block, sampleRate};
        if (processor->setupProcessing(setup) != kResultOk)
            return false;
        if (!data.prepare(*component, block, kSample32))
            return false;
        data.processMode = kRealtime;
        data.inputParameterChanges = &inChanges;
        context = ProcessContext{};
        context.sampleRate = sampleRate;
        context.state = ProcessContext::kPlaying;
        data.processContext = &context;
        if (component->setActive(true) != kResultOk)
            return false;
        active = true;
        processor->setProcessing(true); // some plug-ins answer kNotImplemented
        maxBlock = block;
        channels = ch;
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            pending = values; // the processor starts from every known value
        }
        prepared = true;
        return true;
    }

    bool isPrepared() const override { return prepared; }

    void setParameter(uint32_t id, double normalized) override
    {
        normalized = std::clamp(normalized, 0.0, 1.0);
        if (controller)
            controller->setParamNormalized(id, normalized);
        std::lock_guard<std::mutex> lock(paramMutex);
        values[id] = normalized;
        pending[id] = normalized;
    }

    void process(float *samples, int frames, int ch) override
    {
        if (!prepared || !samples || frames <= 0 || ch <= 0)
            return;
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, maxBlock);
            float *block = samples + size_t(done) * size_t(ch);
            // Main input: the clip's channels (a missing one repeats the last); side chains silent.
            for (int32 b = 0; b < data.numInputs; ++b) {
                AudioBusBuffers &bus = data.inputs[b];
                bus.silenceFlags = 0;
                for (int32 c = 0; c < bus.numChannels; ++c) {
                    float *dst = bus.channelBuffers32[c];
                    if (b == 0) {
                        const int src = std::min<int>(c, ch - 1);
                        for (int i = 0; i < n; ++i)
                            dst[i] = block[i * ch + src];
                    } else {
                        std::fill(dst, dst + n, 0.0f);
                    }
                }
            }
            {
                std::unique_lock<std::mutex> lock(paramMutex, std::try_to_lock);
                if (lock.owns_lock()) { // never wait in the audio thread: the rest goes with the next block
                    for (const auto &v : pending) {
                        int32 index = 0;
                        if (IParamValueQueue *q = inChanges.addParameterData(v.first, index)) {
                            int32 point = 0;
                            q->addPoint(0, v.second, point);
                        }
                    }
                    pending.clear();
                }
            }
            data.numSamples = n;
            const bool ok = processor->process(data) == kResultOk;
            inChanges.clearQueue();
            context.projectTimeSamples += n;
            if (ok && data.numOutputs > 0) {
                const AudioBusBuffers &out = data.outputs[0];
                if (out.numChannels > 0)
                    for (int c = 0; c < ch; ++c) {
                        const float *src = out.channelBuffers32[std::min<int32>(c, out.numChannels - 1)];
                        for (int i = 0; i < n; ++i)
                            block[i * ch + c] = src[i];
                    }
            }
            done += n;
        }
    }

    void reset() override
    {
        if (!prepared)
            return;
        processor->setProcessing(false);
        component->setActive(false);
        component->setActive(true);
        processor->setProcessing(true);
        context.projectTimeSamples = 0;
    }

    int latency() const override { return processor ? int(processor->getLatencySamples()) : 0; }

    void setEditorCallbacks(EditFn edited, ClosedFn closed) override
    {
        editedFn = std::move(edited);
        closedFn = std::move(closed);
    }

    // A value changed in the window: the controller has it already.
    void edited(ParamID id, ParamValue v)
    {
        {
            std::lock_guard<std::mutex> lock(paramMutex);
            values[id] = v;
            pending[id] = v;
        }
        if (editedFn)
            editedFn(id, v);
    }

    // The plug-in changed many values at once (a preset in its window).
    void reloaded()
    {
        if (!controller)
            return;
        for (const ParamInfo &p : parameters()) {
            if (p.readOnly)
                continue;
            const double v = controller->getParamNormalized(p.id);
            double old = -1;
            {
                std::lock_guard<std::mutex> lock(paramMutex);
                auto it = values.find(p.id);
                if (it != values.end())
                    old = it->second;
            }
            if (std::abs(old - v) > 1e-9)
                edited(p.id, v);
        }
    }

#ifdef _WIN32
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto *self = reinterpret_cast<LocalPlugin *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_CLOSE && self) {
            self->closeEditor();
            if (self->closedFn)
                self->closedFn();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    bool hasEditor() const override
    {
        auto *self = const_cast<LocalPlugin *>(this);
        if (editorKnown < 0) {
            self->editorKnown = 0;
            if (controller) {
                IPtr<IPlugView> v = owned(controller->createView(ViewType::kEditor));
                self->editorKnown = v && v->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue ? 1 : 0;
            }
        }
        return editorKnown == 1;
    }

    bool openEditor(uint64_t owner, const std::string &title, std::string *error) override
    {
        if (window) {
            ShowWindow(window, SW_SHOWNORMAL);
            SetForegroundWindow(window);
            return true;
        }
        if (!controller) {
            if (error)
                *error = "the plug-in has no controller";
            return false;
        }
        view = owned(controller->createView(ViewType::kEditor));
        if (!view || view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
            view = nullptr;
            if (error)
                *error = "this plug-in has no window of its own";
            return false;
        }
        static const wchar_t *kClass = L"BukharaCinemaPluginWindow";
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &LocalPlugin::windowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            wc.lpszClassName = kClass;
            registered = RegisterClassExW(&wc) != 0;
        }
        ViewRect size{0, 0, 400, 300};
        view->getSize(&size);
        RECT rc{0, 0, std::max(size.getWidth(), 100), std::max(size.getHeight(), 50)};
        AdjustWindowRectEx(&rc, kEditorStyle, FALSE, kEditorExStyle);
        HWND ownerWnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(owner));
        if (ownerWnd && !IsWindow(ownerWnd))
            ownerWnd = nullptr; // e.g. a program window that is not a real one (tests)
        const std::u16string wide = StringConvert::convert(title);
        window = CreateWindowExW(kEditorExStyle, kClass, reinterpret_cast<const wchar_t *>(wide.c_str()), kEditorStyle,
                                 CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                 ownerWnd, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        if (!window) {
            view = nullptr;
            if (error)
                *error = "the window could not be made";
            return false;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        frame = owned(new Frame);
        frame->hwnd = window;
        view->setFrame(frame);
        if (view->attached(window, kPlatformTypeHWND) != kResultOk) {
            view->setFrame(nullptr);
            view = nullptr;
            DestroyWindow(window);
            window = nullptr;
            if (error)
                *error = "the plug-in did not open its window";
            return false;
        }
        ShowWindow(window, SW_SHOWNORMAL);
        SetForegroundWindow(window);
        return true;
    }

    void closeEditor() override
    {
        if (!window)
            return;
        if (view) {
            view->removed();
            view->setFrame(nullptr);
            view = nullptr;
        }
        frame = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
        window = nullptr;
    }

    bool editorOpen() const override { return window != nullptr; }
#endif
};

} // namespace

std::unique_ptr<Plugin> Plugin::createLocal(const std::string &path, const std::string &uid, std::string *error)
{
    VST3::Hosting::Module::Ptr module = moduleFor(path, error);
    if (!module)
        return nullptr;
    const auto &factory = module->getFactory();
    for (const auto &c : factory.classInfos()) {
        if (c.category() != kVstAudioEffectClass || !sameUid(c.ID(), uid))
            continue;
        auto p = std::make_unique<LocalPlugin>();
        p->module = module;
        p->provider = owned(new PlugProvider(factory, c, true));
        if (!p->provider->initialize()) {
            if (error)
                *error = "the plug-in could not be started";
            return nullptr;
        }
        p->component = p->provider->getComponentPtr();
        p->controller = p->provider->getControllerPtr();
        p->processor = FUnknownPtr<IAudioProcessor>(p->component);
        if (!p->component || !p->processor) {
            if (error)
                *error = "the plug-in has no audio processor";
            return nullptr;
        }
        if (p->processor->canProcessSampleSize(kSample32) != kResultTrue) {
            if (error)
                *error = "the plug-in cannot process 32-bit float samples";
            return nullptr;
        }
        p->m_info = {path, c.ID().toString(), c.name(),
                     c.vendor().empty() ? factory.info().vendor() : c.vendor(), c.version(), c.subCategoriesString()};
        if (p->controller) {
            LocalPlugin *raw = p.get();
            p->handler = owned(new Handler);
            p->handler->edited = [raw](ParamID id, ParamValue v) { raw->edited(id, v); };
            p->handler->reloaded = [raw] { raw->reloaded(); };
            p->controller->setComponentHandler(p->handler);
        }
        // Start from the controller's defaults, so the processor and the controller agree.
        for (const ParamInfo &pi : p->parameters())
            p->values[pi.id] = p->parameter(pi.id);
        return p;
    }
    if (error)
        *error = "no such audio effect in the module";
    return nullptr;
}

#else // !BC_HAVE_VST3: this build cannot host plug-ins

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
std::unique_ptr<Plugin> Plugin::createLocal(const std::string &, const std::string &, std::string *error)
{
    if (error)
        *error = "VST 3 support is not built in";
    return nullptr;
}

#endif

} // namespace bc::vst3
