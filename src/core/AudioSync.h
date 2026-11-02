#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <vector>

namespace bc::AudioSync {

// Sound envelopes for lining up recordings of the same event (multicamera,
// phase 12a): the RMS level of the mono mix in 10 ms steps, in file times.
constexpr int kRate = 100; // envelope values per second

// Decodes the first audio stream with FFmpeg. Returns false (and fills
// *error) when the file has no readable audio.
bool envelope(const QString &path, std::vector<float> &env, QString *error = nullptr,
              const std::atomic_bool *cancel = nullptr);

struct Match
{
    double offset = 0;     // seconds: where time 0 of `other` falls on the time of `ref`
    double confidence = 0; // 0..1: how clearly the best place beats the others
    bool valid = false;
};

// Finds where `other` lines up with `ref` by cross-correlating their
// level changes (log level minus its 1 s average). Lags up to `maxLagSec`
// either way are tried (0 = any lag where the two overlap).
Match match(const std::vector<float> &ref, const std::vector<float> &other, double maxLagSec = 0);

// Start of each recording relative to the first one with sound (seconds;
// that one is 0, others may be negative). Recordings without sound (empty
// envelope) or without a clear match get offset 0 and valid = false.
QList<Match> align(const QList<std::vector<float>> &envelopes, double maxLagSec = 0);

} // namespace bc::AudioSync
