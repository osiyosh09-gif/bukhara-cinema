#pragma once

#include "core/Effects.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace bc {

// Basic colour correction of a video clip (phase 9a), applied before its
// effects: white balance, exposure, contrast, highlights and shadows, the
// colour wheels (lift/gamma/gain, phase 9d), then saturation and vibrance.
// Values are not animated. Then an optional creative LUT (.cube file, phase 9b) mixed in by `lut_amount` percent.
struct ClipColor
{
    QMap<QString, double> values; // missing keys use the default (neutral)
    QString lut;                  // absolute path of a .cube file, or empty

    static QList<EffectParamSpec> specs();
    // Colour wheels (phase 9d): "lift", "gamma", "gain"; each has the
    // values <wheel>_x, <wheel>_y (point on the wheel) and <wheel>_l (master).
    static QStringList wheels();
    static bool isWheelKey(const QString &key);
    double value(const QString &key) const;
    // Sets a known value (limited to its range); false when unknown or unchanged.
    bool setValue(const QString &key, double v);
    bool isNeutral() const;
    bool hasLut() const { return !lut.isEmpty() && value(QStringLiteral("lut_amount")) > 0; }

    bool operator==(const ClipColor &o) const;
    bool operator!=(const ClipColor &o) const { return !(*this == o); }

    QJsonObject toJson() const; // only values that differ from the default
    static ClipColor fromJson(const QJsonObject &o);
    QByteArray toEngineString() const; // empty when neutral
    static ClipColor fromEngineString(const QByteArray &s);
};

// The RGB change (each -1 .. 1, luma kept) of a point on a colour wheel
// (x, y in -100 .. 100; x = towards blue, y = towards red as on a vectorscope).
void wheelBalance(double x, double y, double rgb[3]);

// Applies the correction to a straight-alpha RGBA8888 image of n pixels.
void applyColor(uint8_t *rgba, size_t n, const ClipColor &color);

} // namespace bc
