#pragma once

#include "core/Effects.h"

#include <complex>
#include <memory>
#include <vector>

namespace bc {

// In-place radix-2 FFT (size a power of two); `inverse` scales by 1/n.
void fft(std::vector<std::complex<float>> &data, bool inverse);

// Removes steady background noise (hiss, fan, room tone) by spectral
// gating: short-time FFT (1024 samples, 75 % overlap), a noise floor per
// frequency that follows the quietest level (minimum statistics), and a
// smoothed gain per bin. Delays the sound by kLatency samples.
class Denoiser
{
public:
    static constexpr int kSize = 1024;
    static constexpr int kHop = 256;
    static constexpr int kLatency = kSize;

    Denoiser(int channels, int sampleRate);
    void setParams(double reductionDb, double sensitivity); // 0..40 dB, 0..100
    void reset();
    void process(float *samples, int frames); // interleaved, `channels` wide

private:
    struct Channel
    {
        std::vector<float> in, acc, out; // input window, overlap-add, last hop
        std::vector<float> power, noise, gain;
        bool learned = false;
        int hops = 0; // hops seen, up to kLearnHops
    };
    static constexpr int kLearnHops = 12; // about 64 ms at 48 kHz
    void hop(Channel &c);

    int m_channels;
    int m_fill = 0;
    double m_floor = 0.25;   // lowest gain
    double m_over = 2.0;     // noise over-subtraction
    double m_rise = 1.0;     // noise floor growth per hop
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_buf;
    std::vector<Channel> m_ch;
};

// Sound effects of an audio clip: equalizer (low cut, bass shelf, middle
// bell, treble shelf; RBJ biquads) and compressor (stereo linked, peak
// detector, soft knee) from phase 10b; noise reduction (Denoiser) and hum
// removal (notches at the mains frequency and its harmonics) from phase 10c.
// Works on interleaved float samples and keeps the state between calls, so a
// stream is processed in blocks.
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
    // How many samples the sound comes out later (noise reduction).
    int latency() const;

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
        std::shared_ptr<Denoiser> denoiser;        // noise reduction
        double nrReduction = 0, nrSensitivity = 0;
    };

    ClipEffects m_effects;
    int m_rate = 0;
    int m_channels = 0;
    std::vector<Stage> m_stages;
    double m_reduction = 0;
};

} // namespace bc
