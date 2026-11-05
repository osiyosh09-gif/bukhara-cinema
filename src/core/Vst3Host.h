#pragma once

// Hosting of VST 3 audio effects (phase 15c), on the Steinberg VST 3 SDK
// (MIT licence). Plain C++ without Qt, so it can be built and tested with the
// SDK alone. VST is a registered trademark of Steinberg Media Technologies GmbH.

#include <cstdint>
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
std::vector<PluginInfo> describeBundle(const std::string &path, bool allowLoad, bool *loaded, std::string *error);
// The moduleinfo.json a bundle should carry (loads the module), as the
// SDK's moduleinfotool writes it; empty on failure. For tests and tools.
std::string moduleInfoJson(const std::string &path, std::string *error);

// One running instance of a plug-in: its processor and its controller.
// Parameters are normalized (0..1) as in VST 3. Audio calls (prepare,
// process, reset) come from one thread; setParameter may come from any.
class Plugin
{
public:
    static std::unique_ptr<Plugin> create(const std::string &path, const std::string &uid, std::string *error);
    ~Plugin();
    Plugin(const Plugin &) = delete;
    Plugin &operator=(const Plugin &) = delete;

    const PluginInfo &info() const;
    std::vector<ParamInfo> parameters() const;
    double parameter(uint32_t id) const;                         // the controller's value
    std::string valueText(uint32_t id, double normalized) const; // as the plug-in shows it

    // (Re)starts processing for a sample rate, channel count (1 or 2 work
    // everywhere) and largest block. False when the plug-in refuses.
    bool prepare(double sampleRate, int channels, int maxBlock);
    bool isPrepared() const;
    // Sets a parameter on the controller and queues it for the processor.
    void setParameter(uint32_t id, double normalized);
    void setParameters(const std::map<uint32_t, double> &values);
    // Interleaved float samples in place; any number of frames.
    void process(float *samples, int frames, int channels);
    // Forgets the sound in flight (after a jump).
    void reset();
    int latency() const;

private:
    Plugin();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace bc::vst3
