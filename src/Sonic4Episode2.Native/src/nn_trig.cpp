#include "nn_trig.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {

constexpr std::size_t kQuarterWaveSampleCount = 1025u;
constexpr double kPi = 3.14159265358979323846264338327950288419716939937510;

const std::array<float, kQuarterWaveSampleCount>& quarter_wave_table() {
    static const std::array<float, kQuarterWaveSampleCount> table = [] {
        std::array<float, kQuarterWaveSampleCount> samples{};
        for (std::size_t index = 0u; index < samples.size(); ++index) {
            const float radians = static_cast<float>(static_cast<double>(index) * kPi / 2048.0);
            const float value = static_cast<float>(std::sin(static_cast<double>(radians)));
            samples[index] = static_cast<float>(std::round(static_cast<double>(value) * 1000000.0) / 1000000.0);
        }
        return samples;
    }();
    return table;
}

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        throw std::invalid_argument("trigonometric precision is unsupported");
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double || !std::isfinite(value)) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double subtract(double left, double right) const {
        return rounded(left - right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

}

float nn_sin(std::uint16_t angle) {
    const std::uint16_t quadrant = static_cast<std::uint16_t>((angle >> 14u) & 3u);
    const std::size_t index = static_cast<std::size_t>((angle >> 4u) & 1023u);
    const std::uint16_t remainder = static_cast<std::uint16_t>(angle & 15u);
    const std::array<float, kQuarterWaveSampleCount>& table = quarter_wave_table();

    const float base = (quadrant & 1u) == 0u ? table[index] : table[1024u - index];
    const float neighbor = (quadrant & 1u) == 0u ? table[index + 1u] : table[1023u - index];
    float result = base;
    if (remainder != 0u) {
        const double fraction = static_cast<double>(remainder) / 16.0;
        result = static_cast<float>(
            static_cast<double>(base) + (static_cast<double>(neighbor) - static_cast<double>(base)) * fraction);
    }

    return (quadrant & 2u) == 0u ? result : -result;
}

float nn_cos(std::uint16_t angle) {
    return nn_sin(static_cast<std::uint16_t>(angle + 16384u));
}

NnSinCos nn_sin_cos(std::uint16_t angle, CameraPrecision precision) {
    require_precision(precision);

    const std::uint16_t quadrant = static_cast<std::uint16_t>((angle >> 14u) & 3u);
    const std::size_t index = static_cast<std::size_t>((angle >> 4u) & 1023u);
    const std::uint16_t remainder = static_cast<std::uint16_t>(angle & 15u);
    const std::array<float, kQuarterWaveSampleCount>& table = quarter_wave_table();
    const Arithmetic arithmetic{precision};

    float lower = table[index];
    float upper = table[1024u - index];
    if (remainder != 0u) {
        const double fraction = static_cast<double>(remainder) / 16.0;
        const double lower_delta = arithmetic.subtract(
            static_cast<double>(table[index + 1u]),
            static_cast<double>(lower));
        lower = arithmetic.spill(arithmetic.add(
            static_cast<double>(lower),
            arithmetic.multiply(lower_delta, fraction)));

        const double upper_delta = arithmetic.subtract(
            static_cast<double>(table[1023u - index]),
            static_cast<double>(upper));
        upper = arithmetic.spill(arithmetic.add(
            static_cast<double>(upper),
            arithmetic.multiply(upper_delta, fraction)));
    }

    if (quadrant == 0u) {
        return {lower, upper};
    }
    if (quadrant == 1u) {
        return {upper, -lower};
    }
    if (quadrant == 2u) {
        return {-lower, -upper};
    }
    return {-upper, lower};
}
