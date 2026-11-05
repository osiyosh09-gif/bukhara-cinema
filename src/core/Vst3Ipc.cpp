#include "core/Vst3Ipc.h"

#ifndef _WIN32
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>

namespace bc::vst3::ipc {

namespace {
constexpr uint32_t kMaxMessage = 64u << 20; // a block of sound is far smaller
}

#ifdef _WIN32

void Channel::adopt(HANDLE h, bool overlapped)
{
    close();
    m_h = h;
    m_overlapped = overlapped;
}

bool Channel::isOpen() const
{
    return m_h != INVALID_HANDLE_VALUE && m_h != nullptr;
}

void Channel::close()
{
    if (isOpen())
        CloseHandle(m_h);
    m_h = INVALID_HANDLE_VALUE;
}

bool Channel::waitIo(OVERLAPPED &ov, DWORD *done, int timeoutMs, bool *timedOut)
{
    const DWORD r = WaitForSingleObject(ov.hEvent, timeoutMs < 0 ? INFINITE : DWORD(timeoutMs));
    if (r == WAIT_TIMEOUT) {
        CancelIoEx(m_h, &ov);
        GetOverlappedResult(m_h, &ov, done, TRUE);
        if (timedOut)
            *timedOut = true;
        return false;
    }
    return GetOverlappedResult(m_h, &ov, done, TRUE) != FALSE;
}

bool Channel::read(void *out, size_t n, int timeoutMs, bool *timedOut)
{
    if (timedOut)
        *timedOut = false;
    if (!isOpen())
        return false;
    auto *p = static_cast<char *>(out);
    const auto start = std::chrono::steady_clock::now();
    while (n > 0) {
        const DWORD want = DWORD(std::min<size_t>(n, 1u << 20));
        DWORD got = 0;
        if (m_overlapped) {
            int left = timeoutMs;
            if (timeoutMs >= 0) {
                const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start).count();
                left = int(std::max<long long>(0, timeoutMs - spent));
            }
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            bool ok = ReadFile(m_h, p, want, &got, &ov) != FALSE;
            if (!ok && GetLastError() == ERROR_IO_PENDING)
                ok = waitIo(ov, &got, left, timedOut);
            CloseHandle(ov.hEvent);
            if (!ok || got == 0)
                return false;
        } else {
            if (!ReadFile(m_h, p, want, &got, nullptr) || got == 0)
                return false;
        }
        p += got;
        n -= got;
    }
    return true;
}

bool Channel::write(const void *data, size_t n, int timeoutMs)
{
    if (!isOpen())
        return false;
    auto *p = static_cast<const char *>(data);
    while (n > 0) {
        const DWORD want = DWORD(std::min<size_t>(n, 1u << 20));
        DWORD done = 0;
        if (m_overlapped) {
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            bool ok = WriteFile(m_h, p, want, &done, &ov) != FALSE;
            if (!ok && GetLastError() == ERROR_IO_PENDING)
                ok = waitIo(ov, &done, timeoutMs, nullptr);
            CloseHandle(ov.hEvent);
            if (!ok || done == 0)
                return false;
        } else {
            if (!WriteFile(m_h, p, want, &done, nullptr) || done == 0)
                return false;
        }
        p += done;
        n -= done;
    }
    return true;
}

#else // POSIX

void Channel::adopt(int fd)
{
    close();
    m_fd = fd;
}

bool Channel::isOpen() const
{
    return m_fd >= 0;
}

void Channel::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

bool Channel::read(void *out, size_t n, int timeoutMs, bool *timedOut)
{
    if (timedOut)
        *timedOut = false;
    if (m_fd < 0)
        return false;
    auto *p = static_cast<char *>(out);
    const auto start = std::chrono::steady_clock::now();
    while (n > 0) {
        int left = -1;
        if (timeoutMs >= 0) {
            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start).count();
            left = int(std::max<long long>(0, timeoutMs - spent));
        }
        pollfd pfd{m_fd, POLLIN, 0};
        const int r = ::poll(&pfd, 1, left);
        if (r == 0) {
            if (timedOut)
                *timedOut = true;
            return false;
        }
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        const ssize_t got = ::recv(m_fd, p, n, 0);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            return false;
        p += got;
        n -= size_t(got);
    }
    return true;
}

bool Channel::write(const void *data, size_t n, int)
{
    if (m_fd < 0)
        return false;
    auto *p = static_cast<const char *>(data);
    while (n > 0) {
        const ssize_t done = ::send(m_fd, p, n, MSG_NOSIGNAL); // a closed peer must not end us
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            return false;
        p += done;
        n -= size_t(done);
    }
    return true;
}

#endif

bool send(Channel &c, uint32_t type, const std::string &payload, int timeoutMs)
{
    std::string msg;
    msg.reserve(8 + payload.size());
    const uint32_t head[2] = {type, uint32_t(payload.size())};
    msg.append(reinterpret_cast<const char *>(head), sizeof(head));
    msg.append(payload);
    return c.write(msg.data(), msg.size(), timeoutMs);
}

bool receive(Channel &c, uint32_t *type, std::string *payload, int timeoutMs, bool *timedOut)
{
    uint32_t head[2] = {0, 0};
    const auto start = std::chrono::steady_clock::now();
    if (!c.read(head, sizeof(head), timeoutMs, timedOut))
        return false;
    if (head[1] > kMaxMessage)
        return false;
    int left = timeoutMs;
    if (timeoutMs >= 0) {
        const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start).count();
        left = int(std::max<long long>(0, timeoutMs - spent));
    }
    payload->resize(head[1]);
    if (head[1] > 0 && !c.read(payload->data(), head[1], left, timedOut))
        return false;
    *type = head[0];
    return true;
}

} // namespace bc::vst3::ipc
