#pragma once

#include <QString>

#include <atomic>
#include <vector>

namespace bc::Loudness {

// Loudness as in ITU-R BS.1770-4 / EBU R128: K-weighted power in 400 ms
// blocks (75 % overlap), gated at -70 LUFS and 10 LU below the ungated mean.
// A media file is measured once into a profile of 100 ms parts; any source
// range (a trimmed clip) is then measured from the profile at once.
constexpr int kPartsPerSecond = 10;
constexpr double kSilence = -70.0; // LUFS returned when nothing passes the gates

struct Profile
{
    std::vector<float> power; // per 100 ms: channel-weighted mean square after K-weighting
    std::vector<float> peak;  // per 100 ms: highest absolute sample (sample peak, not true peak)
    bool isEmpty() const { return power.empty(); }
};

// Streaming measurement of interleaved float samples.
class Meter
{
public:
    Meter(int channels, int sampleRate);
    void add(const float *samples, int frames);
    // The profile so far (a last, incomplete 100 ms part is left out).
    const Profile &profile() const { return m_profile; }

private:
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double z1 = 0, z2 = 0;
        double run(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    int m_channels;
    int m_partSize;
    int m_fill = 0;
    double m_sum = 0;
    float m_peak = 0;
    std::vector<Biquad> m_shelf, m_highpass; // per channel
    std::vector<double> m_weight;
    Profile m_profile;
};

struct Result
{
    double lufs = kSilence; // integrated loudness
    double peakDb = -120;   // sample peak, dBFS
    bool valid = false;     // false: too short or silent
};

// Power of the 400 ms blocks inside the source range [fromSec, toSec).
std::vector<double> blocks(const Profile &p, double fromSec, double toSec);
// Integrated (gated) loudness of a set of block powers.
double integrate(const std::vector<double> &blockPowers);
double peakDb(const Profile &p, double fromSec, double toSec);
Result measure(const Profile &p, double fromSec, double toSec);

// Decodes the first audio stream with FFmpeg into a profile.
bool compute(const QString &path, Profile &out, QString *error = nullptr,
             const std::atomic_bool *cancel = nullptr);

// Small binary cache file ("BCLD" + version + data).
bool save(const QString &file, const Profile &p);
bool load(const QString &file, Profile &p);

} // namespace bc::Loudness
