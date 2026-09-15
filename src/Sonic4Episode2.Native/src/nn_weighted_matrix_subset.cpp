#include "nn_weighted_matrix_subset.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::uint32_t kNonindexedVertexFormat = 0x17003u;
constexpr std::uint32_t kIndexedVertexFormat = 0x17403u;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "weighted matrix subset precision is unsupported");
}

std::size_t maximum_subset_size(std::uint32_t vertex_format) {
    if (vertex_format == kNonindexedVertexFormat) {
        return 4u;
    }
    if (vertex_format == kIndexedVertexFormat) {
        return 16u;
    }
    throw std::invalid_argument("weighted matrix subset vertex format is unsupported");
}

void require_finite_matrix(const NnShaderMatrix& matrix) {
    for (float value : matrix) {
        require(std::isfinite(value), "weighted matrix subset source contains a nonfinite value");
    }
}

}

NnWeightedMatrixSubset nn_weighted_matrix_subset(
    const std::vector<NnShaderMatrix>& palette,
    const std::vector<std::uint32_t>& subset,
    std::uint32_t vertex_format,
    CameraPrecision precision) {
    require_precision(precision);
    const std::size_t maximum_size = maximum_subset_size(vertex_format);
    require(!subset.empty(), "weighted matrix subset is empty");
    require(subset.size() <= maximum_size, "weighted matrix subset exceeds its vertex format capacity");

    NnWeightedMatrixSubset result;
    result.model_view.reserve(subset.size());
    result.normal.reserve(subset.size());
    for (std::uint32_t index : subset) {
        require(index < palette.size(), "weighted matrix subset index is out of range");
        const NnShaderMatrix& model_view = palette[index];
        require_finite_matrix(model_view);
        result.model_view.emplace_back();
        std::memcpy(
            result.model_view.back().data(),
            model_view.data(),
            model_view.size() * sizeof(float));
        result.normal.push_back(nn_normal_matrix(model_view, precision));
    }
    return result;
}
