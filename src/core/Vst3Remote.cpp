// Plug-ins in the host program (phase 15c-2): one BukharaPluginHost process
// per plug-in instance, spoken to through two byte streams (core/Vst3Ipc).

#include "core/Vst3Host.h"
#include "core/Vst3Ipc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <string>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace bc::vst3 {

// Vst3Host.cpp
void noteCrash(const std::string &path, const std::string &uid, const std::string &what);
void noteFailure(const std::string &path, const std::string &uid, const std::string &what);
bool recentFailure(const std::string &path, const std::string &uid, std::string *what);

namespace {

std::atomic<int> s_processTimeout{3000};
std::atomic<int> s_loadTimeout{30000};
std::atomic<int> s_counter{0};

using namespace ipc;

// A running host program and the two streams to it.
class Session
{
public:
    Channel req;
    Channel evt;

    ~Session() { finish(0); }

    bool start(std::string *error)
    {
        const std::string exe = hostProgram();
        if (exe.empty()) {
            if (error)
                *error = "no plug-in host program";
            return false;
        }
#ifdef _WIN32
        const std::wstring exeW = widen(exe);
        const std::wstring base = L"\\\\.\\pipe\\bukhara-vst-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                                  + std::to_wstring(++s_counter);
        const std::wstring reqName = base + L"-req";
        const std::wstring evtName = base + L"-evt";
        const DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
        HANDLE r = CreateNamedPipeW(reqName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    mode, 1, 1 << 20, 1 << 20, 0, nullptr);
        HANDLE e = CreateNamedPipeW(evtName.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    mode, 1, 1 << 16, 1 << 16, 0, nullptr);
        if (r == INVALID_HANDLE_VALUE || e == INVALID_HANDLE_VALUE) {
            if (r != INVALID_HANDLE_VALUE)
                CloseHandle(r);
            if (e != INVALID_HANDLE_VALUE)
                CloseHandle(e);
            if (error)
                *error = "could not make a pipe to the plug-in host";
            return false;
        }
        req.adopt(r, true);
        evt.adopt(e, true);
        std::wstring cmd = L"\"" + exeW + L"\" --pipe " + reqName + L" " + evtName;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exeW.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &si, &pi)) {
            if (error)
                *error = "could not start " + exe;
            return false;
        }
        CloseHandle(pi.hThread);
        m_process = pi.hProcess;
        m_pid = pi.dwProcessId;
        // The host ends with this program, even when this program is killed.
        static HANDLE job = [] {
            HANDLE j = CreateJobObjectW(nullptr, nullptr);
            if (j) {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
            }
            return j;
        }();
        if (job)
            AssignProcessToJobObject(job, m_process);
        if (!connect(req.handle()) || !connect(evt.handle())) {
            if (error)
                *error = "the plug-in host did not answer";
            kill();
            return false;
        }
#else
        int r[2], e[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, r) != 0)
            return fail(error, "could not make a socket to the plug-in host");
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, e) != 0) {
            ::close(r[0]);
            ::close(r[1]);
            return fail(error, "could not make a socket to the plug-in host");
        }
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, r[1], 3);
        posix_spawn_file_actions_adddup2(&fa, e[1], 4);
        std::string a0 = exe, a1 = "--fd", a2 = "3", a3 = "4";
        char *argv[] = {a0.data(), a1.data(), a2.data(), a3.data(), nullptr};
        pid_t pid = 0;
        const int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        ::close(r[1]);
        ::close(e[1]);
        req.adopt(r[0]);
        evt.adopt(e[0]);
        if (rc != 0)
            return fail(error, "could not start " + exe);
        m_pid = pid;
#endif
        uint32_t type = 0;
        std::string payload;
        bool timedOut = false;
        if (!receive(req, &type, &payload, s_loadTimeout, &timedOut) || type != Hello) {
            if (error)
                *error = timedOut ? "the plug-in host did not start in time" : "the plug-in host stopped at start";
            kill();
            return false;
        }
        Reader rd(payload);
        if (rd.get<uint32_t>() != kVersion) {
            if (error)
                *error = "the plug-in host is from another version of the program";
            kill();
            return false;
        }
        return true;
    }

    // Ends the host: asks first (`graceMs` to go), then kills.
    void finish(int graceMs)
    {
        if (!running())
            return;
        if (graceMs > 0) {
            send(req, Quit, {}, 500);
#ifdef _WIN32
            if (WaitForSingleObject(m_process, DWORD(graceMs)) == WAIT_OBJECT_0) {
                closeProcess();
                return;
            }
#else
            for (int waited = 0; waited < graceMs; waited += 10) {
                if (waitpid(m_pid, nullptr, WNOHANG) == m_pid) {
                    m_pid = 0;
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
#endif
        }
        kill();
    }

    void kill()
    {
        std::lock_guard<std::mutex> lock(m_killMutex);
#ifdef _WIN32
        if (m_process) {
            TerminateProcess(m_process, 1);
            WaitForSingleObject(m_process, 5000);
            closeProcess();
        }
#else
        if (m_pid > 0) {
            ::kill(m_pid, SIGKILL);
            waitpid(m_pid, nullptr, 0);
            m_pid = 0;
        }
#endif
    }

    bool running() const
    {
#ifdef _WIN32
        return m_process != nullptr;
#else
        return m_pid > 0;
#endif
    }

    unsigned long pid() const { return static_cast<unsigned long>(m_pid); }

#ifdef _WIN32
    static std::wstring widen(const std::string &s)
    {
        if (s.empty())
            return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
        std::wstring w(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
        return w;
    }
#endif

private:
    std::mutex m_killMutex;
#ifdef _WIN32
    HANDLE m_process = nullptr;
    DWORD m_pid = 0;

    void closeProcess()
    {
        CloseHandle(m_process);
        m_process = nullptr;
    }

    bool connect(HANDLE pipe)
    {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool ok = ConnectNamedPipe(pipe, &ov) != FALSE;
        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                ok = true;
            } else if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {ov.hEvent, m_process};
                const DWORD w = WaitForMultipleObjects(2, waits, FALSE, DWORD(s_loadTimeout.load()));
                if (w == WAIT_OBJECT_0) {
                    DWORD done = 0;
                    ok = GetOverlappedResult(pipe, &ov, &done, FALSE) != FALSE;
                } else {
                    CancelIoEx(pipe, &ov);
                    DWORD done = 0;
                    GetOverlappedResult(pipe, &ov, &done, TRUE);
                }
            }
        }
        CloseHandle(ov.hEvent);
        return ok;
    }
#else
    pid_t m_pid = 0;

    static bool fail(std::string *error, const std::string &what)
    {
        if (error)
            *error = what;
        return false;
    }
#endif
};

void putInfo(Writer &w, const PluginInfo &i)
{
    w.str(i.path).str(i.uid).str(i.name).str(i.vendor).str(i.version).str(i.subCategories);
}

PluginInfo getInfo(Reader &r)
{
    PluginInfo i;
    i.path = r.str();
    i.uid = r.str();
    i.name = r.str();
    i.vendor = r.str();
    i.version = r.str();
    i.subCategories = r.str();
    return i;
}

class RemotePlugin : public Plugin
{
public:
    std::string m_path, m_uid;
    Session m_session;
    std::mutex m_call; // one request at a time
    std::atomic<bool> m_dead{false};
    std::atomic<bool> m_closing{false};
    PluginInfo m_info;
    std::vector<ParamInfo> m_params;
    mutable std::mutex m_valueMutex;
    std::map<uint32_t, double> m_values; // as this side knows them
    std::map<uint32_t, double> m_sent;   // as the host has them
    bool m_prepared = false;
    int m_latency = 0;
    mutable int m_hasEditor = -1;
    std::atomic<bool> m_editorOpen{false};
    std::thread m_events;
    std::mutex m_fnMutex;
    EditFn m_edited;
    ClosedFn m_closed;

    ~RemotePlugin() override
    {
        m_closing = true;
        if (!m_dead)
            m_session.finish(2000);
        else
            m_session.kill();
        if (m_events.joinable())
            m_events.join();
    }

    void die(const std::string &what)
    {
        if (m_dead.exchange(true))
            return;
        noteCrash(m_path, m_uid, what);
        m_session.kill();
        if (m_editorOpen.exchange(false)) {
            std::lock_guard<std::mutex> lock(m_fnMutex);
            if (m_closed)
                m_closed();
        }
    }

    // Sends a request and waits for its answer; a failure ends the host.
    bool call(uint32_t type, const std::string &payload, std::string *answer, int timeoutMs) const
    {
        auto *self = const_cast<RemotePlugin *>(this);
        std::lock_guard<std::mutex> lock(self->m_call);
        if (m_dead)
            return false;
        bool timedOut = false;
        uint32_t got = 0;
        if (send(self->m_session.req, type, payload, timeoutMs)
            && receive(self->m_session.req, &got, answer, timeoutMs, &timedOut) && got == type)
            return true;
        if (!m_closing)
            self->die(timedOut ? "the plug-in stopped answering (hung)" : "the plug-in crashed");
        return false;
    }

    void post(uint32_t type, const std::string &payload)
    {
        std::lock_guard<std::mutex> lock(m_call);
        if (m_dead)
            return;
        if (!send(m_session.req, type, payload, s_processTimeout) && !m_closing)
            die("the plug-in crashed");
    }

    const PluginInfo &info() const override { return m_info; }
    std::vector<ParamInfo> parameters() const override { return m_params; }

    double parameter(uint32_t id) const override
    {
        std::lock_guard<std::mutex> lock(m_valueMutex);
        auto it = m_values.find(id);
        return it == m_values.end() ? 0.0 : it->second;
    }

    std::string valueText(uint32_t id, double normalized) const override
    {
        std::string answer;
        if (!call(ValueText, Writer().put<uint32_t>(id).put<double>(normalized).data(), &answer, s_loadTimeout)) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3g", normalized);
            return buf;
        }
        Reader r(answer);
        return r.str();
    }

    bool prepare(double sampleRate, int channels, int maxBlock) override
    {
        std::string answer;
        m_prepared = false;
        if (!call(Prepare, Writer().put<double>(sampleRate).put<int32_t>(channels).put<int32_t>(maxBlock).data(), &answer,
                  s_loadTimeout))
            return false;
        Reader r(answer);
        m_prepared = r.get<uint8_t>() != 0;
        m_latency = r.get<int32_t>();
        return m_prepared;
    }

    bool isPrepared() const override { return m_prepared && !m_dead; }

    void setParameter(uint32_t id, double normalized) override { setParameters({{id, normalized}}); }

    void setParameters(const std::map<uint32_t, double> &values) override
    {
        Writer w;
        uint32_t n = 0;
        Writer items;
        {
            std::lock_guard<std::mutex> lock(m_valueMutex);
            for (const auto &v : values) {
                const double x = std::clamp(v.second, 0.0, 1.0);
                m_values[v.first] = x;
                auto it = m_sent.find(v.first);
                if (it != m_sent.end() && it->second == x)
                    continue; // the host has it: configure() gives every value every frame
                m_sent[v.first] = x;
                items.put<uint32_t>(v.first).put<double>(x);
                ++n;
            }
        }
        if (n == 0)
            return;
        w.put<uint32_t>(n).raw(items.data().data(), items.data().size());
        post(SetParams, w.data());
    }

    void process(float *samples, int frames, int channels) override
    {
        if (m_dead || !m_prepared || !samples || frames <= 0 || channels <= 0)
            return;
        const size_t n = size_t(frames) * size_t(channels);
        Writer w;
        w.put<int32_t>(frames).put<int32_t>(channels).raw(samples, n * sizeof(float));
        std::string answer;
        if (!call(Process, w.data(), &answer, s_processTimeout))
            return; // the sound passes unchanged
        if (answer.size() == n * sizeof(float))
            std::memcpy(samples, answer.data(), answer.size());
    }

    void reset() override { post(Reset, {}); }
    int latency() const override { return m_latency; }

    bool hasEditor() const override
    {
        if (m_hasEditor < 0) {
            std::string answer;
            m_hasEditor = call(HasEditor, {}, &answer, s_loadTimeout) && Reader(answer).get<uint8_t>() != 0 ? 1 : 0;
        }
        return m_hasEditor == 1;
    }

    bool openEditor(uint64_t owner, const std::string &title, std::string *error) override
    {
        startEvents();
#ifdef _WIN32
        AllowSetForegroundWindow(DWORD(m_session.pid())); // its window may come to the front
#endif
        std::string answer;
        if (!call(OpenEditor, Writer().put<uint64_t>(owner).str(title).data(), &answer, s_loadTimeout)) {
            if (error)
                *error = crashMessage(m_path, m_uid);
            return false;
        }
        Reader r(answer);
        const bool ok = r.get<uint8_t>() != 0;
        const std::string err = r.str();
        if (!ok && error)
            *error = err;
        m_editorOpen = ok;
        return ok;
    }

    void closeEditor() override
    {
        if (m_editorOpen.exchange(false))
            post(CloseEditor, {});
    }

    bool editorOpen() const override { return m_editorOpen; }

    void setEditorCallbacks(EditFn edited, ClosedFn closed) override
    {
        {
            std::lock_guard<std::mutex> lock(m_fnMutex);
            m_edited = std::move(edited);
            m_closed = std::move(closed);
        }
        startEvents();
    }

    bool crashed() const override { return m_dead; }
    bool isRemote() const override { return true; }

    void startEvents()
    {
        if (m_events.joinable() || m_dead)
            return;
        m_events = std::thread([this] {
            for (;;) {
                uint32_t type = 0;
                std::string payload;
                if (!receive(m_session.evt, &type, &payload, -1)) {
                    if (!m_closing)
                        die("the plug-in crashed");
                    return;
                }
                Reader r(payload);
                if (type == Edited) {
                    const uint32_t id = r.get<uint32_t>();
                    const double v = r.get<double>();
                    {
                        std::lock_guard<std::mutex> lock(m_valueMutex);
                        m_values[id] = v;
                        m_sent[id] = v; // the host's instance has it already
                    }
                    std::lock_guard<std::mutex> lock(m_fnMutex);
                    if (m_edited)
                        m_edited(id, v);
                } else if (type == EditorClosed) {
                    m_editorOpen = false;
                    std::lock_guard<std::mutex> lock(m_fnMutex);
                    if (m_closed)
                        m_closed();
                }
            }
        });
    }
};

} // namespace

void setTimeouts(int processMs, int loadMs)
{
    s_processTimeout = std::max(100, processMs);
    s_loadTimeout = std::max(500, loadMs);
}

std::unique_ptr<Plugin> createRemote(const std::string &path, const std::string &uid, std::string *error)
{
    const std::string crash = crashMessage(path, uid);
    if (!crash.empty()) {
        if (error)
            *error = crash;
        return nullptr;
    }
    if (recentFailure(path, uid, error))
        return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(path), ec)) {
        if (error)
            *error = path + " is not there";
        noteFailure(path, uid, path + " is not there");
        return nullptr;
    }
    auto p = std::make_unique<RemotePlugin>();
    p->m_path = path;
    p->m_uid = uid;
    std::string err;
    if (!p->m_session.start(&err)) {
        noteFailure(path, uid, err);
        if (error)
            *error = err;
        return nullptr;
    }
    std::string answer;
    if (!p->call(Create, Writer().str(path).str(uid).data(), &answer, s_loadTimeout)) {
        if (error)
            *error = crashMessage(path, uid);
        return nullptr;
    }
    Reader r(answer);
    if (r.get<uint8_t>() == 0) {
        err = r.str();
        noteFailure(path, uid, err);
        if (error)
            *error = err;
        p->m_dead = true; // nothing to talk to; not a crash
        p->m_session.finish(1000);
        return nullptr;
    }
    r.str(); // no error
    p->m_info = getInfo(r);
    const uint32_t n = r.get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        ParamInfo pi;
        pi.id = r.get<uint32_t>();
        pi.title = r.str();
        pi.units = r.str();
        pi.defaultValue = r.get<double>();
        pi.stepCount = r.get<int32_t>();
        const uint8_t flags = r.get<uint8_t>();
        pi.readOnly = flags & 1;
        pi.hidden = flags & 2;
        pi.bypass = flags & 4;
        const double value = r.get<double>();
        p->m_params.push_back(pi);
        p->m_values[pi.id] = value;
        p->m_sent[pi.id] = value;
    }
    if (!r.ok()) {
        if (error)
            *error = "the plug-in host sent a broken answer";
        return nullptr;
    }
    return p;
}

std::vector<PluginInfo> describeBundleIsolated(const std::string &path, bool *crashed, std::string *error)
{
    if (crashed)
        *crashed = false;
    bool loaded = false;
    if (hostProgram().empty())
        return describeBundle(path, true, &loaded, error);
    std::string err;
    std::vector<PluginInfo> out = describeBundle(path, false, &loaded, &err);
    if (err != "no moduleinfo.json") { // read from moduleinfo.json (or VST 3 is not built in)
        if (error)
            *error = err;
        return out;
    }
    Session s;
    if (!s.start(error))
        return {};
    bool timedOut = false;
    uint32_t type = 0;
    std::string answer;
    if (!send(s.req, Describe, Writer().str(path).data(), s_loadTimeout)
        || !receive(s.req, &type, &answer, s_loadTimeout, &timedOut) || type != Describe) {
        if (crashed)
            *crashed = true;
        if (error)
            *error = timedOut ? "the plug-in stopped answering (hung)" : "the plug-in crashed";
        s.kill();
        return {};
    }
    s.finish(1000);
    Reader r(answer);
    if (error)
        *error = r.str();
    const uint32_t n = r.get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok(); ++i)
        out.push_back(getInfo(r));
    return out;
}

// Used by the host program (pluginhost/main.cpp) to write the same layout.
void writePluginInfo(ipc::Writer &w, const PluginInfo &i)
{
    putInfo(w, i);
}

} // namespace bc::vst3
