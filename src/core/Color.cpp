#include "core/Color.h"

#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace bc {

QList<EffectParamSpec> ClipColor::specs()
{
    return {{QStringLiteral("temperature"), -100, 100, 0, 1, 0},
            {QStringLiteral("tint"), -100, 100, 0, 1, 0},
            {QStringLiteral("exposure"), -4, 4, 0, 0.1, 1},
            {QStringLiteral("contrast"), -100, 100, 0, 1, 0},
            {QStringLiteral("highlights"), -100, 100, 0, 1, 0},
            {QStringLiteral("shadows"), -100, 100, 0, 1, 0},
            {QStringLiteral("saturation"), 0, 200, 100, 1, 0},
            {QStringLiteral("vibrance"), -100, 100, 0, 1, 0}};
}

double ClipColor::value(const QString &key) const
{
    for (const auto &s : specs())
        if (s.key == key)
            return std::clamp(values.value(key, s.def), s.min, s.max);
    return 0;
}

bool ClipColor::setValue(const QString &key, double v)
{
    for (const auto &s : specs()) {
        if (s.key != key)
            continue;
        v = std::clamp(v, s.min, s.max);
        if (value(key) == v)
            return false;
        if (v == s.def)
            values.remove(key);
        else
            values.insert(key, v);
        return true;
    }
    return false;
}

bool ClipColor::isNeutral() const
{
    for (const auto &s : specs())
        if (value(s.key) != s.def)
            return false;
    return true;
}

bool ClipColor::operator==(const ClipColor &o) const
{
    for (const auto &s : specs())
        if (value(s.key) != o.value(s.key))
            return false;
    return true;
}

QJsonObject ClipColor::toJson() const
{
    QJsonObject o;
    for (const auto &s : specs())
        if (value(s.key) != s.def)
            o[s.key] = value(s.key);
    return o;
}

ClipColor ClipColor::fromJson(const QJsonObject &o)
{
    ClipColor c;
    for (const auto &s : specs())
        if (o.contains(s.key))
            c.setValue(s.key, o.value(s.key).toDouble(s.def));
    return c;
}

QByteArray ClipColor::toEngineString() const
{
    return isNeutral() ? QByteArray() : QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
}

ClipColor ClipColor::fromEngineString(const QByteArray &s)
{
    return s.isEmpty() ? ClipColor() : fromJson(QJsonDocument::fromJson(s).object());
}

namespace {

inline uint8_t toByte(double v)
{
    return uint8_t(std::clamp(int(std::lround(v * 255.0)), 0, 255));
}

} // namespace

void applyColor(uint8_t *img, size_t n, const ClipColor &c)
{
    if (!img || c.isNeutral())
        return;
    const double temp = c.value(QStringLiteral("temperature")) / 100.0;
    const double tint = c.value(QStringLiteral("tint")) / 100.0;
    // White balance as channel gains: warmer = more red, less blue; tint + = magenta (less green).
    const double gain[3] = {1 + 0.25 * temp + 0.1 * tint, 1 - 0.25 * tint, 1 - 0.25 * temp + 0.1 * tint};
    const double exposure = std::pow(2.0, c.value(QStringLiteral("exposure")));
    const double contrast = std::pow(2.0, c.value(QStringLiteral("contrast")) / 100.0); // 0.5 .. 2
    const double highlights = c.value(QStringLiteral("highlights")) / 100.0;
    const double shadows = c.value(QStringLiteral("shadows")) / 100.0;

    // Each channel goes through the same tone curve after its gain: one table per channel.
    uint8_t lut[3][256];
    for (int ch = 0; ch < 3; ++ch)
        for (int i = 0; i < 256; ++i) {
            double v = i / 255.0 * gain[ch] * exposure;
            v = (v - 0.5) * contrast + 0.5;
            v = std::clamp(v, 0.0, 1.0);
            // Shadows act most around 1/4, highlights around 3/4; ends stay in place.
            const double ws = 6.75 * v * (1 - v) * (1 - v); // peak 1 at v = 1/3
            const double wh = 6.75 * v * v * (1 - v);       // peak 1 at v = 2/3
            v += 0.25 * (shadows * ws + highlights * wh);
            lut[ch][i] = toByte(v);
        }

    const double saturation = c.value(QStringLiteral("saturation")) / 100.0;
    const double vibrance = c.value(QStringLiteral("vibrance")) / 100.0;
    const bool chroma = saturation != 1.0 || vibrance != 0.0;
    for (size_t i = 0; i < n; ++i, img += 4) {
        double r = lut[0][img[0]], g = lut[1][img[1]], b = lut[2][img[2]];
        if (chroma) {
            const double l = 0.299 * r + 0.587 * g + 0.114 * b;
            // Vibrance changes dull colours more than strong ones.
            const double sat = (std::max({r, g, b}) - std::min({r, g, b})) / 255.0;
            const double f = saturation * (1 + vibrance * (1 - sat));
            r = l + (r - l) * f;
            g = l + (g - l) * f;
            b = l + (b - l) * f;
        }
        img[0] = uint8_t(std::clamp(int(std::lround(r)), 0, 255));
        img[1] = uint8_t(std::clamp(int(std::lround(g)), 0, 255));
        img[2] = uint8_t(std::clamp(int(std::lround(b)), 0, 255));
    }
}

} // namespace bc
