#pragma once

#include <QString>
#include <QVector>

#include <memory>

namespace bc {

// A colour look-up table read from an Adobe/Resolve .cube file (phase 9b):
// 3D (LUT_3D_SIZE, 2..129) or 1D (LUT_1D_SIZE, 2..65536). Values are applied
// with linear (1D) or trilinear (3D) interpolation.
class Lut
{
public:
    // Reads the file; on failure returns null and sets `error` (English, for the log).
    static std::shared_ptr<const Lut> load(const QString &path, QString *error = nullptr);
    // The same, cached by path, size and modification time (thread-safe).
    static std::shared_ptr<const Lut> cached(const QString &path);
    static std::shared_ptr<const Lut> parse(const QByteArray &text, QString *error = nullptr);

    bool is3D() const { return m_is3D; }
    int size() const { return m_size; }
    QString title() const { return m_title; }
    // r, g, b in 0..1 (in and out).
    void apply(float &r, float &g, float &b) const;

private:
    bool m_is3D = true;
    int m_size = 0;
    QString m_title;
    float m_min[3] = {0, 0, 0};
    float m_max[3] = {1, 1, 1};
    QVector<float> m_data; // rgb triplets; red changes fastest
};

} // namespace bc
