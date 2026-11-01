#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QRectF>
#include <QString>

#include <cstdint>

namespace bc {

// One parameter of an effect: its key, range, default and display step.
struct EffectParamSpec
{
    QString key;
    double min = 0;
    double max = 100;
    double def = 0;
    double step = 1;
    int decimals = 0;
};

// One picture effect of a video clip (phase 8b). A clip has a list of them,
// applied in order before its motion. Values are not animated.
struct ClipEffect
{
    enum class Kind { BrightnessContrast, Saturation, Blur, Sharpen, Crop, Mirror };

    Kind kind = Kind::BrightnessContrast;
    bool enabled = true;
    QMap<QString, double> params; // missing keys use the default

    static ClipEffect make(Kind kind); // with default values
    static QList<Kind> kinds();
    static QList<EffectParamSpec> specs(Kind kind);
    static QString kindToString(Kind k);
    static bool kindFromString(const QString &s, Kind *k);

    double value(const QString &key) const;
    // Sets a known parameter (limited to its range); false when unknown or unchanged.
    bool setValue(const QString &key, double v);
    // True when it changes nothing (disabled, or all values neutral).
    bool isNeutral() const;

    bool operator==(const ClipEffect &o) const;
    bool operator!=(const ClipEffect &o) const { return !(*this == o); }
};

using ClipEffects = QList<ClipEffect>;

QJsonArray effectsToJson(const ClipEffects &effects);
ClipEffects effectsFromJson(const QJsonArray &a);
// Compact text for the engine filter: only enabled effects that change something.
QByteArray effectsToEngineString(const ClipEffects &effects);
ClipEffects effectsFromEngineString(const QByteArray &s);
bool effectsActive(const ClipEffects &effects);

// Applies the effects to a straight-alpha RGBA8888 image. `content` is the
// picture inside letterbox bars (crop and mirror use it); sizes such as the
// blur radius are given for a 1080-line frame and scale with `height`.
void applyEffects(uint8_t *rgba, int width, int height, const ClipEffects &effects, const QRectF &content);

} // namespace bc
