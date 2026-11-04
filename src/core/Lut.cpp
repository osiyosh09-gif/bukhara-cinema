#include "core/Lut.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace bc {

std::shared_ptr<const Lut> Lut::parse(const QByteArray &text, QString *error)
{
    auto fail = [&](const QString &e) -> std::shared_ptr<const Lut> {
        if (error)
            *error = e;
        return nullptr;
    };
    auto lut = std::make_shared<Lut>();
    qsizetype expected = -1;
    int lineNo = 0;
    for (const QByteArray &raw : text.split('\n')) {
        ++lineNo;
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith("TITLE")) {
            lut->m_title = QString::fromUtf8(line.mid(5).trimmed()).remove(QLatin1Char('"'));
            continue;
        }
        const QList<QByteArray> parts = line.simplified().split(' ');
        const QByteArray &key = parts.first();
        if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
            const bool is3D = key == "LUT_3D_SIZE";
            const int n = parts.size() > 1 ? parts[1].toInt() : 0;
            if (n < 2 || (is3D && n > 129) || n > 65536)
                return fail(QStringLiteral("line %1: bad size %2").arg(lineNo).arg(n));
            lut->m_is3D = is3D;
            lut->m_size = n;
            expected = is3D ? qsizetype(n) * n * n : n;
            lut->m_data.reserve(expected * 3);
            continue;
        }
        if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
            if (parts.size() < 4)
                return fail(QStringLiteral("line %1: bad domain").arg(lineNo));
            float *d = key == "DOMAIN_MIN" ? lut->m_min : lut->m_max;
            for (int i = 0; i < 3; ++i)
                d[i] = parts[i + 1].toFloat();
            continue;
        }
        bool ok = false;
        key.toFloat(&ok);
        if (!ok)
            continue; // other keywords (LUT_3D_INPUT_RANGE, ...) are ignored
        if (expected < 0)
            return fail(QStringLiteral("line %1: values before the size").arg(lineNo));
        if (parts.size() < 3)
            return fail(QStringLiteral("line %1: needs three values").arg(lineNo));
        for (int i = 0; i < 3; ++i) {
            const float v = parts[i].toFloat(&ok);
            if (!ok)
                return fail(QStringLiteral("line %1: not a number").arg(lineNo));
            lut->m_data.append(v);
        }
    }
    if (expected < 0)
        return fail(QStringLiteral("no LUT_3D_SIZE or LUT_1D_SIZE"));
    if (lut->m_data.size() != expected * 3)
        return fail(QStringLiteral("%1 values, expected %2").arg(lut->m_data.size() / 3).arg(expected));
    for (int i = 0; i < 3; ++i)
        if (!(lut->m_max[i] > lut->m_min[i]))
            return fail(QStringLiteral("bad domain"));
    return lut;
}

std::shared_ptr<const Lut> Lut::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1").arg(path);
        return nullptr;
    }
    if (f.size() > 64 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("file too large");
        return nullptr;
    }
    return parse(f.readAll(), error);
}

std::shared_ptr<const Lut> Lut::cached(const QString &path)
{
    static QMutex mutex;
    struct Entry
    {
        qint64 size;
        QDateTime modified;
        std::shared_ptr<const Lut> lut;
    };
    static QHash<QString, Entry> cache;
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists())
        return nullptr;
    QMutexLocker lock(&mutex);
    auto it = cache.find(path);
    if (it != cache.end() && it->size == fi.size() && it->modified == fi.lastModified())
        return it->lut;
    Entry e{fi.size(), fi.lastModified(), load(path)};
    if (cache.size() > 32)
        cache.clear();
    cache.insert(path, e);
    return e.lut;
}

void Lut::apply(float &r, float &g, float &b) const
{
    float in[3] = {r, g, b};
    const int n = m_size;
    for (int i = 0; i < 3; ++i)
        in[i] = std::clamp((in[i] - m_min[i]) / (m_max[i] - m_min[i]), 0.0f, 1.0f) * float(n - 1);
    if (!m_is3D) {
        float out[3];
        for (int c = 0; c < 3; ++c) {
            const int i0 = std::min(int(in[c]), n - 2);
            const float t = in[c] - float(i0);
            out[c] = m_data[i0 * 3 + c] * (1 - t) + m_data[(i0 + 1) * 3 + c] * t;
        }
        r = out[0];
        g = out[1];
        b = out[2];
        return;
    }
    const int x0 = std::min(int(in[0]), n - 2), y0 = std::min(int(in[1]), n - 2), z0 = std::min(int(in[2]), n - 2);
    const float tx = in[0] - float(x0), ty = in[1] - float(y0), tz = in[2] - float(z0);
    auto at = [&](int x, int y, int z) { return m_data.constData() + (size_t(z) * n * n + size_t(y) * n + x) * 3; };
    float out[3];
    for (int c = 0; c < 3; ++c) {
        const float c00 = at(x0, y0, z0)[c] * (1 - tx) + at(x0 + 1, y0, z0)[c] * tx;
        const float c10 = at(x0, y0 + 1, z0)[c] * (1 - tx) + at(x0 + 1, y0 + 1, z0)[c] * tx;
        const float c01 = at(x0, y0, z0 + 1)[c] * (1 - tx) + at(x0 + 1, y0, z0 + 1)[c] * tx;
        const float c11 = at(x0, y0 + 1, z0 + 1)[c] * (1 - tx) + at(x0 + 1, y0 + 1, z0 + 1)[c] * tx;
        const float c0 = c00 * (1 - ty) + c10 * ty;
        const float c1 = c01 * (1 - ty) + c11 * ty;
        out[c] = c0 * (1 - tz) + c1 * tz;
    }
    r = out[0];
    g = out[1];
    b = out[2];
}

} // namespace bc
