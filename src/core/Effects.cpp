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
    {ClipEffect::Kind::ChromaKey, "chroma_key"},
    {ClipEffect::Kind::Mask, "mask"},
    {ClipEffect::Kind::Equalizer, "equalizer"},
    {ClipEffect::Kind::Compressor, "compressor"},
    {ClipEffect::Kind::NoiseReduction, "noise_reduction"},
    {ClipEffect::Kind::HumRemoval, "hum_removal"},
    {ClipEffect::Kind::Plugin, "vst3"},
};

} // namespace

QList<ClipEffect::Kind> ClipEffect::kinds(bool audio)
{
    QList<Kind> out;
    for (const auto &k : kNames)
        if (isAudio(k.kind) == audio && k.kind != Kind::Plugin) // plug-ins are listed by name, not as a kind
            out << k.kind;
    return out;
}

ClipEffect ClipEffect::makePlugin(const QString &path, const QString &id, const QString &name,
                                  const QMap<QString, double> &values)
{
    ClipEffect e;
    e.kind = Kind::Plugin;
    e.pluginPath = path;
    e.pluginId = id;
    e.pluginName = name;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        e.setValue(it.key(), it.value());
    return e;
}

bool ClipEffect::pluginParamId(const QString &key, quint32 *id)
{
    if (key.size() < 2 || key[0] != QLatin1Char('p'))
        return false;
    bool ok = false;
    const quint32 v = key.mid(1).toUInt(&ok);
    if (ok && id)
        *id = v;
    return ok;
}

bool ClipEffect::isAudio(Kind kind)
{
    return kind == Kind::Equalizer || kind == Kind::Compressor || kind == Kind::NoiseReduction
           || kind == Kind::HumRemoval || kind == Kind::Plugin;
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
    case Kind::ChromaKey:
        // Hue of the key colour (120 = green, 240 = blue), how far around it
        // pixels go fully (tolerance) and partly (softness) transparent, the
        // least colourfulness a pixel needs to be keyed, and spill removal.
        return {{QStringLiteral("hue"), 0, 360, 120, 1, 0},
                {QStringLiteral("tolerance"), 0, 180, 40, 1, 0},
                {QStringLiteral("softness"), 0, 180, 20, 1, 0},
                {QStringLiteral("minimum"), 0, 100, 15, 1, 0},
                {QStringLiteral("spill"), 0, 100, 50, 5, 0}};
    case Kind::Mask:
        // Centre and size in % of the picture, soft edge in px of a 1080-line frame.
        return {{QStringLiteral("ellipse"), 0, 1, 1, 1, 0},
                {QStringLiteral("x"), 0, 100, 50, 1, 0},
                {QStringLiteral("y"), 0, 100, 50, 1, 0},
                {QStringLiteral("width"), 0, 200, 60, 1, 0},
                {QStringLiteral("height"), 0, 200, 60, 1, 0},
                {QStringLiteral("feather"), 0, 200, 10, 1, 0},
                {QStringLiteral("invert"), 0, 1, 0, 1, 0}};
    case Kind::Equalizer:
        // Low cut (high-pass, 0 = off), bass shelf, middle bell, treble shelf.
        return {{QStringLiteral("low_cut"), 0, 500, 0, 10, 0},
                {QStringLiteral("low_gain"), -15, 15, 0, 0.5, 1},
                {QStringLiteral("low_freq"), 40, 500, 120, 10, 0},
                {QStringLiteral("mid_gain"), -15, 15, 0, 0.5, 1},
                {QStringLiteral("mid_freq"), 200, 8000, 1000, 50, 0},
                {QStringLiteral("mid_q"), 0.3, 5, 1, 0.1, 1},
                {QStringLiteral("high_gain"), -15, 15, 0, 0.5, 1},
                {QStringLiteral("high_freq"), 2000, 16000, 8000, 250, 0}};
    case Kind::Compressor:
        // Above the threshold the level grows `ratio` times slower; make-up gain afterwards.
        return {{QStringLiteral("threshold"), -60, 0, -18, 1, 0},
                {QStringLiteral("ratio"), 1, 20, 4, 0.5, 1},
                {QStringLiteral("attack"), 0.1, 200, 10, 1, 1},
                {QStringLiteral("release"), 10, 2000, 150, 10, 0},
                {QStringLiteral("makeup"), 0, 24, 0, 0.5, 1}};
    case Kind::NoiseReduction:
        // How far steady noise is turned down (dB) and how eagerly sound is
        // taken for noise (higher = more is removed, voices may sound thinner).
        return {{QStringLiteral("reduction"), 0, 40, 12, 1, 0},
                {QStringLiteral("sensitivity"), 0, 100, 50, 5, 0}};
    case Kind::HumRemoval:
        // Mains hum: 50 Hz in Uzbekistan and Europe, 60 Hz in America; notches
        // at the base and its harmonics.
        return {{QStringLiteral("hum_freq"), 40, 70, 50, 1, 0},
                {QStringLiteral("harmonics"), 1, 10, 4, 1, 0},
                {QStringLiteral("notch_width"), 0.5, 10, 2, 0.5, 1}};
    case Kind::Plugin:
        return {}; // the plug-in describes its own parameters ("p<id>", 0..1)
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
    if (kind == Kind::Plugin)
        return pluginParamId(key, nullptr) ? std::clamp(params.value(key, 0.0), 0.0, 1.0) : 0.0;
    for (const auto &s : specs(kind))
        if (s.key == key)
            return std::clamp(params.value(key, s.def), s.min, s.max);
    return 0;
}

bool ClipEffect::setValue(const QString &key, double v)
{
    if (kind == Kind::Plugin) {
        if (!pluginParamId(key, nullptr) || !std::isfinite(v))
            return false;
        v = std::clamp(v, 0.0, 1.0);
        if (params.contains(key) && params.value(key) == v)
            return false;
        params.insert(key, v);
        return true;
    }
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
    case Kind::ChromaKey:
        return value(QStringLiteral("tolerance")) == 0 && value(QStringLiteral("softness")) == 0;
    case Kind::Mask:
        return false; // even a full-frame mask hides the letterbox bars
    case Kind::Equalizer:
        return value(QStringLiteral("low_cut")) <= 0 && value(QStringLiteral("low_gain")) == 0
               && value(QStringLiteral("mid_gain")) == 0 && value(QStringLiteral("high_gain")) == 0;
    case Kind::Compressor:
        return value(QStringLiteral("ratio")) <= 1 && value(QStringLiteral("makeup")) == 0;
    case Kind::NoiseReduction:
        return value(QStringLiteral("reduction")) <= 0;
    case Kind::HumRemoval:
        return false;
    case Kind::Plugin:
        return false; // a plug-in may change the sound at any setting
    }
    return true;
}

bool ClipEffect::operator==(const ClipEffect &o) const
{
    if (kind != o.kind || enabled != o.enabled)
        return false;
    if (kind == Kind::Plugin)
        return pluginPath == o.pluginPath && pluginId == o.pluginId && pluginName == o.pluginName
               && params == o.params;
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
        if (e.kind == ClipEffect::Kind::Plugin) {
            o[QStringLiteral("plugin_path")] = e.pluginPath;
            o[QStringLiteral("plugin_id")] = e.pluginId;
            o[QStringLiteral("plugin_name")] = e.pluginName;
            QJsonObject values;
            for (auto it = e.params.cbegin(); it != e.params.cend(); ++it)
                values[it.key()] = it.value();
            o[QStringLiteral("params")] = values;
        }
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
        if (kind == ClipEffect::Kind::Plugin) {
            e.pluginPath = o.value(QStringLiteral("plugin_path")).toString();
            e.pluginId = o.value(QStringLiteral("plugin_id")).toString();
            e.pluginName = o.value(QStringLiteral("plugin_name")).toString();
            const QJsonObject values = o.value(QStringLiteral("params")).toObject();
            for (auto it = values.begin(); it != values.end(); ++it)
                e.setValue(it.key(), it.value().toDouble());
        }
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

// Hue of an RGB colour in degrees (0 .. 360).
double hueOf(int r, int g, int b)
{
    const int mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const double c = mx - mn;
    if (c <= 0)
        return 0;
    double h;
    if (mx == r)
        h = std::fmod((g - b) / c, 6.0);
    else if (mx == g)
        h = (b - r) / c + 2;
    else
        h = (r - g) / c + 4;
    h *= 60;
    return h < 0 ? h + 360 : h;
}

void chromaKey(uint8_t *img, size_t n, double keyHue, double tolerance, double softness, double minimum, double spill)
{
    for (size_t i = 0; i < n; ++i, img += 4) {
        if (img[3] == 0)
            continue;
        const int r = img[0], g = img[1], b = img[2];
        const double chroma = (std::max({r, g, b}) - std::min({r, g, b})) / 255.0 * 100.0;
        // Grey, black and white pixels are never keyed; a 10% ramp avoids a hard edge.
        const double colourful = std::clamp((chroma - minimum) / 10.0, 0.0, 1.0);
        if (colourful <= 0)
            continue;
        double d = std::fabs(hueOf(r, g, b) - keyHue);
        if (d > 180)
            d = 360 - d;
        double keep; // 0 = the key colour (transparent), 1 = kept
        if (d <= tolerance)
            keep = 0;
        else if (d >= tolerance + softness)
            keep = 1;
        else
            keep = (d - tolerance) / softness;
        const double key = (1 - keep) * colourful;
        img[3] = clampByte(img[3] * (1 - key));
        // Spill: what stays near the key colour loses that tint (towards grey).
        const double near = 1 - std::clamp((d - tolerance - softness) / 30.0, 0.0, 1.0);
        const double s = spill / 100.0 * near * colourful;
        if (s > 0 && img[3] > 0) {
            const double l = 0.299 * r + 0.587 * g + 0.114 * b;
            img[0] = clampByte(r + (l - r) * s);
            img[1] = clampByte(g + (l - g) * s);
            img[2] = clampByte(b + (l - b) * s);
        }
    }
}

void mask(uint8_t *img, int w, int h, const QRectF &content, bool ellipse, double x, double y, double width,
          double height, double feather, bool invert)
{
    const double cx = content.left() + content.width() * x / 100.0;
    const double cy = content.top() + content.height() * y / 100.0;
    const double a = content.width() * width / 200.0;  // half sizes
    const double b = content.height() * height / 200.0;
    for (int py = 0; py < h; ++py) {
        uint8_t *row = img + size_t(py) * w * 4;
        const double dy = py + 0.5 - cy;
        for (int px = 0; px < w; ++px) {
            const double dx = px + 0.5 - cx;
            double inside; // distance from the edge in pixels, > 0 inside
            if (a <= 0 || b <= 0)
                inside = -1;
            else if (ellipse)
                inside = (1 - std::sqrt((dx / a) * (dx / a) + (dy / b) * (dy / b))) * std::min(a, b);
            else
                inside = std::min(a - std::fabs(dx), b - std::fabs(dy));
            double m = feather > 0 ? std::clamp(inside / feather, 0.0, 1.0) : (inside >= 0 ? 1.0 : 0.0);
            if (invert)
                m = 1 - m;
            if (m < 1)
                row[px * 4 + 3] = uint8_t(std::lround(row[px * 4 + 3] * m));
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
        case ClipEffect::Kind::ChromaKey:
            chromaKey(rgba, n, e.value(QStringLiteral("hue")), e.value(QStringLiteral("tolerance")),
                      e.value(QStringLiteral("softness")), e.value(QStringLiteral("minimum")), e.value(QStringLiteral("spill")));
            break;
        case ClipEffect::Kind::Mask:
            mask(rgba, w, h, content, e.value(QStringLiteral("ellipse")) >= 0.5, e.value(QStringLiteral("x")),
                 e.value(QStringLiteral("y")), e.value(QStringLiteral("width")), e.value(QStringLiteral("height")),
                 e.value(QStringLiteral("feather")) * scale, e.value(QStringLiteral("invert")) >= 0.5);
            break;
        case ClipEffect::Kind::Equalizer:
        case ClipEffect::Kind::Compressor:
        case ClipEffect::Kind::NoiseReduction:
        case ClipEffect::Kind::HumRemoval:
        case ClipEffect::Kind::Plugin:
            break; // sound: core/AudioFx
        }
    }
}

} // namespace bc
