#pragma once

#include "core/Effects.h"

#include <vector>

namespace bc {

// Sound effects of an audio clip (phase 10b): equalizer (low cut, bass
// shelf, middle bell, treble shelf; RBJ biquads) and compressor (stereo
// linked, peak detector, soft knee). Works on interleaved float samples and
// keeps the filter state between calls, so a stream is processed in blocks.
class AudioChain
{
public:
    // Picks the active sound effects; the state is kept when nothing changed.
    void configure(const ClipEffects &effects, int sampleRate, int channels);
    bool isActive() const { return !m_stages.empty(); }
    void reset(); // forget the filter state (after a jump)
    void process(float *samples, int frames);
    // Compressors: how much the last block was turned down, in dB (>= 0).
    double gainReductionDb() const { return m_reduction; }

    static constexpr int kMaxChannels = 8;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[kMaxChannels] = {};
        double z2[kMaxChannels] = {};
    };
    struct Stage
    {
        ClipEffect::Kind kind;
        std::vector<Biquad> filters;              // equalizer
        double threshold = 0, ratio = 1, knee = 6; // compressor (dB)
        double attack = 0, release = 0, makeup = 1;
        double reduction = 0;                      // smoothed, dB
    };

    ClipEffects m_effects;
    int m_rate = 0;
    int m_channels = 0;
    std::vector<Stage> m_stages;
    double m_reduction = 0;
};

} // namespace bc
