#include "nn_normal_matrix.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {

constexpr std::array<std::size_t, 9u> kConsumedIndices = {{0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u}};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "normal matrix precision is unsupported");
}

void require_finite_input(const NnShaderMatrix& model_view) {
    for (std::size_t index : kConsumedIndices) {
        require(std::isfinite(model_view[index]), "normal matrix source contains a nonfinite value");
    }
}

void require_finite_output(float value) {
    require(std::isfinite(value), "normal matrix result is nonfinite");
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

    double divide(double left, double right) const {
        return rounded(left / right);
    }
};

float spill(double value) {
    const float result = static_cast<float>(value);
    require_finite_output(result);
    return result;
}

float spill_negated(double value) {
    return spill(-value);
}

}

NnNormalMatrix nn_normal_matrix(const NnShaderMatrix& model_view, CameraPrecision precision) {
    require_precision(precision);
    require_finite_input(model_view);

    const float a = model_view[0u];
    const float b = model_view[1u];
    const float c = model_view[2u];
    const float d = model_view[4u];
    const float e = model_view[5u];
    const float f = model_view[6u];
    const float g = model_view[8u];
    const float h = model_view[9u];
    const float i = model_view[10u];
    const Arithmetic arithmetic{precision};

    const float cofactor00 = spill(arithmetic.subtract(arithmetic.multiply(e, i), arithmetic.multiply(f, h)));
    const float cofactor10 = spill(arithmetic.subtract(arithmetic.multiply(c, h), arithmetic.multiply(b, i)));
    const float cofactor20 = spill(arithmetic.subtract(arithmetic.multiply(b, f), arithmetic.multiply(c, e)));

    const double determinant_first_terms = arithmetic.add(
        arithmetic.multiply(a, cofactor00),
        arithmetic.multiply(d, cofactor10));
    const float determinant = spill(arithmetic.add(
        arithmetic.multiply(g, cofactor20),
        determinant_first_terms));
    if (determinant == 0.0f) {
        return {};
    }

    const float reciprocal = spill(arithmetic.divide(1.0, determinant));
    NnNormalMatrix result{};
    result[0u] = spill(arithmetic.multiply(reciprocal, cofactor00));
    result[1u] = spill_negated(arithmetic.multiply(
        reciprocal,
        arithmetic.subtract(arithmetic.multiply(i, d), arithmetic.multiply(f, g))));
    result[2u] = spill(arithmetic.multiply(
        arithmetic.subtract(arithmetic.multiply(h, d), arithmetic.multiply(e, g)),
        reciprocal));
    result[3u] = spill(arithmetic.multiply(reciprocal, cofactor10));
    result[4u] = spill(arithmetic.multiply(
        reciprocal,
        arithmetic.subtract(arithmetic.multiply(i, a), arithmetic.multiply(c, g))));
    result[5u] = spill_negated(arithmetic.multiply(
        reciprocal,
        arithmetic.subtract(arithmetic.multiply(h, a), arithmetic.multiply(b, g))));
    result[6u] = spill(arithmetic.multiply(reciprocal, cofactor20));
    result[7u] = spill_negated(arithmetic.multiply(
        reciprocal,
        arithmetic.subtract(arithmetic.multiply(f, a), arithmetic.multiply(c, d))));
    result[8u] = spill(arithmetic.multiply(
        reciprocal,
        arithmetic.subtract(arithmetic.multiply(e, a), arithmetic.multiply(d, b))));
    return result;
}
