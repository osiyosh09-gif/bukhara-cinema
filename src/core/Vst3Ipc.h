#pragma once

// Messages between BUKHARA CINEMA and its plug-in host program (phase 15c-2).
// Plain C++ (Win32 or POSIX), shared by both programs.
//
// Two byte streams per plug-in: requests (the program asks, the host answers
// with a message of the same type, or not at all for the "post" kinds) and
// events (the host tells: a value was changed in the plug-in's window, the
// window was closed). A message is [type u32][size u32][payload].

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bc::vst3::ipc {

constexpr uint32_t kVersion = 1;

enum Type : uint32_t {
    // requests (answered)
    Hello = 1,      // host -> program at start: version
    Describe,       // path -> list of PluginInfo
    Create,         // path, uid -> ok, error, info, parameters
    Param,          // id -> value
    ValueText,      // id, value -> text
    Prepare,        // rate, channels, block -> ok
    Process,        // frames, channels, samples -> samples
    Latency,        // -> samples
    HasEditor,      // -> bool
    OpenEditor,     // owner window, title -> ok, error
    // posts (not answered)
    SetParams = 50, // count, (id, value)...
    Reset,
    CloseEditor,
    Quit,
    // events (host -> program)
    Edited = 100, // id, value
    EditorClosed,
};

class Writer
{
public:
    template <typename T>
    Writer &put(T v)
    {
        const char *p = reinterpret_cast<const char *>(&v);
        m_data.append(p, sizeof(T));
        return *this;
    }
    Writer &str(const std::string &s)
    {
        put<uint32_t>(uint32_t(s.size()));
        m_data.append(s);
        return *this;
    }
    Writer &raw(const void *p, size_t n)
    {
        m_data.append(static_cast<const char *>(p), n);
        return *this;
    }
    const std::string &data() const { return m_data; }

private:
    std::string m_data;
};

class Reader
{
public:
    explicit Reader(const std::string &data) : m_p(data.data()), m_end(data.data() + data.size()) {}
    template <typename T>
    T get()
    {
        T v{};
        if (size_t(m_end - m_p) < sizeof(T)) {
            m_ok = false;
            return v;
        }
        std::memcpy(&v, m_p, sizeof(T));
        m_p += sizeof(T);
        return v;
    }
    std::string str()
    {
        const uint32_t n = get<uint32_t>();
        if (!m_ok || size_t(m_end - m_p) < n) {
            m_ok = false;
            return {};
        }
        std::string s(m_p, n);
        m_p += n;
        return s;
    }
    bool raw(void *out, size_t n)
    {
        if (size_t(m_end - m_p) < n) {
            m_ok = false;
            return false;
        }
        std::memcpy(out, m_p, n);
        m_p += n;
        return true;
    }
    bool ok() const { return m_ok; }

private:
    const char *m_p;
    const char *m_end;
    bool m_ok = true;
};

// One end of a byte stream to the other program.
class Channel
{
public:
    Channel() = default;
    ~Channel() { close(); }
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

#ifdef _WIN32
    // `overlapped`: the handle was opened for overlapped I/O (needed for timeouts).
    void adopt(HANDLE h, bool overlapped);
    HANDLE handle() const { return m_h; }
#else
    void adopt(int fd);
    int fd() const { return m_fd; }
#endif
    bool isOpen() const;
    // Exactly n bytes; timeoutMs < 0 waits for ever. *timedOut tells why it failed.
    bool read(void *out, size_t n, int timeoutMs, bool *timedOut = nullptr);
    bool write(const void *data, size_t n, int timeoutMs = -1);
    void close();

private:
#ifdef _WIN32
    HANDLE m_h = INVALID_HANDLE_VALUE;
    bool m_overlapped = false;
    bool waitIo(OVERLAPPED &ov, DWORD *done, int timeoutMs, bool *timedOut);
#else
    int m_fd = -1;
#endif
};

bool send(Channel &c, uint32_t type, const std::string &payload, int timeoutMs = -1);
bool receive(Channel &c, uint32_t *type, std::string *payload, int timeoutMs, bool *timedOut = nullptr);

} // namespace bc::vst3::ipc
