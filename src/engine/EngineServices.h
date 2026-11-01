#pragma once

#include <QByteArray>
#include <QImage>

struct mlt_repository_s;

namespace bc::EngineServices {

// Registers BUKHARA CINEMA's own MLT services (all written for this program,
// so no GPL MLT module is needed):
//  - producer "bc_still": a still picture with transparency (photos, titles),
//    shown for the whole clip and fitted into the frame;
//  - filter "bc_gain": clip volume in dB with linear fade in / fade out.
void registerAll(mlt_repository_s *repository);

// Hands an image to the next "bc_still" producer. Create it right away with
// Mlt::Producer(profile, nullptr, resource) so MLT adds its format converters.
QByteArray stillResource(const QImage &image);

} // namespace bc::EngineServices
