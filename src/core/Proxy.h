#pragma once

#include "core/Project.h"

#include <QString>

#include <atomic>
#include <functional>

namespace bc::Proxy {

// A proxy is a light copy of a big video used for smooth playback on a slow
// computer: 540 lines high, MPEG-4 Part 2 with a key frame every 12 frames
// (cheap to decode and to seek), in Matroska. Sound is copied unchanged, so
// time stamps line up with the original. Export always uses the original.
constexpr int kHeight = 540;

// Videos larger than Full HD get a proxy automatically when the setting is on.
bool recommended(const MediaInfo &info);

// Writes the proxy of `source` to `target` (via a temporary file, so a
// cancelled or failed run leaves nothing behind). `progress` gets 0..1.
bool make(const QString &source, const QString &target, QString *error = nullptr,
          const std::function<void(double)> &progress = {}, const std::atomic_bool *cancel = nullptr);

} // namespace bc::Proxy
