// BukharaPluginHost (phase 15c-2): runs one VST 3 plug-in for BUKHARA CINEMA
// in a process of its own, so a plug-in that crashes or hangs cannot take the
// editor down. Started by core/Vst3Remote.cpp with the two streams to use:
//   Windows: BukharaPluginHost --pipe <request pipe> <event pipe>
//   POSIX:   BukharaPluginHost --fd <request fd> <event fd>
// VST is a registered trademark of Steinberg Media Technologies GmbH.

#include "core/Vst3Host.h"
#include "core/Vst3Ipc.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <ole2.h>
#endif

namespace bc::vst3 {
void writePluginInfo(ipc::Writer &w, const PluginInfo &i); // core/Vst3Remote.cpp
}

using namespace bc::vst3;
using namespace bc::vst3::ipc;

namespace {

Channel g_req;
Channel g_evt;
std::mutex g_evtMutex;
std::unique_ptr<Plugin> g_plugin;
std::vector<float> g_buffer;

void event(uint32_t type, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_evtMutex);
    send(g_evt, type, payload);
}

// Handles one request; false for Quit. `answer` stays unset for posts.
bool handle(uint32_t type, const std::string &payload, bool *answered, std::string *answer)
{
    Reader r(payload);
    Writer w;
    *answered = type < SetParams;
    switch (type) {
    case Describe: {
        const std::string path = r.str();
        bool loaded = false;
        std::string error;
        const std::vector<PluginInfo> infos = describeBundle(path, true, &loaded, &error);
        w.str(error).put<uint32_t>(uint32_t(infos.size()));
        for (const PluginInfo &i : infos)
            writePluginInfo(w, i);
        break;
    }
    case Create: {
        const std::string path = r.str();
        const std::string uid = r.str();
        std::string error;
        g_plugin = Plugin::createLocal(path, uid, &error);
        if (!g_plugin) {
            w.put<uint8_t>(0).str(error.empty() ? "the plug-in could not be started" : error);
            break;
        }
        g_plugin->setEditorCallbacks(
            [](uint32_t id, double v) { event(Edited, Writer().put<uint32_t>(id).put<double>(v).data()); },
            [] { event(EditorClosed, {}); });
        w.put<uint8_t>(1).str({});
        writePluginInfo(w, g_plugin->info());
        const std::vector<ParamInfo> params = g_plugin->parameters();
        w.put<uint32_t>(uint32_t(params.size()));
        for (const ParamInfo &p : params) {
            const uint8_t flags = uint8_t((p.readOnly ? 1 : 0) | (p.hidden ? 2 : 0) | (p.bypass ? 4 : 0));
            w.put<uint32_t>(p.id).str(p.title).str(p.units).put<double>(p.defaultValue).put<int32_t>(p.stepCount);
            w.put<uint8_t>(flags).put<double>(g_plugin->parameter(p.id));
        }
        break;
    }
    case Param: {
        const uint32_t id = r.get<uint32_t>();
        w.put<double>(g_plugin ? g_plugin->parameter(id) : 0.0);
        break;
    }
    case ValueText: {
        const uint32_t id = r.get<uint32_t>();
        const double v = r.get<double>();
        w.str(g_plugin ? g_plugin->valueText(id, v) : std::string());
        break;
    }
    case Prepare: {
        const double rate = r.get<double>();
        const int channels = r.get<int32_t>();
        const int block = r.get<int32_t>();
        const bool ok = g_plugin && g_plugin->prepare(rate, channels, block);
        w.put<uint8_t>(ok ? 1 : 0).put<int32_t>(g_plugin ? g_plugin->latency() : 0);
        break;
    }
    case Process: {
        const int frames = r.get<int32_t>();
        const int channels = r.get<int32_t>();
        const size_t n = size_t(std::max(0, frames)) * size_t(std::max(0, channels));
        g_buffer.resize(n);
        if (n > 0 && r.raw(g_buffer.data(), n * sizeof(float)) && g_plugin)
            g_plugin->process(g_buffer.data(), frames, channels);
        w.raw(g_buffer.data(), n * sizeof(float));
        break;
    }
    case Latency:
        w.put<int32_t>(g_plugin ? g_plugin->latency() : 0);
        break;
    case HasEditor:
        w.put<uint8_t>(g_plugin && g_plugin->hasEditor() ? 1 : 0);
        break;
    case OpenEditor: {
        const uint64_t owner = r.get<uint64_t>();
        const std::string title = r.str();
        std::string error;
        const bool ok = g_plugin && g_plugin->openEditor(owner, title, &error);
        w.put<uint8_t>(ok ? 1 : 0).str(error);
        break;
    }
    case SetParams: {
        const uint32_t n = r.get<uint32_t>();
        std::map<uint32_t, double> values;
        for (uint32_t i = 0; i < n && r.ok(); ++i) {
            const uint32_t id = r.get<uint32_t>();
            values[id] = r.get<double>();
        }
        if (g_plugin && r.ok())
            g_plugin->setParameters(values);
        break;
    }
    case Reset:
        if (g_plugin)
            g_plugin->reset();
        break;
    case CloseEditor:
        if (g_plugin)
            g_plugin->closeEditor();
        break;
    case Quit:
        return false;
    default:
        *answered = false;
        break;
    }
    if (*answered)
        *answer = w.data();
    return true;
}

// Reads requests until Quit or until BUKHARA CINEMA goes away; `run` does
// the work (on Windows on the window thread, where plug-in windows live).
template <typename Run>
void serve(Run run)
{
    for (;;) {
        uint32_t type = 0;
        std::string payload;
        if (!receive(g_req, &type, &payload, -1))
            return;
        bool answered = false;
        std::string answer;
        bool more = true;
        run([&] { more = handle(type, payload, &answered, &answer); });
        if (answered && !send(g_req, type, answer))
            return;
        if (!more)
            return;
    }
}

} // namespace

#ifdef _WIN32

namespace {

constexpr UINT kRun = WM_APP + 1;

LRESULT CALLBACK workerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == kRun) {
        (*reinterpret_cast<std::function<void()> *>(wp))();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HANDLE openPipe(const char *name, DWORD access)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w.data(), n);
    return CreateFileW(w.c_str(), access, 0, nullptr, OPEN_EXISTING, 0, nullptr);
}

} // namespace

int main(int argc, char **argv)
{
    // A crashing plug-in must just end this process: no "stopped working" box.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc < 4 || std::strcmp(argv[1], "--pipe") != 0)
        return 2;
    HANDLE req = openPipe(argv[2], GENERIC_READ | GENERIC_WRITE);
    HANDLE evt = openPipe(argv[3], GENERIC_WRITE);
    if (req == INVALID_HANDLE_VALUE || evt == INVALID_HANDLE_VALUE)
        return 3;
    g_req.adopt(req, false);
    g_evt.adopt(evt, false);
    OleInitialize(nullptr); // plug-in windows may use drag and drop
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = workerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BukharaPluginHostWorker";
    RegisterClassExW(&wc);
    HWND worker = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!worker)
        return 4;
    send(g_req, Hello, Writer().put<uint32_t>(kVersion).data());
    const DWORD mainThread = GetCurrentThreadId();
    std::thread reader([worker, mainThread] {
        serve([worker](const std::function<void()> &fn) {
            // Runs on the window thread; SendMessage waits for it.
            SendMessageW(worker, kRun, reinterpret_cast<WPARAM>(&fn), 0);
        });
        PostThreadMessageW(mainThread, WM_QUIT, 0, 0);
    });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_plugin)
        g_plugin->closeEditor();
    g_plugin.reset();
    reader.join(); // it has finished: it ended the loop
    OleUninitialize();
    return 0;
}

#else

int main(int argc, char **argv)
{
    if (argc < 4 || std::strcmp(argv[1], "--fd") != 0)
        return 2;
    g_req.adopt(std::atoi(argv[2]));
    g_evt.adopt(std::atoi(argv[3]));
    send(g_req, Hello, Writer().put<uint32_t>(kVersion).data());
    serve([](const std::function<void()> &fn) { fn(); });
    g_plugin.reset();
    return 0;
}

#endif
