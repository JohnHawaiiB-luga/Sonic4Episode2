#include "nn_normal_matrix.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

namespace {

using MatrixBits = std::array<std::uint32_t, 16u>;
using NormalBits = std::array<std::uint32_t, 9u>;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(const float& value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

NnShaderMatrix matrix_from_bits(const MatrixBits& bits) {
    NnShaderMatrix matrix{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        matrix[index] = float_from_bits(bits[index]);
    }
    return matrix;
}

MatrixBits matrix_bits(const NnShaderMatrix& matrix) {
    MatrixBits bits{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        bits[index] = float_bits(matrix[index]);
    }
    return bits;
}

NormalBits normal_bits(const NnNormalMatrix& matrix) {
    NormalBits bits{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        bits[index] = float_bits(matrix[index]);
    }
    return bits;
}

bool check_normal_bits(const NnNormalMatrix& actual, const NormalBits& expected, const char* message) {
    return check(normal_bits(actual) == expected, message);
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

MatrixBits identity_bits() {
    return {{
        0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
}

bool test_invertible_fixtures_preserve_original_float_bits() {
    const MatrixBits diagonal_bits = {{
        0x40000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x3f000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0xc0800000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const MatrixBits mixed_bits = {{
        0x3fc00000u, 0xbe800000u, 0x40000000u, 0x00000000u,
        0x3f800000u, 0x40400000u, 0x3f000000u, 0x00000000u,
        0xc0000000u, 0x3f400000u, 0x40800000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const NormalBits identity_expected = {{
        0x3f800000u, 0x80000000u, 0x00000000u,
        0x00000000u, 0x3f800000u, 0x80000000u,
        0x00000000u, 0x80000000u, 0x3f800000u,
    }};
    const NormalBits diagonal_expected = {{
        0x3f000000u, 0x80000000u, 0x80000000u,
        0x80000000u, 0x40000000u, 0x00000000u,
        0x80000000u, 0x00000000u, 0xbe800000u,
    }};
    const NormalBits mixed_expected = {{
        0x3eb8eaa0u, 0xbe1f1166u, 0x3e56bde3u,
        0x3d9f1166u, 0x3e9f1166u, 0xbc9f1166u,
        0xbe42dbb7u, 0x3d1f1166u, 0x3e171d54u,
    }};
    const std::array<CameraPrecision, 2u> precisions = {{CameraPrecision::Single, CameraPrecision::Double}};

    for (CameraPrecision precision : precisions) {
        if (!check_normal_bits(
                nn_normal_matrix(matrix_from_bits(identity_bits()), precision),
                identity_expected,
                "identity normal matrix bits differ") ||
            !check_normal_bits(
                nn_normal_matrix(matrix_from_bits(diagonal_bits), precision),
                diagonal_expected,
                "nonuniform diagonal normal matrix bits differ") ||
            !check_normal_bits(
                nn_normal_matrix(matrix_from_bits(mixed_bits), precision),
                mixed_expected,
                "mixed-sign normal matrix bits differ")) {
            return false;
        }
    }
    return true;
}

bool test_singular_matrices_return_positive_zeros() {
    const MatrixBits zero_bits = {{
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const MatrixBits duplicate_row_bits = {{
        0x3f800000u, 0x40000000u, 0x40400000u, 0x00000000u,
        0x3f800000u, 0x40000000u, 0x40400000u, 0x00000000u,
        0x40800000u, 0x40a00000u, 0x40c00000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const NormalBits zero_expected{};
    const std::array<CameraPrecision, 2u> precisions = {{CameraPrecision::Single, CameraPrecision::Double}};

    for (CameraPrecision precision : precisions) {
        if (!check_normal_bits(
                nn_normal_matrix(matrix_from_bits(zero_bits), precision),
                zero_expected,
                "all-zero normal matrix did not return positive zeros") ||
            !check_normal_bits(
                nn_normal_matrix(matrix_from_bits(duplicate_row_bits), precision),
                zero_expected,
                "singular normal matrix did not return positive zeros")) {
            return false;
        }
    }
    return true;
}

bool test_ignores_unused_words_and_preserves_input() {
    const NormalBits identity_expected = {{
        0x3f800000u, 0x80000000u, 0x00000000u,
        0x00000000u, 0x3f800000u, 0x80000000u,
        0x00000000u, 0x80000000u, 0x3f800000u,
    }};
    NnShaderMatrix matrix = matrix_from_bits(identity_bits());
    matrix[3u] = float_from_bits(0x7fc00001u);
    matrix[7u] = float_from_bits(0xff800000u);
    matrix[11u] = float_from_bits(0x7f800000u);
    matrix[12u] = float_from_bits(0xffc00002u);
    matrix[13u] = float_from_bits(0x7f800001u);
    matrix[14u] = float_from_bits(0xffffffffu);
    matrix[15u] = float_from_bits(0xff800001u);
    const MatrixBits original = matrix_bits(matrix);

    const NnNormalMatrix actual = nn_normal_matrix(matrix, CameraPrecision::Single);
    return check_normal_bits(actual, identity_expected, "unused matrix words affected normal matrix output") &&
           check(matrix_bits(matrix) == original, "normal matrix calculation modified its input");
}

bool test_rejects_invalid_domain_without_mutating_input() {
    {
        NnShaderMatrix matrix = matrix_from_bits(identity_bits());
        const MatrixBits original = matrix_bits(matrix);
        if (!expects_invalid_argument(
                [&]() { static_cast<void>(nn_normal_matrix(matrix, static_cast<CameraPrecision>(99))); },
                "unsupported normal matrix precision was accepted") ||
            !check(matrix_bits(matrix) == original, "unsupported precision modified normal matrix input")) {
            return false;
        }
    }

    const std::array<std::uint32_t, 3u> nonfinite = {{0x7fc00000u, 0x7f800000u, 0xff800000u}};
    const std::array<std::size_t, 3u> consumed_indices = {{0u, 5u, 10u}};
    for (std::size_t index = 0u; index < nonfinite.size(); ++index) {
        NnShaderMatrix matrix = matrix_from_bits(identity_bits());
        matrix[consumed_indices[index]] = float_from_bits(nonfinite[index]);
        const MatrixBits original = matrix_bits(matrix);
        if (!expects_invalid_argument(
                [&]() { static_cast<void>(nn_normal_matrix(matrix, CameraPrecision::Double)); },
                "nonfinite consumed normal matrix source was accepted") ||
            !check(matrix_bits(matrix) == original, "rejected normal matrix input was modified")) {
            return false;
        }
    }
    return true;
}

}

int main() {
    return test_invertible_fixtures_preserve_original_float_bits() &&
                   test_singular_matrices_return_positive_zeros() &&
                   test_ignores_unused_words_and_preserves_input() &&
                   test_rejects_invalid_domain_without_mutating_input()
        ? 0
        : 1;
}
