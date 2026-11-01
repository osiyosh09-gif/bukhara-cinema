#pragma once

#include <QImage>
#include <QSize>

#include <array>

namespace bc::Scopes {

// Video scopes of one frame (phase 9c), drawn as images: what the Scopes
// panel shows next to the Program monitor.
enum class Kind {
    Waveform,    // brightness (luma) of each column, 0 % at the bottom, 100 % at the top
    Parade,      // the same for red, green and blue side by side
    Vectorscope, // hue as angle, saturation as distance from the centre
    Histogram,   // how many pixels have each level, per channel
};

struct Stats
{
    std::array<int, 256> red{};
    std::array<int, 256> green{};
    std::array<int, 256> blue{};
    std::array<int, 256> luma{};
    int pixels = 0;
};

// Rec. 709 luma of an 8-bit RGB value (0..255).
int luma(int r, int g, int b);
// Chroma of an 8-bit RGB value: Cb (blue - luma) and Cr (red - luma), -128..127.
void chroma(int r, int g, int b, double *cb, double *cr);

Stats statistics(const QImage &frame);
// The frame is reduced to at most `analyseWidth` columns first (speed).
QImage render(const QImage &frame, Kind kind, QSize size, int analyseWidth = 320);

} // namespace bc::Scopes
