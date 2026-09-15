#include "stage_scene.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

template <typename Callable>
bool rejects(Callable&& callable) {
    try {
        callable();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        return check(false, "wrong scene exception type");
    }
    return check(false, "malformed scene input was accepted");
}

std::uint32_t bits(float value) {
    std::uint32_t word = 0u;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

bool test_assembly() {
    const StageGridData mp{3u, 1u, 2u, {1u, 0u, 0x4002u}};
    const StageGridData md{3u, 1u, 1u, {0u, 0x0Fu, 0u}};
    std::vector<StageSceneModel> models(3u);
    models[1] = {0x80000004u, std::array<std::uint32_t, 3u>{{0u, 0u, 0u}}};
    models[2] = {0xFFFFFFFBu, std::array<std::uint32_t, 3u>{{0u, 0u, 0u}}};
    const auto single = assemble_pc_stage_instances(mp, md, models, {0, 2, 0, 0},
                                                   {0.0f, 0.0f, 0.0f}, StageTransformPrecision::Single);
    const auto wide = assemble_pc_stage_instances(mp, md, models, {0, 2, 0, 0},
                                                 {0.0f, 0.0f, 0.0f}, StageTransformPrecision::Double);
    const auto redirected = assemble_pc_stage_instances(mp, md, models, {1, 1, 0, 0},
                                                       {0.0f, 0.0f, 0.0f}, StageTransformPrecision::Single);
    if (!check(single.size() == 2u && wide.size() == 2u && redirected.size() == 1u,
               "scene did not preserve anchor deduplication") ||
        !check(single[0].placement.anchor_x == 0u && single[0].placement.model_id == 1u &&
                   single[1].placement.anchor_x == 2u && single[1].placement.model_id == 2u &&
                   single[1].placement.flip_x,
               "scene changed anchor order or flags") ||
        !check(bits(single[0].transform[12u]) == 0u && bits(wide[0].transform[12u]) == 0xB5000000u &&
                   single[0].transform[13u] == -64.0f && single[0].transform[14u] == -32.0f,
               "scene lost precision or model depth selection") ||
        !check(single[1].transform[12u] == 192.0f && single[1].transform[14u] == 0.0f,
               "unrelated archive flags changed model depth") ||
        !check(redirected[0].transform == single[0].transform,
               "redirect used query coordinates instead of anchor coordinates")) {
        return false;
    }
    models[1].first_node_translation_bits.reset();
    return check(single[0].transform[14u] == -32.0f && mp.cells[1] == 0u && md.cells[1] == 0x0Fu,
                 "scene retained model storage or mutated input grids");
}

bool test_validation() {
    const StageGridData mp{1u, 1u, 2u, {1u}};
    const StageGridData md{1u, 1u, 1u, {0u}};
    std::vector<StageSceneModel> models(2u);
    models[1].first_node_translation_bits = {{0u, 0u, 0u}};
    const auto run = [&](const std::vector<StageSceneModel>& table) {
        return assemble_pc_stage_instances(mp, md, table, {0, 0, 0, 0},
                                           {0.0f, 0.0f, 0.0f}, StageTransformPrecision::Single);
    };
    if (!rejects([&]() { run({}); }) ||
        !rejects([&]() { run(std::vector<StageSceneModel>(4097u)); }) ||
        !rejects([&]() { run(std::vector<StageSceneModel>(2u)); }) ||
        !rejects([&]() { assemble_pc_stage_instances(mp, md, models, {1, 0, 0, 0},
                {0.0f, 0.0f, 0.0f}, static_cast<StageTransformPrecision>(2)); }) ||
        !rejects([&]() { assemble_pc_stage_instances(mp, md, models, {1, 0, 0, 0},
                {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, StageTransformPrecision::Single); })) {
        return false;
    }
    models[1].first_node_translation_bits = {{0x7FC01234u, 0u, 0u}};
    if (!rejects([&]() { run(models); })) {
        return false;
    }
    return check(assemble_pc_stage_instances(mp, md, models, {1, 0, 0, 0},
                 {0.0f, 0.0f, 0.0f}, StageTransformPrecision::Single).empty(),
                 "empty query consumed an unused model");
}

}

int main() {
    return test_assembly() && test_validation() ? 0 : 1;
}
