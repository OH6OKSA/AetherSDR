#pragma once

#include <QColor>

#include <algorithm>
#include <iterator>

namespace AetherSDR::mobile {

// The desktop's default waterfall colormap (theme "waterfall.colormap.default"),
// shared by the waterfall and the 3D view as on the desktop.
inline QRgb spectrumPaletteAt(float v)
{
    struct Stop { float at; int r; int g; int b; };
    static constexpr Stop kStops[] = {
        {0.00f, 0x00, 0x00, 0x00}, {0.15f, 0x00, 0x00, 0x80}, {0.30f, 0x00, 0x40, 0xff},
        {0.45f, 0x00, 0xc8, 0xff}, {0.60f, 0x00, 0xdc, 0x00}, {0.80f, 0xff, 0xff, 0x00},
        {1.00f, 0xff, 0x00, 0x00},
    };
    v = std::clamp(v, 0.0f, 1.0f);
    for (size_t i = 1; i < std::size(kStops); ++i) {
        if (v <= kStops[i].at) {
            const Stop& a = kStops[i - 1];
            const Stop& b = kStops[i];
            const float t = (v - a.at) / (b.at - a.at);
            return qRgb(static_cast<int>(a.r + (b.r - a.r) * t), static_cast<int>(a.g + (b.g - a.g) * t),
                        static_cast<int>(a.b + (b.b - a.b) * t));
        }
    }
    return qRgb(0xff, 0x00, 0x00);
}

} // namespace AetherSDR::mobile
