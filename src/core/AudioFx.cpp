#include "core/AudioFx.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

enum class Shape { HighPass, LowShelf, Peak, HighShelf };

// Coefficients from Robert Bristow-Johnson's "Audio EQ Cookbook".
void design(double &b0, double &b1, double &b2, double &a1, double &a2, Shape shape, double rate, double freq,
            double gainDb, double q)
{
    freq = std::clamp(freq, 10.0, rate * 0.45);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2 * M_PI * freq / rate;
    const double cw = std::cos(w0), sw = std::sin(w0);
    double a0 = 1;
    switch (shape) {
    case Shape::HighPass: {
        const double alpha = sw / (2 * q);
        b0 = (1 + cw) / 2;
        b1 = -(1 + cw);
        b2 = (1 + cw) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cw;
        a2 = 1 - alpha;
        break;
    }
    case Shape::Peak: {
        const double alpha = sw / (2 * q);
        b0 = 1 + alpha * A;
        b1 = -2 * cw;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cw;
        a2 = 1 - alpha / A;
        break;
    }
    case Shape::LowShelf:
    case Shape::HighShelf: {
        const double alpha = sw / 2 * std::sqrt(2.0); // shelf slope S = 1
        const double sa = 2 * std::sqrt(A) * alpha;
        if (shape == Shape::LowShelf) {
            b0 = A * ((A + 1) - (A - 1) * cw + sa);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sa;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cw + sa);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sa;
        }
        break;
    }
    }
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;
}

double coefficient(double ms, double rate)
{
    return ms <= 0 ? 0 : std::exp(-1.0 / (ms * 0.001 * rate));
}

} // namespace

void AudioChain::configure(const ClipEffects &effects, int sampleRate, int channels)
{
    channels = std::clamp(channels, 1, kMaxChannels);
    if (effects == m_effects && sampleRate == m_rate && channels == m_channels)
        return;
    const bool sameShape = sampleRate == m_rate && channels == m_channels;
    std::vector<Stage> old;
    old.swap(m_stages);
    m_effects = effects;
    m_rate = sampleRate;
    m_channels = channels;
    if (sampleRate <= 0)
        return;
    const double rate = sampleRate;
    for (const ClipEffect &e : effects) {
        if (!ClipEffect::isAudio(e.kind) || e.isNeutral())
            continue;
        Stage s;
        s.kind = e.kind;
        if (e.kind == ClipEffect::Kind::Equalizer) {
            auto add = [&](Shape shape, double freq, double gain, double q) {
                Biquad b;
                design(b.b0, b.b1, b.b2, b.a1, b.a2, shape, rate, freq, gain, q);
                s.filters.push_back(b);
            };
            if (e.value(QStringLiteral("low_cut")) > 0)
                add(Shape::HighPass, e.value(QStringLiteral("low_cut")), 0, M_SQRT1_2);
            if (e.value(QStringLiteral("low_gain")) != 0)
                add(Shape::LowShelf, e.value(QStringLiteral("low_freq")), e.value(QStringLiteral("low_gain")), 0);
            if (e.value(QStringLiteral("mid_gain")) != 0)
                add(Shape::Peak, e.value(QStringLiteral("mid_freq")), e.value(QStringLiteral("mid_gain")),
                    e.value(QStringLiteral("mid_q")));
            if (e.value(QStringLiteral("high_gain")) != 0)
                add(Shape::HighShelf, e.value(QStringLiteral("high_freq")), e.value(QStringLiteral("high_gain")), 0);
        } else {
            s.threshold = e.value(QStringLiteral("threshold"));
            s.ratio = e.value(QStringLiteral("ratio"));
            s.attack = coefficient(e.value(QStringLiteral("attack")), rate);
            s.release = coefficient(e.value(QStringLiteral("release")), rate);
            s.makeup = std::pow(10.0, e.value(QStringLiteral("makeup")) / 20.0);
        }
        m_stages.push_back(std::move(s));
    }
    // Moving a knob must not click: keep the state of stages that stay alike.
    if (sameShape && old.size() == m_stages.size())
        for (size_t i = 0; i < old.size(); ++i) {
            if (old[i].kind != m_stages[i].kind)
                continue;
            m_stages[i].reduction = old[i].reduction;
            if (old[i].filters.size() == m_stages[i].filters.size())
                for (size_t f = 0; f < old[i].filters.size(); ++f)
                    for (int c = 0; c < kMaxChannels; ++c) {
                        m_stages[i].filters[f].z1[c] = old[i].filters[f].z1[c];
                        m_stages[i].filters[f].z2[c] = old[i].filters[f].z2[c];
                    }
        }
}

void AudioChain::reset()
{
    for (Stage &s : m_stages) {
        s.reduction = 0;
        for (Biquad &b : s.filters)
            for (int c = 0; c < kMaxChannels; ++c)
                b.z1[c] = b.z2[c] = 0;
    }
    m_reduction = 0;
}

void AudioChain::process(float *samples, int frames)
{
    if (!samples || frames <= 0 || m_stages.empty())
        return;
    const int ch = m_channels;
    double maxReduction = 0;
    for (Stage &s : m_stages) {
        if (s.kind == ClipEffect::Kind::Equalizer) {
            for (Biquad &b : s.filters)
                for (int i = 0; i < frames; ++i)
                    for (int c = 0; c < ch; ++c) {
                        // Transposed direct form II.
                        float &x = samples[i * ch + c];
                        const double in = x;
                        const double out = b.b0 * in + b.z1[c];
                        b.z1[c] = b.b1 * in - b.a1 * out + b.z2[c];
                        b.z2[c] = b.b2 * in - b.a2 * out;
                        x = float(out);
                    }
            continue;
        }
        // Compressor: the loudest channel decides (stereo image stays).
        for (int i = 0; i < frames; ++i) {
            float peak = 0;
            for (int c = 0; c < ch; ++c)
                peak = std::max(peak, std::abs(samples[i * ch + c]));
            const double level = 20.0 * std::log10(std::max(1e-9, double(peak)));
            const double over = level - s.threshold;
            double target = 0; // dB to take off
            const double slope = 1.0 - 1.0 / s.ratio;
            if (over >= s.knee / 2)
                target = over * slope;
            else if (over > -s.knee / 2)
                target = slope * (over + s.knee / 2) * (over + s.knee / 2) / (2 * s.knee); // soft knee
            const double k = target > s.reduction ? s.attack : s.release;
            s.reduction = k * s.reduction + (1 - k) * target;
            maxReduction = std::max(maxReduction, s.reduction);
            const float g = float(s.makeup * std::pow(10.0, -s.reduction / 20.0));
            for (int c = 0; c < ch; ++c)
                samples[i * ch + c] *= g;
        }
    }
    m_reduction = maxReduction;
}

} // namespace bc
