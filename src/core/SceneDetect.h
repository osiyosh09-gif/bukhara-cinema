#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <functional>
#include <vector>

namespace bc::SceneDetect {

// Finds where a video changes from one shot to another (a hard cut), without
// the internet or an AI model: each frame is shrunk to 64x36 and compared with
// the frame before it (colour histograms and pixel differences). A cut is a
// frame that differs much more than its neighbours do, so fast motion or a
// camera pan inside one shot is not taken for a cut.

struct Profile
{
    std::vector<qint64> timeUs; // frame time from the start of the file
    std::vector<float> score;   // 0..1: how different from the frame before (first frame: 0)
    bool isEmpty() const { return timeUs.empty(); }
};

// Decodes the video stream of `path` (on the video card when HwAccel allows).
// `progress` gets 0..1.
bool compute(const QString &path, Profile &out, QString *error = nullptr,
             const std::function<void(double)> &progress = {}, const std::atomic_bool *cancel = nullptr);

// Times (microseconds from the file start) where a new shot begins.
// `sensitivity` 0..1: higher finds softer cuts. Cuts closer than `minGapUs`
// to the previous one are left out.
QList<qint64> cuts(const Profile &p, double sensitivity, qint64 minGapUs = 500000);

// The difference score of two small RGB pictures (64x36x3 bytes); exposed for tests.
float difference(const unsigned char *a, const unsigned char *b);

bool save(const QString &file, const Profile &p);
bool load(const QString &file, Profile &p);

constexpr int kWidth = 64;
constexpr int kHeight = 36;

} // namespace bc::SceneDetect
