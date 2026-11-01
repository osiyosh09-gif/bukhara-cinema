#include "core/Effects.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace bc {

namespace {

struct KindName
{
    ClipEffect::Kind kind;
    const char *name;
};

constexpr KindName kNames[] = {
    {ClipEffect::Kind::BrightnessContrast, "brightness_contrast"},
    {ClipEffect::Kind::Saturation, "saturation"},
    {ClipEffect::Kind::Blur, "blur"},
    {ClipEffect::Kind::Sharpen, "sharpen"},
    {ClipEffect::Kind::Crop, "crop"},
    {ClipEffect::Kind::Mirror, "mirror"},
};

} // namespace

QList<ClipEffect::Kind> ClipEffect::kinds()
{
    QList<Kind> out;
    for (const auto &k : kNames)
        out << k.kind;
    return out;
}

QString ClipEffect::kindToString(Kind k)
{
    for (const auto &n : kNames)
        if (n.kind == k)
            return QString::fromLatin1(n.name);
    return {};
}

bool ClipEffect::kindFromString(const QString &s, Kind *k)
{
    for (const auto &n : kNames)
        if (s == QLatin1String(n.name)) {
            *k = n.kind;
            return true;
        }
    return false;
}

QList<EffectParamSpec> ClipEffect::specs(Kind kind)
{
    switch (kind) {
    case Kind::BrightnessContrast:
        return {{QStringLiteral("brightness"), -100, 100, 0, 1, 0}, {QStringLiteral("contrast"), -100, 100, 0, 1, 0}};
    case Kind::Saturation:
        return {{QStringLiteral("saturation"), 0, 300, 100, 5, 0}};
    case Kind::Blur:
        return {{QStringLiteral("radius"), 0, 100, 5, 0.5, 1}};
    case Kind::Sharpen:
        return {{QStringLiteral("amount"), 0, 300, 50, 5, 0}};
    case Kind::Crop:
        return {{QStringLiteral("left"), 0, 100, 0, 1, 0},
                {QStringLiteral("right"), 0, 100, 0, 1, 0},
                {QStringLiteral("top"), 0, 100, 0, 1, 0},
                {QStringLiteral("bottom"), 0, 100, 0, 1, 0}};
    case Kind::Mirror:
        // 0 or 1 (switches)
        return {{QStringLiteral("horizontal"), 0, 1, 1, 1, 0}, {QStringLiteral("vertical"), 0, 1, 0, 1, 0}};
    }
    return {};
}

ClipEffect ClipEffect::make(Kind kind)
{
    ClipEffect e;
    e.kind = kind;
    for (const auto &s : specs(kind))
        e.params.insert(s.key, s.def);
    return e;
}

double ClipEffect::value(const QString &key) const
{
    for (const auto &s : specs(kind))
        if (s.key == key)
            return std::clamp(params.value(key, s.def), s.min, s.max);
    return 0;
}

bool ClipEffect::setValue(const QString &key, double v)
{
    for (const auto &s : specs(kind)) {
        if (s.key != key)
            continue;
        v = std::clamp(v, s.min, s.max);
        if (s.max == 1 && s.step == 1)
            v = v >= 0.5 ? 1 : 0;
        if (params.contains(key) && params.value(key) == v)
            return false;
        params.insert(key, v);
        return true;
    }
    return false;
}

bool ClipEffect::isNeutral() const
{
    if (!enabled)
        return true;
    switch (kind) {
    case Kind::BrightnessContrast:
        return value(QStringLiteral("brightness")) == 0 && value(QStringLiteral("contrast")) == 0;
    case Kind::Saturation:
        return value(QStringLiteral("saturation")) == 100;
    case Kind::Blur:
        return value(QStringLiteral("radius")) <= 0;
    case Kind::Sharpen:
        return value(QStringLiteral("amount")) <= 0;
    case Kind::Crop:
        return value(QStringLiteral("left")) == 0 && value(QStringLiteral("right")) == 0
               && value(QStringLiteral("top")) == 0 && value(QStringLiteral("bottom")) == 0;
    case Kind::Mirror:
        return value(QStringLiteral("horizontal")) == 0 && value(QStringLiteral("vertical")) == 0;
    }
    return true;
}

bool ClipEffect::operator==(const ClipEffect &o) const
{
    if (kind != o.kind || enabled != o.enabled)
        return false;
    for (const auto &s : specs(kind))
        if (value(s.key) != o.value(s.key))
            return false;
    return true;
}

QJsonArray effectsToJson(const ClipEffects &effects)
{
    QJsonArray a;
    for (const auto &e : effects) {
        QJsonObject o;
        o[QStringLiteral("kind")] = ClipEffect::kindToString(e.kind);
        if (!e.enabled)
            o[QStringLiteral("enabled")] = false;
        for (const auto &s : ClipEffect::specs(e.kind))
            o[s.key] = e.value(s.key);
        a.append(o);
    }
    return a;
}

ClipEffects effectsFromJson(const QJsonArray &a)
{
    ClipEffects out;
    for (const auto &v : a) {
        const QJsonObject o = v.toObject();
        ClipEffect::Kind kind;
        if (!ClipEffect::kindFromString(o.value(QStringLiteral("kind")).toString(), &kind))
            continue; // unknown (newer) effect: skipped
        ClipEffect e = ClipEffect::make(kind);
        e.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        for (const auto &s : ClipEffect::specs(kind))
            if (o.contains(s.key))
                e.setValue(s.key, o.value(s.key).toDouble(s.def));
        out << e;
    }
    return out;
}

bool effectsActive(const ClipEffects &effects)
{
    return std::any_of(effects.begin(), effects.end(), [](const ClipEffect &e) { return !e.isNeutral(); });
}

QByteArray effectsToEngineString(const ClipEffects &effects)
{
    ClipEffects active;
    for (const auto &e : effects)
        if (!e.isNeutral())
            active << e;
    if (active.isEmpty())
        return {};
    return QJsonDocument(effectsToJson(active)).toJson(QJsonDocument::Compact);
}

ClipEffects effectsFromEngineString(const QByteArray &s)
{
    if (s.isEmpty())
        return {};
    return effectsFromJson(QJsonDocument::fromJson(s).array());
}

// ---------------------------------------------------------------------------
// Pixel work. Images are straight-alpha RGBA8888.

namespace {

inline uint8_t clampByte(double v)
{
    return uint8_t(std::clamp(int(std::lround(v)), 0, 255));
}

void premultiply(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i, p += 4) {
        const int a = p[3];
        if (a == 255)
            continue;
        p[0] = uint8_t((p[0] * a + 127) / 255);
        p[1] = uint8_t((p[1] * a + 127) / 255);
        p[2] = uint8_t((p[2] * a + 127) / 255);
    }
}

void unpremultiply(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i, p += 4) {
        const int a = p[3];
        if (a == 255)
            continue;
        if (a == 0) {
            p[0] = p[1] = p[2] = 0;
            continue;
        }
        p[0] = uint8_t(std::min(255, (p[0] * 255 + a / 2) / a));
        p[1] = uint8_t(std::min(255, (p[1] * 255 + a / 2) / a));
        p[2] = uint8_t(std::min(255, (p[2] * 255 + a / 2) / a));
    }
}

// One box pass of radius r along rows (stride 4 per pixel, `step` pixels
// between samples, `count` samples per line, `lines` lines `lineStep` apart).
void boxPass(const uint8_t *src, uint8_t *dst, int count, int lines, size_t step, size_t lineStep, int r)
{
    const int win = 2 * r + 1;
    for (int l = 0; l < lines; ++l) {
        const uint8_t *s = src + size_t(l) * lineStep;
        uint8_t *d = dst + size_t(l) * lineStep;
        auto at = [&](int i) { return s + size_t(std::clamp(i, 0, count - 1)) * step; };
        std::array<int, 4> sum{0, 0, 0, 0};
        for (int i = -r; i <= r; ++i)
            for (int c = 0; c < 4; ++c)
                sum[c] += at(i)[c];
        for (int i = 0; i < count; ++i) {
            uint8_t *o = d + size_t(i) * step;
            for (int c = 0; c < 4; ++c)
                o[c] = uint8_t((sum[c] + win / 2) / win);
            const uint8_t *add = at(i + r + 1);
            const uint8_t *sub = at(i - r);
            for (int c = 0; c < 4; ++c)
                sum[c] += add[c] - sub[c];
        }
    }
}

// Close to a Gaussian blur: three box passes each way, on premultiplied pixels.
void boxBlur(uint8_t *img, int w, int h, int r)
{
    if (r <= 0 || w <= 0 || h <= 0)
        return;
    const size_t n = size_t(w) * h;
    std::vector<uint8_t> tmp(n * 4);
    for (int pass = 0; pass < 3; ++pass) {
        boxPass(img, tmp.data(), w, h, 4, size_t(w) * 4, std::min(r, w));
        boxPass(tmp.data(), img, h, w, size_t(w) * 4, 4, std::min(r, h));
    }
}

void blurStraight(uint8_t *img, int w, int h, int r)
{
    const size_t n = size_t(w) * h;
    premultiply(img, n);
    boxBlur(img, w, h, r);
    unpremultiply(img, n);
}

void brightnessContrast(uint8_t *img, size_t n, double brightness, double contrast)
{
    const double factor = std::pow(2.0, contrast / 50.0); // -100 = 1/4, +100 = 4 times
    const double offset = brightness * 1.28;
    uint8_t lut[256];
    for (int v = 0; v < 256; ++v)
        lut[v] = clampByte((v - 128) * factor + 128 + offset);
    for (size_t i = 0; i < n; ++i, img += 4) {
        img[0] = lut[img[0]];
        img[1] = lut[img[1]];
        img[2] = lut[img[2]];
    }
}

void saturation(uint8_t *img, size_t n, double s)
{
    for (size_t i = 0; i < n; ++i, img += 4) {
        const double l = 0.299 * img[0] + 0.587 * img[1] + 0.114 * img[2];
        img[0] = clampByte(l + (img[0] - l) * s);
        img[1] = clampByte(l + (img[1] - l) * s);
        img[2] = clampByte(l + (img[2] - l) * s);
    }
}

void sharpen(uint8_t *img, int w, int h, double amount, int r)
{
    const size_t n = size_t(w) * h;
    std::vector<uint8_t> blurred(img, img + n * 4);
    blurStraight(blurred.data(), w, h, r);
    for (size_t i = 0; i < n; ++i) {
        uint8_t *p = img + i * 4;
        const uint8_t *b = blurred.data() + i * 4;
        for (int c = 0; c < 3; ++c)
            p[c] = clampByte(p[c] + (p[c] - b[c]) * amount);
    }
}

void crop(uint8_t *img, int w, int h, const QRectF &content, double l, double r, double t, double b)
{
    const double x0 = content.left() + content.width() * l / 100.0;
    const double x1 = content.right() - content.width() * r / 100.0;
    const double y0 = content.top() + content.height() * t / 100.0;
    const double y1 = content.bottom() - content.height() * b / 100.0;
    for (int y = 0; y < h; ++y) {
        const bool rowOut = y + 0.5 < y0 || y + 0.5 > y1;
        uint8_t *row = img + size_t(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const bool inContent = x + 0.5 >= content.left() && x + 0.5 <= content.right()
                                   && y + 0.5 >= content.top() && y + 0.5 <= content.bottom();
            if (!inContent)
                continue; // bars stay as they are
            if (rowOut || x + 0.5 < x0 || x + 0.5 > x1)
                std::memset(row + x * 4, 0, 4);
        }
    }
}

void mirror(uint8_t *img, int w, int h, bool horizontal, bool vertical)
{
    if (horizontal)
        for (int y = 0; y < h; ++y) {
            auto *row = reinterpret_cast<uint32_t *>(img + size_t(y) * w * 4);
            std::reverse(row, row + w);
        }
    if (vertical) {
        std::vector<uint8_t> line(size_t(w) * 4);
        for (int y = 0; y < h / 2; ++y) {
            uint8_t *a = img + size_t(y) * w * 4;
            uint8_t *b = img + size_t(h - 1 - y) * w * 4;
            std::memcpy(line.data(), a, line.size());
            std::memcpy(a, b, line.size());
            std::memcpy(b, line.data(), line.size());
        }
    }
}

} // namespace

void applyEffects(uint8_t *rgba, int w, int h, const ClipEffects &effects, const QRectF &content0)
{
    if (!rgba || w <= 0 || h <= 0)
        return;
    const QRectF content = content0.isValid() ? content0 : QRectF(0, 0, w, h);
    const size_t n = size_t(w) * h;
    const double scale = h / 1080.0;
    for (const ClipEffect &e : effects) {
        if (e.isNeutral())
            continue;
        switch (e.kind) {
        case ClipEffect::Kind::BrightnessContrast:
            brightnessContrast(rgba, n, e.value(QStringLiteral("brightness")), e.value(QStringLiteral("contrast")));
            break;
        case ClipEffect::Kind::Saturation:
            saturation(rgba, n, e.value(QStringLiteral("saturation")) / 100.0);
            break;
        case ClipEffect::Kind::Blur: {
            // Three box passes of radius r spread about as far as `radius`.
            const int r = std::max(1, int(std::lround(e.value(QStringLiteral("radius")) * scale * 0.6)));
            blurStraight(rgba, w, h, r);
            break;
        }
        case ClipEffect::Kind::Sharpen:
            sharpen(rgba, w, h, e.value(QStringLiteral("amount")) / 100.0, std::max(1, int(std::lround(1.5 * scale))));
            break;
        case ClipEffect::Kind::Crop:
            crop(rgba, w, h, content, e.value(QStringLiteral("left")), e.value(QStringLiteral("right")),
                 e.value(QStringLiteral("top")), e.value(QStringLiteral("bottom")));
            break;
        case ClipEffect::Kind::Mirror:
            // Letterbox bars are symmetric, so flipping the whole frame flips the picture in place.
            mirror(rgba, w, h, e.value(QStringLiteral("horizontal")) >= 0.5, e.value(QStringLiteral("vertical")) >= 0.5);
            break;
        }
    }
}

} // namespace bc
