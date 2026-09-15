#include "nn_weighted_matrix_subset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using MatrixBits = std::array<std::uint32_t, 16u>;
using NormalBits = std::array<std::uint32_t, 9u>;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
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

std::vector<MatrixBits> palette_bits(const std::vector<NnShaderMatrix>& palette) {
    std::vector<MatrixBits> bits;
    bits.reserve(palette.size());
    for (const NnShaderMatrix& matrix : palette) {
        bits.push_back(matrix_bits(matrix));
    }
    return bits;
}

bool check_model_view_bits(
    const std::vector<NnShaderMatrix>& actual,
    const std::vector<MatrixBits>& expected,
    const char* message) {
    if (actual.size() != expected.size()) {
        return check(false, message);
    }
    for (std::size_t index = 0u; index < actual.size(); ++index) {
        if (matrix_bits(actual[index]) != expected[index]) {
            return check(false, message);
        }
    }
    return true;
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

MatrixBits nonuniform_diagonal_bits() {
    return {{
        0x40000000u, 0x00000000u, 0x00000000u, 0x80000000u,
        0x00000000u, 0x3f000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0xc0800000u, 0x00000000u,
        0x40a00000u, 0xc0e00000u, 0x41100000u, 0x3f800000u,
    }};
}

bool test_reorders_duplicates_and_preserves_source_bits() {
    MatrixBits first = nonuniform_diagonal_bits();
    MatrixBits second = identity_bits();
    MatrixBits third = identity_bits();
    second[0u] = 0x40400000u;
    second[5u] = 0xc0000000u;
    second[10u] = 0x3e800000u;
    second[7u] = 0x80000000u;
    third[0u] = 0x3f400000u;
    third[1u] = 0xbf800000u;
    third[4u] = 0x3f000000u;
    third[5u] = 0x40000000u;
    third[10u] = 0x40800000u;
    third[12u] = 0x80000000u;

    std::vector<NnShaderMatrix> palette = {
        matrix_from_bits(first), matrix_from_bits(second), matrix_from_bits(third),
    };
    const std::vector<MatrixBits> source_before = palette_bits(palette);
    const std::vector<std::uint32_t> subset = {2u, 0u, 2u};

    const NnWeightedMatrixSubset actual =
        nn_weighted_matrix_subset(palette, subset, 0x17003u, CameraPrecision::Single);
    return check(actual.model_view.size() == subset.size() && actual.normal.size() == subset.size(),
                 "reordered subset did not retain its supplied count") &&
           check_model_view_bits(actual.model_view, std::vector<MatrixBits>{third, first, third},
                                 "subset copy lost order, duplicates, or float bits") &&
           check(normal_bits(actual.normal[0u]) == normal_bits(nn_normal_matrix(palette[2u], CameraPrecision::Single)) &&
                     normal_bits(actual.normal[1u]) == normal_bits(nn_normal_matrix(palette[0u], CameraPrecision::Single)) &&
                     normal_bits(actual.normal[2u]) == normal_bits(nn_normal_matrix(palette[2u], CameraPrecision::Single)),
                 "subset normal matrices did not follow the compacted palette order") &&
           check(palette_bits(palette) == source_before, "subset construction modified its palette input");
}

bool test_nonindexed_single_slot_and_nonuniform_normal() {
    const MatrixBits source_bits = nonuniform_diagonal_bits();
    const NormalBits expected_normal = {{
        0x3f000000u, 0x80000000u, 0x80000000u,
        0x80000000u, 0x40000000u, 0x00000000u,
        0x80000000u, 0x00000000u, 0xbe800000u,
    }};
    const std::vector<NnShaderMatrix> palette = {matrix_from_bits(source_bits)};
    const std::vector<std::uint32_t> subset = {0u};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnWeightedMatrixSubset actual = nn_weighted_matrix_subset(palette, subset, 0x17003u, precision);
        if (!check(actual.model_view.size() == 1u && actual.normal.size() == 1u,
                   "nonindexed one-slot subset returned extra storage") ||
            !check_model_view_bits(actual.model_view, std::vector<MatrixBits>{source_bits},
                                   "nonindexed one-slot subset changed its matrix bits") ||
            !check(normal_bits(actual.normal[0u]) == expected_normal,
                   "nonuniform normal matrix did not match the hand-computable inverse scale")) {
            return false;
        }
    }
    return true;
}

bool test_format_capacity_boundaries() {
    const NnShaderMatrix identity = matrix_from_bits(identity_bits());
    const std::vector<NnShaderMatrix> palette(16u, identity);
    const std::vector<std::uint32_t> nonindexed = {0u, 1u, 2u, 3u};
    const std::vector<std::uint32_t> indexed = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
    };

    const NnWeightedMatrixSubset nonindexed_actual =
        nn_weighted_matrix_subset(palette, nonindexed, 0x17003u, CameraPrecision::Single);
    const NnWeightedMatrixSubset indexed_actual =
        nn_weighted_matrix_subset(palette, indexed, 0x17403u, CameraPrecision::Double);
    return check(nonindexed_actual.model_view.size() == 4u && nonindexed_actual.normal.size() == 4u,
                 "nonindexed capacity boundary returned the wrong count") &&
           check(indexed_actual.model_view.size() == 16u && indexed_actual.normal.size() == 16u,
                 "indexed capacity boundary returned the wrong count");
}

bool test_rejects_invalid_domain_and_ignores_inactive_entries() {
    const NnShaderMatrix identity = matrix_from_bits(identity_bits());
    const std::vector<std::uint32_t> one_slot = {0u};

    {
        const std::vector<NnShaderMatrix> palette = {identity};
        if (!expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(
                        palette, one_slot, 0x17003u, static_cast<CameraPrecision>(99)));
                },
                "unsupported subset precision was accepted") ||
            !expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(palette, one_slot, 0u, CameraPrecision::Single));
                },
                "unsupported subset vertex format was accepted") ||
            !expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(
                        palette, std::vector<std::uint32_t>{}, 0x17003u, CameraPrecision::Single));
                },
                "empty matrix subset was accepted") ||
            !expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(
                        palette,
                        std::vector<std::uint32_t>{0u, 0u, 0u, 0u, 0u},
                        0x17003u,
                        CameraPrecision::Single));
                },
                "overcapacity nonindexed subset was accepted") ||
            !expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(
                        palette,
                        std::vector<std::uint32_t>{
                            0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                            0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
                        0x17403u,
                        CameraPrecision::Single));
                },
                "overcapacity indexed subset was accepted") ||
            !expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(
                        palette,
                        std::vector<std::uint32_t>{std::numeric_limits<std::uint32_t>::max()},
                        0x17003u,
                        CameraPrecision::Single));
                },
                "out-of-range matrix index was accepted")) {
            return false;
        }
    }

    {
        std::vector<NnShaderMatrix> palette = {identity};
        palette[0u][3u] = float_from_bits(0x7fc00001u);
        const std::vector<MatrixBits> source_before = palette_bits(palette);
        if (!expects_invalid_argument(
                [&]() {
                    static_cast<void>(nn_weighted_matrix_subset(palette, one_slot, 0x17003u, CameraPrecision::Double));
                },
                "nonfinite active padding matrix word was accepted") ||
            !check(palette_bits(palette) == source_before, "rejected active matrix modified the palette")) {
            return false;
        }
    }

    {
        std::vector<NnShaderMatrix> palette = {identity, identity};
        palette[1u][0u] = float_from_bits(0x7fc00001u);
        palette[1u][5u] = float_from_bits(0x7f800000u);
        const std::vector<MatrixBits> source_before = palette_bits(palette);
        const NnWeightedMatrixSubset actual =
            nn_weighted_matrix_subset(palette, one_slot, 0x17003u, CameraPrecision::Single);
        if (!check(actual.model_view.size() == 1u && actual.normal.size() == 1u,
                   "inactive palette entry changed the active subset count") ||
            !check_model_view_bits(actual.model_view, std::vector<MatrixBits>{identity_bits()},
                                   "inactive nonfinite entry changed the selected matrix") ||
            !check(palette_bits(palette) == source_before, "accepted subset modified an inactive palette entry")) {
            return false;
        }
    }

    return true;
}

}

int main() {
    return test_reorders_duplicates_and_preserves_source_bits() &&
                   test_nonindexed_single_slot_and_nonuniform_normal() &&
                   test_format_capacity_boundaries() &&
                   test_rejects_invalid_domain_and_ignores_inactive_entries()
        ? 0
        : 1;
}
