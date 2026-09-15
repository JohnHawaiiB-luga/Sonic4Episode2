#include "stage_scene.h"

#include <cmath>
#include <cstring>

std::vector<StageSceneInstance> assemble_pc_stage_instances(
    const StageGridData& mp,
    const StageGridData& md,
    const std::vector<StageSceneModel>& models,
    const StageMapRange& range,
    const std::array<float, 3u>& offsets,
    StageTransformPrecision precision) {
    if (models.empty() || models.size() > 4096u) {
        throw StageDataError("stage scene model count is unsupported");
    }
    if (precision != StageTransformPrecision::Single && precision != StageTransformPrecision::Double) {
        throw StageDataError("stage scene precision is unsupported");
    }
    for (const float offset : offsets) {
        if (!std::isfinite(offset) || std::fabs(offset) > 16777216.0f) {
            throw StageDataError("stage scene offsets are nonfinite or out of range");
        }
    }

    const auto placements = resolve_stage_map(mp, md, static_cast<std::uint16_t>(models.size()), range);
    std::vector<StageSceneInstance> instances;
    instances.reserve(placements.size());
    for (const auto& placement : placements) {
        const auto& model = models[placement.model_id];
        if (!model.first_node_translation_bits.has_value()) {
            throw StageDataError("stage scene selected model has no first node translation");
        }
        std::array<float, 3u> pivot{};
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        for (std::size_t axis = 0u; axis < pivot.size(); ++axis) {
            std::memcpy(&pivot[axis], &(*model.first_node_translation_bits)[axis], sizeof(float));
        }
        instances.push_back({placement, make_pc_stage_transform(
            placement, pivot, offsets, (model.archive_flags & 4u) != 0u, precision)});
    }
    return instances;
}
