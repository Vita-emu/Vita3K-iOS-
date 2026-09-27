#pragma once
#include <algorithm>
#include <cmath>
namespace overlay_layout {
struct Rect {
    double x, y, width, height;
};
inline double normalized(double value) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}
inline Rect panel(double width, double height, double left, double top,
    double right, double bottom, double x, double y, bool collapsed) {
    const double available_width = std::max(0.0, width - left - right - 16);
    const double available_height = std::max(0.0, height - top - bottom - 16);
    const double w = std::min(300.0, available_width);
    const double h = std::min(collapsed ? 32.0 : 116.0, available_height);
    return { left + 8 + normalized(x) * (available_width - w),
        top + 8 + normalized(y) * (available_height - h), w, h };
}
} // namespace overlay_layout
