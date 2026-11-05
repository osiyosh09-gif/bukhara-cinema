#pragma once

// Hosting of VST 3 audio effects (phase 15c), on the Steinberg VST 3 SDK
// (MIT licence). Plain C++ without Qt, so it can be built and tested with the
// SDK alone. VST is a registered trademark of Steinberg Media Technologies GmbH.
//
// Phase 15c-2: when a host program is set (setHostProgram), every plug-in runs
// in its own copy of that small program (BukharaPluginHost). A plug-in that
// crashes or hangs then takes only that process down: the instance reports
// crashed(), its sound passes unchanged, and the crash is remembered
// (crashMessage) until forgetCrash, so the same plug-in is not started again
// and again. Without a host program everything runs in this process.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bc::vst3 {

// True when this build can host VST 3 plug-ins (BC_HAVE_VST3).
bool available();

struct PluginInfo
{
    std::string path;          // the .vst3 bundle (folder) or file
    std::string uid;           // class id, 32 hex digits
    std::string name;
    std::string vendor;
    std::string version;
    std::string subCategories; // e.g. "Fx|Dynamics"
};

struct ParamInfo
{
    uint32_t id = 0;
    std::string title;
    std::string units;
    double defaultValue = 0; // normalized 0..1
    int32_t stepCount = 0;   // 0 = continuous, 1 = on/off, n = n + 1 choices
    bool readOnly = false;
    bool hidden = false;
    bool bypass = false;
};

// .vst3 bundles in the standard VST 3 folders of this system.
std::vector<std::string> standardBundles();
// .vst3 bundles inside a folder (searched recursively).
std::vector<std::string> findBundles(const std::string &folder);

// The audio effects of a bundle, from Contents/Resources/moduleinfo.json
// when there (nothing is loaded). Without it the module is loaded only when
// `allowLoad`; *loaded tells whether that happened. Instruments are left out.
// The module is loaded in this process.
std::vector<PluginInfo> describeBundle(const std::string &path, bool allowLoad, bool *loaded, std::string *error);
// The same, but a module that has to be loaded is loaded by the host program
// (when set): a crash while reading it sets *crashed instead of ending this
// program. Without a host program it is describeBundle(path, true, ...).
std::vector<PluginInfo> describeBundleIsolated(const std::string &path, bool *crashed, std::string *error);
// The moduleinfo.json a bundle should carry (loads the module), as the
// SDK's moduleinfotool writes it; empty on failure. For tests and tools.
std::string moduleInfoJson(const std::string &path, std::string *error);

// --- the host program (phase 15c-2)
void setHostProgram(const std::string &path); // empty: plug-ins run in this process
std::string hostProgram();
// How long the host program may take: to answer one block of sound, and to
// load a plug-in or open its window. Longer means hung: it is ended.
void setTimeouts(int processMs, int loadMs);
// Plug-ins that crashed or hung in this run, by "path|uid".
std::string crashMessage(const std::string &path, const std::string &uid); // empty: none
void forgetCrash(const std::string &path, const std::string &uid);
std::string crashKey(const std::string &path, const std::string &uid);

// One running instance of a plug-in: its processor and its controller.
// Parameters are normalized (0..1) as in VST 3. Audio calls (prepare,
// process, reset) come from one thread; setParameter may come from any.
class Plugin
{
public:
    // In the host program when one is set, else in this process.
    static std::unique_ptr<Plugin> create(const std::string &path, const std::string &uid, std::string *error);
    // Always in this process (the host program itself, tests).
    static std::unique_ptr<Plugin> createLocal(const std::string &path, const std::string &uid, std::string *error);
    virtual ~Plugin();
    Plugin(const Plugin &) = delete;
    Plugin &operator=(const Plugin &) = delete;

    virtual const PluginInfo &info() const = 0;
    virtual std::vector<ParamInfo> parameters() const = 0;
    virtual double parameter(uint32_t id) const = 0;                         // the controller's value
    virtual std::string valueText(uint32_t id, double normalized) const = 0; // as the plug-in shows it

    // (Re)starts processing for a sample rate, channel count (1 or 2 work
    // everywhere) and largest block. False when the plug-in refuses.
    virtual bool prepare(double sampleRate, int channels, int maxBlock) = 0;
    virtual bool isPrepared() const = 0;
    // Sets a parameter on the controller and queues it for the processor.
    virtual void setParameter(uint32_t id, double normalized) = 0;
    virtual void setParameters(const std::map<uint32_t, double> &values);
    // Interleaved float samples in place; any number of frames. A crashed
    // plug-in leaves them as they are.
    virtual void process(float *samples, int frames, int channels) = 0;
    // Forgets the sound in flight (after a jump).
    virtual void reset() = 0;
    virtual int latency() const = 0;

    // --- the plug-in's own window (phase 15c-2; Windows only)
    virtual bool hasEditor() const { return false; }
    // `owner` is the native window it stays above (HWND), or 0.
    virtual bool openEditor(uint64_t owner, const std::string &title, std::string *error);
    virtual void closeEditor() {}
    virtual bool editorOpen() const { return false; }
    // What the user changes in the window (id, normalized value) and that it
    // was closed; called from another thread.
    using EditFn = std::function<void(uint32_t id, double value)>;
    using ClosedFn = std::function<void()>;
    virtual void setEditorCallbacks(EditFn edited, ClosedFn closed);

    // The host program ended (crash, hang): nothing works any more.
    virtual bool crashed() const { return false; }
    virtual bool isRemote() const { return false; }

protected:
    Plugin() = default;
};

} // namespace bc::vst3
