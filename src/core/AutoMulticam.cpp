#include "core/AutoMulticam.h"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <random>

namespace bc::AutoMulticam {

namespace {

constexpr int kSmooth = 40;   // steps averaged (0.4 s): a word, not a syllable
constexpr int kHold = 50;     // steps a new speaker must last (0.5 s) before the cut
constexpr int kLead = 15;     // the cut comes this many steps before the speech
constexpr double kSpeech = 8; // dB above a camera's own background that counts as speech

bool hasPicture(const QList<std::vector<bool>> &picture, int angle, qint64 step)
{
    if (angle < 0)
        return false;
    if (angle >= picture.size())
        return true; // nothing known: always
    const std::vector<bool> &p = picture[angle];
    if (p.empty())
        return true;
    return step >= 0 && step < qint64(p.size()) && p[size_t(step)];
}

double levelAt(const std::vector<float> &env, qint64 i)
{
    return i >= 0 && i < qint64(env.size()) ? double(env[size_t(i)]) : 0.0;
}

// The cuts of a list of per-step choices, as frames.
QList<Cut> toCuts(const std::vector<int> &shown, qint64 i0, double fps, qint64 from, qint64 to)
{
    QList<Cut> cuts;
    for (size_t k = 0; k < shown.size(); ++k) {
        if (k > 0 && shown[k] == shown[k - 1])
            continue;
        qint64 frame = k == 0 ? from : qint64(std::llround(double(i0 + qint64(k)) * fps / kRate));
        frame = std::clamp(frame, from, to - 1);
        if (!cuts.isEmpty() && cuts.last().frame >= frame) {
            cuts.last().angle = shown[k]; // two changes in one frame: the later wins
            continue;
        }
        if (!cuts.isEmpty() && cuts.last().angle == shown[k])
            continue;
        cuts << Cut{frame, shown[k]};
    }
    // Merging may have left neighbours with the same camera.
    QList<Cut> out;
    for (const Cut &c : cuts)
        if (out.isEmpty() || out.last().angle != c.angle)
            out << c;
    return out;
}

} // namespace

Plan plan(const QList<std::vector<float>> &levels, const QList<std::vector<bool>> &picture, double fps,
          qint64 from, qint64 to, const Settings &s)
{
    Plan result;
    const int n = int(std::max(levels.size(), picture.size()));
    if (n <= 0 || fps <= 0 || to <= from)
        return result;
    const qint64 i0 = qint64(std::floor(double(from) / fps * kRate));
    const qint64 i1 = std::max(i0 + 1, qint64(std::ceil(double(to) / fps * kRate)));
    const qint64 steps = i1 - i0;
    const qint64 minSteps = std::max<qint64>(1, qint64(s.minShot * kRate));
    const qint64 maxSteps = s.maxShot > 0 ? std::max(minSteps + 1, qint64(s.maxShot * kRate)) : 0;
    const int wide = s.wideAngle >= 0 && s.wideAngle < n ? s.wideAngle : -1;
    auto pictureOk = [&](int a, qint64 k) { return hasPicture(picture, a, i0 + k); };
    auto anyWith = [&](qint64 k, int except) {
        for (int a = 0; a < n; ++a)
            if (a != except && pictureOk(a, k))
                return a;
        return -1;
    };
    std::vector<int> shown(static_cast<size_t>(steps), 0);

    if (s.mode == Mode::Rhythm) {
        std::mt19937 rng(s.seed);
        const qint64 longest = maxSteps > 0 ? maxSteps : minSteps * 2;
        std::uniform_int_distribution<qint64> length(minSteps, std::max(minSteps, longest));
        int prev = -1, prev2 = -1;
        for (qint64 k = 0; k < steps;) {
            QList<int> choices, fresh;
            for (int a = 0; a < n; ++a)
                if (pictureOk(a, k) && a != prev) {
                    choices << a;
                    if (a != prev2)
                        fresh << a;
                }
            const QList<int> &pool = fresh.size() >= 1 && choices.size() > 2 ? fresh : choices;
            int angle = prev;
            if (!pool.isEmpty())
                angle = pool[int(std::uniform_int_distribution<int>(0, int(pool.size()) - 1)(rng))];
            if (angle < 0)
                angle = 0;
            const qint64 len = length(rng);
            for (qint64 j = k; j < std::min(steps, k + len); ++j)
                shown[size_t(j)] = angle;
            prev2 = prev;
            prev = angle;
            k += len;
        }
    } else {
        // Each camera's level in dB, smoothed, above its own background.
        std::vector<std::vector<double>> rel(static_cast<size_t>(n));
        std::vector<bool> heard(static_cast<size_t>(n), false);
        for (int a = 0; a < n; ++a) {
            if (a >= levels.size() || levels[a].empty())
                continue;
            const std::vector<float> &env = levels[a];
            std::vector<double> power(static_cast<size_t>(steps));
            for (qint64 k = 0; k < steps; ++k) {
                const double v = levelAt(env, i0 + k);
                power[size_t(k)] = v * v;
            }
            std::vector<double> prefix(size_t(steps) + 1, 0.0);
            for (qint64 k = 0; k < steps; ++k)
                prefix[size_t(k) + 1] = prefix[size_t(k)] + power[size_t(k)];
            std::vector<double> db(static_cast<size_t>(steps));
            for (qint64 k = 0; k < steps; ++k) {
                const qint64 lo = std::max<qint64>(0, k - kSmooth / 2);
                const qint64 hi = std::min(steps, k + kSmooth / 2 + 1);
                const double avg = (prefix[size_t(hi)] - prefix[size_t(lo)]) / double(hi - lo);
                db[size_t(k)] = 10.0 * std::log10(avg + 1e-12);
            }
            // The background from the unsmoothed level: short pauses between words count.
            std::vector<double> sorted;
            for (double p : power)
                if (p > 1e-11)
                    sorted.push_back(10.0 * std::log10(p));
            if (sorted.size() < 10)
                continue;
            const size_t q = sorted.size() / 20; // the quietest moments (pauses): this camera's background
            std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(q), sorted.end());
            const double floor = sorted[q];
            rel[size_t(a)].resize(size_t(steps));
            for (qint64 k = 0; k < steps; ++k)
                rel[size_t(a)][size_t(k)] = db[size_t(k)] - floor;
            heard[size_t(a)] = true;
        }
        // Who speaks at each step: -1 nobody (or nobody clearly).
        std::vector<int> speaker(static_cast<size_t>(steps), -1);
        qint64 speech = 0, clear = 0;
        for (qint64 k = 0; k < steps; ++k) {
            int best = -1;
            double first = -1e9, second = -1e9;
            for (int a = 0; a < n; ++a) {
                if (!heard[size_t(a)] || !pictureOk(a, k))
                    continue;
                const double v = rel[size_t(a)][size_t(k)];
                if (v > first) {
                    second = first;
                    first = v;
                    best = a;
                } else if (v > second) {
                    second = v;
                }
            }
            if (best < 0 || first < kSpeech)
                continue;
            ++speech;
            if (second < -1e8 || first - second >= s.margin) {
                speaker[size_t(k)] = best;
                ++clear;
            }
        }
        result.separation = speech > 0 ? double(clear) / double(speech) : 0.0;

        // What the editor wants at each step.
        // What the editor wants at each step: the speaker, else the wide shot,
        // else -2 = no opinion (keep the shot, and keep waiting for a change).
        auto wanted = [&](qint64 k) {
            const int who = speaker[size_t(k)];
            if (who >= 0)
                return who;
            if (wide >= 0 && pictureOk(wide, k))
                return wide;
            return -2;
        };
        // Start on whoever speaks first (within the shortest shot), else the wide shot.
        int current = -1;
        for (qint64 k = 0; k < std::min(steps, minSteps) && current < 0; ++k)
            if (speaker[size_t(k)] >= 0)
                current = speaker[size_t(k)];
        if (current < 0 || !pictureOk(current, 0))
            current = wide >= 0 && pictureOk(wide, 0) ? wide : std::max(0, anyWith(0, -1));
        qint64 shotStart = 0;
        int pending = -1;
        qint64 pendingSince = 0;
        for (qint64 k = 0; k < steps; ++k) {
            const qint64 length = k - shotStart;
            if (!pictureOk(current, k)) { // the camera stopped recording: leave it now
                const int other = wide >= 0 && pictureOk(wide, k) ? wide : anyWith(k, current);
                if (other >= 0) {
                    current = other;
                    shotStart = k;
                    pending = -1;
                }
            } else {
                const int want = wanted(k);
                if (want >= 0 && want != current) {
                    if (want != pending) {
                        pending = want;
                        pendingSince = k;
                    }
                    if (k - pendingSince + 1 >= kHold && length >= minSteps) {
                        // Cut a little before the new speaker began, but not into the shot's minimum.
                        qint64 at = std::max(shotStart + minSteps, pendingSince - kLead);
                        while (at < k && !pictureOk(want, at)) // not before its picture begins
                            ++at;
                        for (qint64 j = at; j < k; ++j)
                            shown[size_t(j)] = want;
                        current = want;
                        shotStart = at;
                        pending = -1;
                    }
                } else {
                    if (want == current)
                        pending = -1; // the shown camera speaks again
                    if (pending < 0 && maxSteps > 0 && length >= maxSteps) {
                        // A long shot: the wide angle, or the camera heard best after this one.
                        int other = wide >= 0 && wide != current && pictureOk(wide, k) ? wide : -1;
                        if (other < 0) {
                            double best = -1e9;
                            for (int a = 0; a < n; ++a)
                                if (a != current && pictureOk(a, k)) {
                                    const double v = heard[size_t(a)] ? rel[size_t(a)][size_t(k)] : -100.0;
                                    if (v > best) {
                                        best = v;
                                        other = a;
                                    }
                                }
                        }
                        if (other >= 0) {
                            current = other;
                            shotStart = k;
                        }
                    }
                }
            }
            shown[size_t(k)] = current;
        }
    }

    result.cuts = toCuts(shown, i0, fps, from, to);
    result.share.fill(0.0, n);
    for (int a : shown)
        if (a >= 0 && a < n)
            result.share[a] += 1.0 / double(steps);
    return result;
}

bool save(const QString &file, const std::vector<float> &env)
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&f);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << quint32(0x42434556) << quint32(1) << quint32(kRate) << quint64(env.size()); // "BCEV"
    for (float v : env)
        out << v;
    return out.status() == QDataStream::Ok && f.commit();
}

bool load(const QString &file, std::vector<float> &env)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&f);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0, rate = 0;
    quint64 count = 0;
    in >> magic >> version >> rate >> count;
    if (magic != 0x42434556 || version != 1 || rate != quint32(kRate) || count > (quint64(1) << 31))
        return false;
    env.resize(size_t(count));
    for (float &v : env)
        in >> v;
    return in.status() == QDataStream::Ok;
}

} // namespace bc::AutoMulticam
