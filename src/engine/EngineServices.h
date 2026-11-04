#pragma once

#include <QByteArray>
#include <QImage>

struct mlt_repository_s;

namespace bc::EngineServices {

// Registers BUKHARA CINEMA's own MLT services (all written for this program,
// so no GPL MLT module is needed):
//  - producer "bc_still": a still picture with transparency (photos, titles),
//    shown for the whole clip and fitted into the frame;
//  - producer "bc_preview": a rendered preview file (phase 13b) as RGBA;
//  - filter "bc_gain": clip volume in dB with linear fade in / fade out and
//    balance ("pan", -1 .. 1; also used for the mixer's track and master faders);
//  - filter "bc_meter": stores the peak of each channel under its "meter" key.
void registerAll(mlt_repository_s *repository);

// Hands an image to the next "bc_still" producer. Create it right away with
// Mlt::Producer(profile, nullptr, resource) so MLT adds its format converters.
QByteArray stillResource(const QImage &image);

// The last peaks (linear, 0 .. 1+) measured by "bc_meter" filters with this
// key; `serial` grows with every measured frame (0 = never measured).
struct MeterLevel
{
    float left = 0;
    float right = 0;
    qint64 serial = 0;
};
MeterLevel meterLevel(const QByteArray &key);
void clearMeters(const QByteArray &prefix); // forget the keys starting with prefix

} // namespace bc::EngineServices
