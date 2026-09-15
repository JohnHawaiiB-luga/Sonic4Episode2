#include "ambient_field.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0u]) |
           (static_cast<std::uint32_t>(data[1u]) << 8u) |
           (static_cast<std::uint32_t>(data[2u]) << 16u) |
           (static_cast<std::uint32_t>(data[3u]) << 24u);
}

float read_float(const std::uint8_t* data) {
    const std::uint32_t bits = read_le32(data);
    float value;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) throw StageDataError("stage ambient palette is nonfinite");
    return value;
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        require(std::isfinite(value), "ambient field arithmetic is nonfinite");
        if (precision == CameraPrecision::Double) return value;
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }
    double add(double a, double b) const { return rounded(a + b); }
    double subtract(double a, double b) const { return rounded(a - b); }
    double multiply(double a, double b) const { return rounded(a * b); }
    double divide(double a, double b) const { return rounded(a / b); }
};

float spill(double value) {
    const float result = static_cast<float>(value);
    require(std::isfinite(result), "ambient field stored value is nonfinite");
    return result;
}

float axis_weight(
    float coordinate, float low, float high, float fade,
    bool low_disabled, bool high_disabled, const Arithmetic& arithmetic) {
    const double midpoint = arithmetic.add(low, arithmetic.multiply(arithmetic.subtract(high, low), 0.5));
    const float side = spill(arithmetic.subtract(coordinate, midpoint));
    if (side < 0.0f && !low_disabled) {
        return spill(std::min(1.0, arithmetic.divide(arithmetic.subtract(coordinate, low), fade)));
    }
    if (side > 0.0f && !high_disabled) {
        return spill(std::min(1.0, arithmetic.divide(arithmetic.subtract(coordinate, high), -fade)));
    }
    return 1.0f;
}

}

AmbientFieldPalettes parse_pc_stage_ambient_palettes(
    const std::uint8_t* data, std::size_t size, std::uint32_t stage_index) {
    if (data == nullptr || size < 0x34u) throw StageDataError("stage ambient GPB header is truncated");
    if (read_le32(data) != 0x00425047u || read_le32(data + 4u) != 0x01010000u ||
        std::memcmp(data + 8u, "stage_env", 10u) != 0) {
        throw StageDataError("stage ambient GPB schema is unsupported");
    }
    const std::size_t table = read_le32(data + 0x28u);
    const std::size_t stride = read_le32(data + 0x2cu);
    const std::size_t count = read_le32(data + 0x30u);
    if (stride != 0x220u || table < 0x34u || table > size ||
        count > (size - table) / stride || stage_index >= count) {
        throw StageDataError("stage ambient GPB record table is invalid");
    }
    const std::size_t record = table + static_cast<std::size_t>(stage_index) * stride;
    AmbientFieldPalettes result{};
    for (std::size_t palette = 0u; palette < result.size(); ++palette) {
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            result[palette][channel] = read_float(data + record + 0x94u + palette * 0x14u + channel * 4u);
        }
        result[palette][3u] = 1.0f;
    }
    return result;
}

std::vector<AmbientFieldPoint> make_ambient_field_points(const StageEventPlacements& events) {
    std::vector<AmbientFieldPoint> result;
    for (const StageEventPlacement& event : events.events) {
        if (event.object_id != 476u) continue;
        result.push_back({
            {{static_cast<float>(event.x), static_cast<float>(event.y)}},
            {{static_cast<float>(2 * event.left), static_cast<float>(2 * event.top),
              static_cast<float>(2 * (static_cast<int>(event.left) + event.width)),
              static_cast<float>(2 * (static_cast<int>(event.top) + event.height))}},
            static_cast<std::uint8_t>(event.flags & 7u),
            static_cast<std::uint8_t>((event.flags >> 3u) & 15u),
            32.0f * static_cast<float>(1u + ((event.flags >> 7u) & 1u)),
        });
    }
    return result;
}

std::array<float, 4u> ambient_field_ring_color(
    const std::vector<AmbientFieldPoint>& points,
    const AmbientFieldPalettes& palettes,
    const std::array<float, 2u>& position,
    CameraPrecision precision) {
    require(precision == CameraPrecision::Single || precision == CameraPrecision::Double,
            "ambient field precision is unsupported");
    require(std::isfinite(position[0u]) && std::isfinite(position[1u]), "ambient field position is nonfinite");
    const Arithmetic arithmetic{precision};
    std::array<float, 3u> accumulated{};
    for (const AmbientFieldPoint& point : points) {
        require(point.palette_index < palettes.size() && (point.disabled_edges & ~15u) == 0u,
                "ambient field point flags are unsupported");
        require(std::isfinite(point.center[0u]) && std::isfinite(point.center[1u]) &&
                    std::isfinite(point.fade) && point.fade > 0.0f,
                "ambient field point parameters are invalid");
        for (float bound : point.bounds) require(std::isfinite(bound), "ambient field bound is nonfinite");
        require(point.bounds[0u] <= point.bounds[2u] && point.bounds[1u] <= point.bounds[3u],
                "ambient field bounds are inverted");
        const float x = spill(arithmetic.subtract(position[0u], point.center[0u]));
        const float y = spill(arithmetic.subtract(position[1u], point.center[1u]));
        if (x < point.bounds[0u] || y < point.bounds[1u] || x > point.bounds[2u] || y > point.bounds[3u]) continue;
        const float wx = axis_weight(x, point.bounds[0u], point.bounds[2u], point.fade,
                                    (point.disabled_edges & 1u) != 0u, (point.disabled_edges & 4u) != 0u, arithmetic);
        const float wy = axis_weight(y, point.bounds[1u], point.bounds[3u], point.fade,
                                    (point.disabled_edges & 2u) != 0u, (point.disabled_edges & 8u) != 0u, arithmetic);
        const float weight = spill(std::clamp(arithmetic.multiply(wx, wy), 0.0, 1.0));
        for (std::size_t channel = 0u; channel < accumulated.size(); ++channel) {
            const float palette = palettes[point.palette_index][channel];
            require(std::isfinite(palette), "ambient field palette is nonfinite");
            accumulated[channel] = spill(arithmetic.add(accumulated[channel],
                arithmetic.multiply(arithmetic.subtract(1.0, palette), weight)));
        }
    }
    std::array<float, 4u> result{{0.0f, 0.0f, 0.0f, 1.0f}};
    for (std::size_t channel = 0u; channel < accumulated.size(); ++channel) {
        result[channel] = spill(std::clamp(arithmetic.subtract(1.0, accumulated[channel]), 0.0, 1.0));
    }
    return result;
}

