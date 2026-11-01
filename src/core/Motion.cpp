#include "core/Motion.h"

#include <QJsonArray>
#include <QList>

#include <algorithm>
#include <cmath>

namespace bc {

ClipTransform ClipTransform::clamped() const
{
    ClipTransform t = *this;
    t.x = std::clamp(x, -10.0, 10.0);
    t.y = std::clamp(y, -10.0, 10.0);
    t.scale = std::clamp(scale, 0.0, 1000.0);
    t.rotation = std::clamp(rotation, -3600.0, 3600.0);
    t.opacity = std::clamp(opacity, 0.0, 100.0);
    return t;
}

ClipTransform ClipTransform::lerp(const ClipTransform &a, const ClipTransform &b, double t)
{
    auto mix = [t](double p, double q) { return p + (q - p) * t; };
    ClipTransform r;
    r.x = mix(a.x, b.x);
    r.y = mix(a.y, b.y);
    r.scale = mix(a.scale, b.scale);
    r.rotation = mix(a.rotation, b.rotation);
    r.opacity = mix(a.opacity, b.opacity);
    return r;
}

bool ClipMotion::isIdentity() const
{
    if (keys.isEmpty())
        return base.isIdentity();
    return std::all_of(keys.begin(), keys.end(), [](const TransformKey &k) { return k.value.isIdentity(); });
}

ClipTransform ClipMotion::at(qint64 f) const
{
    if (keys.isEmpty())
        return base;
    if (f <= keys.first().frame)
        return keys.first().value;
    if (f >= keys.last().frame)
        return keys.last().value;
    for (int i = 1; i < keys.size(); ++i) {
        const TransformKey &b = keys[i];
        if (f <= b.frame) {
            const TransformKey &a = keys[i - 1];
            return ClipTransform::lerp(a.value, b.value, double(f - a.frame) / double(b.frame - a.frame));
        }
    }
    return keys.last().value;
}

bool ClipMotion::hasKey(qint64 f) const
{
    return std::any_of(keys.begin(), keys.end(), [f](const TransformKey &k) { return k.frame == f; });
}

void ClipMotion::set(qint64 f, const ClipTransform &value)
{
    const ClipTransform v = value.clamped();
    if (keys.isEmpty()) {
        base = v;
        return;
    }
    auto it = std::lower_bound(keys.begin(), keys.end(), f,
                               [](const TransformKey &k, qint64 fr) { return k.frame < fr; });
    if (it != keys.end() && it->frame == f)
        it->value = v;
    else
        keys.insert(it, TransformKey{f, v});
}

bool ClipMotion::removeKey(qint64 f)
{
    for (int i = 0; i < keys.size(); ++i) {
        if (keys[i].frame == f) {
            if (keys.size() == 1)
                base = keys[i].value; // the last key keeps the picture where it is
            keys.removeAt(i);
            return true;
        }
    }
    return false;
}

void ClipMotion::setAnimated(bool on, qint64 f)
{
    if (on == animated())
        return;
    if (on) {
        keys = {TransformKey{f, base}};
    } else {
        base = at(f);
        keys.clear();
    }
}

qint64 ClipMotion::nextKey(qint64 f) const
{
    for (const auto &k : keys)
        if (k.frame > f)
            return k.frame;
    return -1;
}

qint64 ClipMotion::prevKey(qint64 f) const
{
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        if (it->frame < f)
            return it->frame;
    return -1;
}

void ClipMotion::rescale(double factor)
{
    QList<TransformKey> out;
    for (TransformKey k : keys) {
        k.frame = qint64(std::llround(k.frame * factor));
        if (!out.isEmpty() && out.last().frame == k.frame)
            out.last() = k;
        else
            out << k;
    }
    keys = out;
}

namespace {

QJsonObject transformJson(const ClipTransform &t)
{
    QJsonObject o;
    o[QStringLiteral("x")] = t.x;
    o[QStringLiteral("y")] = t.y;
    o[QStringLiteral("scale")] = t.scale;
    o[QStringLiteral("rotation")] = t.rotation;
    o[QStringLiteral("opacity")] = t.opacity;
    return o;
}

ClipTransform transformFromJson(const QJsonObject &o)
{
    ClipTransform t;
    t.x = o.value(QStringLiteral("x")).toDouble(0);
    t.y = o.value(QStringLiteral("y")).toDouble(0);
    t.scale = o.value(QStringLiteral("scale")).toDouble(100);
    t.rotation = o.value(QStringLiteral("rotation")).toDouble(0);
    t.opacity = o.value(QStringLiteral("opacity")).toDouble(100);
    return t.clamped();
}

} // namespace

QJsonObject ClipMotion::toJson() const
{
    QJsonObject o;
    if (!base.isIdentity())
        o[QStringLiteral("value")] = transformJson(base);
    if (!keys.isEmpty()) {
        QJsonArray a;
        for (const auto &k : keys) {
            QJsonObject ko = transformJson(k.value);
            ko[QStringLiteral("frame")] = double(k.frame);
            a.append(ko);
        }
        o[QStringLiteral("keys")] = a;
    }
    return o;
}

ClipMotion ClipMotion::fromJson(const QJsonObject &o)
{
    ClipMotion m;
    m.base = transformFromJson(o.value(QStringLiteral("value")).toObject());
    for (const auto &v : o.value(QStringLiteral("keys")).toArray()) {
        const QJsonObject ko = v.toObject();
        const qint64 f = std::max<qint64>(0, qint64(ko.value(QStringLiteral("frame")).toDouble()));
        if (m.keys.isEmpty())
            m.keys = {TransformKey{f, transformFromJson(ko)}};
        else
            m.set(f, transformFromJson(ko));
    }
    return m;
}

QByteArray ClipMotion::toEngineString(qint64 offset) const
{
    auto one = [](qint64 f, const ClipTransform &t) {
        return QByteArray::number(f) + ':' + QByteArray::number(t.x, 'g', 10) + ':' + QByteArray::number(t.y, 'g', 10)
               + ':' + QByteArray::number(t.scale, 'g', 10) + ':' + QByteArray::number(t.rotation, 'g', 10) + ':'
               + QByteArray::number(t.opacity, 'g', 10);
    };
    if (keys.isEmpty())
        return "b" + one(0, base);
    QByteArrayList parts;
    for (const auto &k : keys)
        parts << one(k.frame - offset, k.value);
    return parts.join(';');
}

ClipMotion ClipMotion::fromEngineString(const QByteArray &s)
{
    ClipMotion m;
    const bool isBase = s.startsWith('b');
    for (const QByteArray &part : (isBase ? s.mid(1) : s).split(';')) {
        const QByteArrayList f = part.split(':');
        if (f.size() != 6)
            continue;
        ClipTransform t;
        t.x = f[1].toDouble();
        t.y = f[2].toDouble();
        t.scale = f[3].toDouble();
        t.rotation = f[4].toDouble();
        t.opacity = f[5].toDouble();
        if (isBase) {
            m.base = t;
            return m;
        }
        m.keys << TransformKey{f[0].toLongLong(), t};
    }
    return m;
}

} // namespace bc
