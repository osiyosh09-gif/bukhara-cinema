#include "core/AudioFx.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

enum class Shape { HighPass, LowShelf, Peak, HighShelf, Notch };

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
    case Shape::Notch: {
        const double alpha = sw / (2 * q);
        b0 = 1;
        b1 = -2 * cw;
        b2 = 1;
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
        } else if (e.kind == ClipEffect::Kind::HumRemoval) {
            const double base = e.value(QStringLiteral("hum_freq"));
            const double width = e.value(QStringLiteral("notch_width"));
            const int harmonics = int(std::lround(e.value(QStringLiteral("harmonics"))));
            for (int h = 1; h <= harmonics && base * h < rate * 0.45; ++h) {
                Biquad b;
                design(b.b0, b.b1, b.b2, b.a1, b.a2, Shape::Notch, rate, base * h, 0, base * h / width);
                s.filters.push_back(b);
            }
        } else if (e.kind == ClipEffect::Kind::NoiseReduction) {
            s.nrReduction = e.value(QStringLiteral("reduction"));
            s.nrSensitivity = e.value(QStringLiteral("sensitivity"));
            s.denoiser = std::make_shared<Denoiser>(channels, sampleRate);
            s.denoiser->setParams(s.nrReduction, s.nrSensitivity);
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
            if (old[i].denoiser && m_stages[i].denoiser) {
                // Keep the learned noise and the sound in flight, with the new values.
                old[i].denoiser->setParams(m_stages[i].nrReduction, m_stages[i].nrSensitivity);
                m_stages[i].denoiser = old[i].denoiser;
            }
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
        if (s.denoiser)
            s.denoiser->reset();
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
        if (s.denoiser) {
            s.denoiser->process(samples, frames);
            continue;
        }
        if (!s.filters.empty() || s.kind == ClipEffect::Kind::Equalizer) {
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

int AudioChain::latency() const
{
    int n = 0;
    for (const Stage &s : m_stages)
        if (s.denoiser)
            n += Denoiser::kLatency;
    return n;
}

// ---------------------------------------------------------------------------
// FFT

void fft(std::vector<std::complex<float>> &a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / double(len) * (inverse ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * std::complex<float>(w);
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto &x : a)
            x /= float(n);
}

// ---------------------------------------------------------------------------
// Denoiser

Denoiser::Denoiser(int channels, int sampleRate)
    : m_channels(std::clamp(channels, 1, AudioChain::kMaxChannels))
    , m_window(kSize)
    , m_buf(kSize)
    , m_ch(size_t(m_channels))
{
    // sqrt-Hann on both sides: their product (Hann) adds up to 2 at 75 % overlap.
    for (int i = 0; i < kSize; ++i)
        m_window[size_t(i)] = float(std::sqrt(0.5 - 0.5 * std::cos(2 * M_PI * i / kSize)));
    // The noise floor may rise by about 3 dB a second (it falls at once).
    const double hopSeconds = double(kHop) / std::max(1, sampleRate);
    m_rise = std::pow(10.0, 0.3 * hopSeconds);
    reset();
}

void Denoiser::setParams(double reductionDb, double sensitivity)
{
    m_floor = std::pow(10.0, -std::clamp(reductionDb, 0.0, 60.0) / 20.0);
    m_over = 1.0 + std::clamp(sensitivity, 0.0, 100.0) / 100.0 * 3.0; // 1 .. 4
}

void Denoiser::reset()
{
    m_fill = 0;
    for (Channel &c : m_ch) {
        c.in.assign(kSize, 0);
        c.acc.assign(kSize, 0);
        c.out.assign(kHop, 0);
        c.power.assign(kSize / 2 + 1, 0);
        c.noise.assign(kSize / 2 + 1, 0);
        c.gain.assign(kSize / 2 + 1, 1);
        c.learned = false;
        c.hops = 0;
    }
}

void Denoiser::process(float *samples, int frames)
{
    const int ch = m_channels;
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < ch; ++c) {
            Channel &st = m_ch[size_t(c)];
            float &x = samples[i * ch + c];
            st.in[size_t(kSize - kHop + m_fill)] = x;
            x = st.out[size_t(m_fill)];
        }
        if (++m_fill == kHop) {
            m_fill = 0;
            for (Channel &st : m_ch)
                hop(st);
        }
    }
}

void Denoiser::hop(Channel &c)
{
    const int bins = kSize / 2 + 1;
    for (int i = 0; i < kSize; ++i)
        m_buf[size_t(i)] = c.in[size_t(i)] * m_window[size_t(i)];
    fft(m_buf, false);
    for (int k = 0; k < bins; ++k) {
        const float p = std::norm(m_buf[size_t(k)]);
        // Smoothed power, and the noise estimate: it falls quickly, follows levels close
        // to it (noise-like), and rises only slowly while a louder sound plays.
        float &ps = c.power[size_t(k)];
        ps = c.learned ? 0.6f * ps + 0.4f * p : p;
        float &n = c.noise[size_t(k)];
        if (c.hops < kLearnHops) // the first moment: the plain average
            n = (n * float(c.hops) + p) / float(c.hops + 1);
        else if (ps < n)
            n = 0.85f * n + 0.15f * ps;
        else if (ps < 4.0f * n)
            n = 0.93f * n + 0.07f * ps;
        else
            n = float(n * m_rise);
        // Spectral subtraction gain, limited, then held so it does not flutter.
        const double ratio = c.power[size_t(k)] > 1e-20 ? n / c.power[size_t(k)] : 1.0;
        const double g = std::max(m_floor, 1.0 - m_over * ratio);
        float &held = c.gain[size_t(k)];
        held = float(g >= held ? 0.4 * held + 0.6 * g : 0.6 * held + 0.4 * g);
    }
    c.learned = true;
    if (c.hops < kLearnHops)
        ++c.hops;
    for (int k = 0; k < bins; ++k) {
        // A little smoothing across neighbouring bins against "musical noise".
        const float left = c.gain[size_t(std::max(0, k - 1))], right = c.gain[size_t(std::min(bins - 1, k + 1))];
        const float g = 0.25f * left + 0.5f * c.gain[size_t(k)] + 0.25f * right;
        m_buf[size_t(k)] *= g;
        if (k > 0 && k < kSize / 2)
            m_buf[size_t(kSize - k)] = std::conj(m_buf[size_t(k)]);
    }
    fft(m_buf, true);
    for (int i = 0; i < kSize; ++i)
        c.acc[size_t(i)] += m_buf[size_t(i)].real() * m_window[size_t(i)] * 0.5f;
    // The first hop of the sum is complete: it is the next output.
    std::copy(c.acc.begin(), c.acc.begin() + kHop, c.out.begin());
    std::copy(c.acc.begin() + kHop, c.acc.end(), c.acc.begin());
    std::fill(c.acc.end() - kHop, c.acc.end(), 0.0f);
    std::copy(c.in.begin() + kHop, c.in.end(), c.in.begin());
}

} // namespace bc
