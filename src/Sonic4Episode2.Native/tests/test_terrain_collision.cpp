#include "terrain_collision.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kHeightRecordSize = 4096u;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

template <typename Callable>
bool expects_stage_data_error(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool check_float(float actual, float expected, const char* message) {
    return check(float_bits(actual) == float_bits(expected), message);
}

void write_le16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0x00FFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0x00FFu);
}

TerrainTable make_table(
    TerrainRecordKind kind,
    std::uint16_t record_count = 1u,
    std::vector<std::uint16_t> chip_records = {0u}) {
    return TerrainTable{
        kind,
        record_count,
        std::vector<std::uint8_t>(terrain_record_size(kind) * record_count, 0u),
        std::move(chip_records),
    };
}

struct CollisionInputs {
    StageGridData first;
    StageGridData second;
    TerrainTable height;
    TerrainTable angle;
    TerrainTable attribute;
    TerrainCollisionBounds bounds;
};

CollisionInputs make_inputs(
    std::uint16_t first_cell = 0u,
    std::uint16_t second_cell = 0u,
    std::uint16_t record_count = 1u,
    std::vector<std::uint16_t> chip_records = {0u}) {
    return CollisionInputs{
        StageGridData{1u, 1u, 2u, {first_cell}},
        StageGridData{1u, 1u, 2u, {second_cell}},
        make_table(TerrainRecordKind::Height, record_count, chip_records),
        make_table(TerrainRecordKind::Angle, record_count, chip_records),
        make_table(TerrainRecordKind::Attribute, record_count, std::move(chip_records)),
        TerrainCollisionBounds{0, 0, 64, 64},
    };
}

void write_height(
    TerrainTable& height,
    std::size_t record,
    std::size_t block_x,
    std::size_t block_y,
    std::size_t word,
    std::uint16_t packed) {
    const std::size_t offset = record * kHeightRecordSize +
        (block_y * 8u + block_x) * 64u + word * 2u;
    write_le16(height.records, offset, packed);
}

TerrainCollision make_collision(const CollisionInputs& inputs) {
    return TerrainCollision(
        inputs.first,
        inputs.second,
        inputs.height,
        inputs.angle,
        inputs.attribute,
        inputs.bounds);
}

TerrainCollisionQuery query(
    float x,
    float y,
    std::uint16_t flags,
    std::uint8_t vector,
    std::optional<std::uint16_t> direction = std::optional<std::uint16_t>{
        static_cast<std::uint16_t>(0x5A5Au)},
    std::optional<std::uint32_t> attribute = std::optional<std::uint32_t>{0xDECAFBADu}) {
    return TerrainCollisionQuery{x, y, flags, vector, direction, attribute};
}

bool check_outputs(
    const TerrainCollisionResult& result,
    float distance,
    std::optional<std::uint16_t> direction,
    std::optional<std::uint32_t> attribute,
    const char* message) {
    return check_float(result.distance, distance, message) &&
           check(result.direction == direction, message) &&
           check(result.attribute == attribute, message);
}

bool test_all_transform_literals() {
    struct TransformCase {
        std::uint16_t cell;
        std::uint8_t block_x;
        std::uint8_t block_y;
        std::uint8_t inner_x;
        std::uint8_t inner_y;
        float x_distance;
        float y_distance;
        std::uint16_t direction;
    };
    const std::array<TransformCase, 16u> cases = {{
        {0x0000u, 1u, 2u, 1u, 2u, 0.0f, 1.25f, 0xEE00u},
        {0x1000u, 5u, 1u, 29u, 1u, 1.5f, -1.5f, 0xAE00u},
        {0x2000u, 6u, 5u, 30u, 29u, -1.25f, -1.5f, 0x6E00u},
        {0x3000u, 2u, 6u, 2u, 30u, -1.25f, -0.25f, 0x2E00u},
        {0x4000u, 6u, 2u, 30u, 2u, -1.25f, 1.25f, 0x1200u},
        {0x5000u, 2u, 1u, 2u, 1u, 1.5f, -0.25f, 0xD200u},
        {0x6000u, 1u, 5u, 1u, 29u, 0.0f, -1.5f, 0x9200u},
        {0x7000u, 5u, 6u, 29u, 30u, -1.25f, -1.5f, 0x5200u},
        {0x8000u, 1u, 5u, 1u, 29u, 0.0f, -1.5f, 0x9200u},
        {0x9000u, 5u, 6u, 29u, 30u, -1.25f, -1.5f, 0x5200u},
        {0xA000u, 6u, 2u, 30u, 2u, -1.25f, 1.25f, 0x1200u},
        {0xB000u, 2u, 1u, 2u, 1u, 1.5f, -0.25f, 0xD200u},
        {0xC000u, 6u, 5u, 30u, 29u, -1.25f, -1.5f, 0x6E00u},
        {0xD000u, 2u, 6u, 2u, 30u, -1.25f, -0.25f, 0x2E00u},
        {0xE000u, 1u, 2u, 1u, 2u, 0.0f, 1.25f, 0xEE00u},
        {0xF000u, 5u, 1u, 29u, 1u, 1.5f, -1.5f, 0xAE00u},
    }};

    for (const TransformCase& entry : cases) {
        CollisionInputs inputs = make_inputs(entry.cell);
        const bool odd_rotation = (entry.cell & 0x1000u) != 0u;
        const std::size_t x_word = odd_rotation ? entry.inner_x : entry.inner_y;
        const std::size_t y_word = odd_rotation ? entry.inner_y : entry.inner_x;
        write_height(
            inputs.height,
            0u,
            entry.block_x,
            entry.block_y,
            x_word,
            0x0B05u);
        write_height(
            inputs.height,
            0u,
            entry.block_x,
            entry.block_y,
            y_word,
            0x0B05u);
        const std::size_t slot = static_cast<std::size_t>(entry.block_y) * 8u + entry.block_x;
        inputs.angle.records[slot] = 0x12u;
        inputs.attribute.records[slot] = 0x02u;
        const TerrainCollision collision = make_collision(inputs);
        const TerrainCollisionResult x_result = collision.fast_query(
            query(8.25f, 16.5f, 0u, 0u), TerrainCollisionPrecision::Single);
        const TerrainCollisionResult y_result = collision.fast_query(
            query(8.25f, 16.5f, 0u, 2u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                x_result,
                entry.x_distance,
                std::optional<std::uint16_t>{entry.direction},
                std::optional<std::uint32_t>{2u},
                "transformed X terrain query mismatch") ||
            !check_outputs(
                y_result,
                entry.y_distance,
                std::optional<std::uint16_t>{entry.direction},
                std::optional<std::uint32_t>{2u},
                "transformed Y terrain query mismatch")) {
            return false;
        }
    }
    return true;
}

bool test_endpoint_direction_and_mask_outputs() {
    CollisionInputs endpoint = make_inputs(0x8000u);
    write_height(endpoint.height, 0u, 0u, 7u, 0u, 0x2000u);
    endpoint.angle.records[56u] = 0x12u;
    endpoint.attribute.records[56u] = 0x02u;
    const TerrainCollision endpoint_collision = make_collision(endpoint);
    const TerrainCollisionResult endpoint_result = endpoint_collision.fast_query(
        query(0.0f, 0.0f, 0u, 2u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult endpoint_nullopt = endpoint_collision.fast_query(
        query(0.0f, 0.0f, 0u, 2u, std::nullopt, std::nullopt),
        TerrainCollisionPrecision::Single);
    if (!check_outputs(
            endpoint_result,
            -1.0f,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(0x9200u)},
            std::optional<std::uint32_t>{2u},
            "packed 0x2000 endpoint mismatch") ||
        !check_outputs(
            endpoint_nullopt,
            -1.0f,
            std::nullopt,
            std::nullopt,
            "null optional terrain outputs changed")) {
        return false;
    }

    CollisionInputs raw20 = make_inputs();
    write_height(raw20.height, 0u, 0u, 0u, 0u, 0x0004u);
    raw20.angle.records[0u] = 0x20u;
    raw20.attribute.records[0u] = 0x02u;
    const TerrainCollisionResult raw20_result = make_collision(raw20).fast_query(
        query(0.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    if (!check_outputs(
            raw20_result,
            0.0f,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xE100u)},
            std::optional<std::uint32_t>{2u},
            "raw 0x20 angle normalization mismatch")) {
        return false;
    }

    CollisionInputs rawe0 = raw20;
    rawe0.angle.records[0u] = 0xE0u;
    const TerrainCollisionResult rawe0_result = make_collision(rawe0).fast_query(
        query(0.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    if (!check_outputs(
            rawe0_result,
            0.0f,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(0x1F00u)},
            std::optional<std::uint32_t>{2u},
            "raw 0xE0 angle normalization mismatch")) {
        return false;
    }

    CollisionInputs masked = raw20;
    masked.attribute.records[0u] = 0xA5u;
    const TerrainCollisionResult masked_result = make_collision(masked).fast_query(
        query(0.0f, 0.0f, 0x80u, 0u), TerrainCollisionPrecision::Single);
    if (!check_outputs(
            masked_result,
            8.0f,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(0x5A5Au)},
            std::optional<std::uint32_t>{0xDECAFBADu},
            "masked terrain changed optional outputs")) {
        return false;
    }

    CollisionInputs empty = make_inputs();
    const TerrainCollisionResult empty_result = make_collision(empty).fast_query(
        query(0.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    return check_outputs(
        empty_result,
        8.0f,
        std::optional<std::uint16_t>{static_cast<std::uint16_t>(0x5A5Au)},
        std::optional<std::uint32_t>{0xDECAFBADu},
        "empty terrain changed optional outputs");
}

bool test_layer_selection_and_chip_indirection() {
    CollisionInputs inputs = make_inputs(1u, 0u, 2u, {0u, 1u});
    write_height(inputs.height, 0u, 0u, 0u, 0u, 0x0008u);
    write_height(inputs.height, 1u, 0u, 0u, 0u, 0x0004u);
    inputs.angle.records[0u] = 0x12u;
    inputs.angle.records[64u] = 0xE0u;
    inputs.attribute.records[0u] = 0x34u;
    inputs.attribute.records[64u] = 0x22u;
    const TerrainCollision collision = make_collision(inputs);
    const TerrainCollisionResult first = collision.fast_query(
        query(0.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult second = collision.fast_query(
        query(0.0f, 0.0f, 1u, 0u), TerrainCollisionPrecision::Single);
    return check_outputs(
               first,
               0.0f,
               std::optional<std::uint16_t>{static_cast<std::uint16_t>(0x1F00u)},
               std::optional<std::uint32_t>{0x22u},
               "first layer chip indirection mismatch") &&
           check_outputs(
               second,
               1.0f,
               std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
               std::optional<std::uint32_t>{0x34u},
               "second layer selection mismatch");
}

bool test_clamp_boundaries_and_original_phase() {
    CollisionInputs clamped = make_inputs();
    clamped.bounds = TerrainCollisionBounds{8, 0, 64, 64};
    write_height(clamped.height, 0u, 1u, 0u, 0u, 0x0004u);
    clamped.angle.records[1u] = 0x12u;
    clamped.attribute.records[1u] = 0x02u;
    const TerrainCollisionResult clamped_result = make_collision(clamped).fast_query(
        query(7.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    if (!check_outputs(
            clamped_result,
            -7.0f,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
            std::optional<std::uint32_t>{2u},
            "clamped coordinate lost original phase")) {
        return false;
    }

    CollisionInputs edge = make_inputs();
    edge.bounds = TerrainCollisionBounds{5, 6, 61, 60};
    write_height(edge.height, 0u, 0u, 2u, 0u, 0x0005u);
    write_height(edge.height, 0u, 7u, 2u, 0u, 0x0005u);
    const TerrainCollision collision = make_collision(edge);
    const std::optional<std::uint16_t> direction{static_cast<std::uint16_t>(0x5A5Au)};
    const std::optional<std::uint32_t> attribute{0xDECAFBADu};
    const TerrainCollisionResult left = collision.fast_query(
        query(4.0f, 16.0f, 0x40u, 0u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult right = collision.fast_query(
        query(56.0f, 16.0f, 0x40u, 0u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult full_reject = collision.fast_query(
        query(64.0f, 16.0f, 0x40u, 0u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult top = collision.fast_query(
        query(16.0f, 5.0f, 0x40u, 2u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult bottom = collision.fast_query(
        query(16.0f, 56.0f, 0x40u, 2u), TerrainCollisionPrecision::Single);
    if (!check_outputs(left, -5.0f, direction, attribute, "left boundary mismatch") ||
        !check_outputs(right, 3.0f, direction, attribute, "right boundary mismatch") ||
        !check_outputs(full_reject, -1.0f, direction, attribute, "full-grid reject mismatch") ||
        !check_outputs(top, -6.0f, direction, attribute, "top boundary mismatch") ||
        !check_outputs(bottom, 2.0f, direction, attribute, "bottom boundary mismatch")) {
        return false;
    }

    CollisionInputs aligned = make_inputs();
    aligned.bounds = TerrainCollisionBounds{8, 0, 64, 64};
    const TerrainCollisionResult aligned_result = make_collision(aligned).fast_query(
        query(7.0f, 0.0f, 0x40u, 0u), TerrainCollisionPrecision::Single);
    return check_outputs(
        aligned_result,
        -8.0f,
        direction,
        attribute,
        "aligned left boundary mismatch");
}

bool test_constrained_fractional_sampling() {
    CollisionInputs inputs = make_inputs();
    write_height(inputs.height, 0u, 0u, 0u, 1u, 0x0004u);
    inputs.angle.records[0u] = 0x12u;
    inputs.attribute.records[0u] = 0x02u;
    const TerrainCollisionResult result = make_collision(inputs).fast_query(
        query(0.0f, 0.25f, 0x40u, 0u), TerrainCollisionPrecision::Single);
    return check_outputs(
        result,
        0.0f,
        std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
        std::optional<std::uint32_t>{2u},
        "constrained query truncated its fractional sample coordinate");
}

bool test_precision_cancellation() {
    CollisionInputs inputs = make_inputs();
    write_height(inputs.height, 0u, 0u, 0u, 0u, 0x0004u);
    inputs.angle.records[0u] = 0x12u;
    inputs.attribute.records[0u] = 0x02u;
    const TerrainCollision collision = make_collision(inputs);
    const float phase = std::ldexp(1.0f, -25);
    const TerrainCollisionResult single = collision.fast_query(
        query(phase, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    const TerrainCollisionResult doubled = collision.fast_query(
        query(phase, 0.0f, 0u, 0u), TerrainCollisionPrecision::Double);
    return check_outputs(
               single,
               0.0f,
               std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
               std::optional<std::uint32_t>{2u},
               "single precision cancellation mismatch") &&
           check_outputs(
               doubled,
               -phase,
               std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
               std::optional<std::uint32_t>{2u},
               "double precision cancellation mismatch");
}

void write_normal_sample(
    CollisionInputs& inputs,
    bool sample_x,
    std::int32_t x,
    std::int32_t y,
    std::uint8_t raw_height,
    std::uint8_t angle,
    std::uint8_t attribute) {
    const std::int32_t qx = x * 4;
    const std::int32_t qy = y * 4;
    const std::size_t block_x = static_cast<std::size_t>((qx >> 5) & 7);
    const std::size_t block_y = static_cast<std::size_t>((qy >> 5) & 7);
    const std::size_t word = sample_x
        ? static_cast<std::size_t>(qy & 31)
        : static_cast<std::size_t>(qx & 31);
    const std::uint16_t packed = sample_x
        ? static_cast<std::uint16_t>(raw_height)
        : static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw_height) << 8u);
    write_height(inputs.height, 0u, block_x, block_y, word, packed);
    const std::size_t slot =
        static_cast<std::size_t>((y >> 3) & 7) * 8u + static_cast<std::size_t>((x >> 3) & 7);
    inputs.angle.records[slot] = angle;
    inputs.attribute.records[slot] = attribute;
}

bool test_normal_ordered_probes_and_metadata() {
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 16, 0, 0x04u, 0x12u, 0x02u);
        const TerrainCollision collision = make_collision(inputs);
        const TerrainCollisionResult result = collision.normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        const TerrainCollisionResult nullopt_result = collision.normal_query(
            query(16.0f, 0.0f, 0u, 0u, std::nullopt, std::nullopt),
            TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                0.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
                std::optional<std::uint32_t>{2u},
                "normal direct terrain mismatch") ||
            !check_outputs(
                nullopt_result,
                0.0f,
                std::nullopt,
                std::nullopt,
                "normal nonzero terrain changed null optional outputs")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 24, 0, 0x04u, 0x14u, 0x04u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                8.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal forward second probe mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 24, 0, 0x20u, 0x14u, 0x04u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                7.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal forward endpoint mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 32, 0, 0x04u, 0x16u, 0x06u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                16.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEA00u)},
                std::optional<std::uint32_t>{6u},
                "normal forward third probe mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 32, 0, 0x20u, 0x16u, 0x06u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                15.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEA00u)},
                std::optional<std::uint32_t>{6u},
                "normal forward third endpoint mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                24.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0x5A5Au)},
                std::optional<std::uint32_t>{0xDECAFBADu},
                "normal all-empty outputs were not restored")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 16, 0, 0x20u, 0x12u, 0x02u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                -1.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
                std::optional<std::uint32_t>{2u},
                "normal backward empty probe did not restore first outputs")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 16, 0, 0x20u, 0x12u, 0x02u);
        write_normal_sample(inputs, true, 8, 0, 0x04u, 0x14u, 0x04u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                -8.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal backward second probe mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 16, 0, 0x20u, 0x12u, 0x02u);
        write_normal_sample(inputs, true, 8, 0, 0x20u, 0x14u, 0x04u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                -9.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal backward third empty probe did not restore second outputs")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 16, 0, 0x20u, 0x12u, 0x02u);
        write_normal_sample(inputs, true, 8, 0, 0x20u, 0x14u, 0x04u);
        write_normal_sample(inputs, true, 0, 0, 0x04u, 0x16u, 0x06u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                -16.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEA00u)},
                std::optional<std::uint32_t>{6u},
                "normal backward third probe mismatch")) {
            return false;
        }
    }
    CollisionInputs inputs = make_inputs();
    write_normal_sample(inputs, true, 16, 0, 0x20u, 0x12u, 0x02u);
    write_normal_sample(inputs, true, 8, 0, 0x20u, 0x14u, 0x04u);
    write_normal_sample(inputs, true, 0, 0, 0x20u, 0x16u, 0x06u);
    const TerrainCollisionResult result = make_collision(inputs).normal_query(
        query(16.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
    return check_outputs(
        result,
        -17.0f,
        std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEA00u)},
        std::optional<std::uint32_t>{6u},
        "normal backward third endpoint mismatch");
}

bool test_normal_axes_precision_bounds_and_domain() {
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, false, 0, 24, 0x04u, 0x14u, 0x04u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(0.0f, 16.0f, 0u, 2u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                8.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal Y-axis probe mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 8, 0, 0x04u, 0x14u, 0x04u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(16.0f, 0.0f, 0u, 1u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                16.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal negative-direction probe mismatch")) {
            return false;
        }
    }
    const float phase = std::ldexp(1.0f, -25);
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 0, 0, 0x04u, 0x12u, 0x02u);
        const TerrainCollision collision = make_collision(inputs);
        const TerrainCollisionResult single = collision.normal_query(
            query(phase, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        const TerrainCollisionResult doubled = collision.normal_query(
            query(phase, 0.0f, 0u, 0u), TerrainCollisionPrecision::Double);
        if (!check_outputs(
                single,
                0.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
                std::optional<std::uint32_t>{2u},
                "normal single precision direct mismatch") ||
            !check_outputs(
                doubled,
                -phase,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
                std::optional<std::uint32_t>{2u},
                "normal double precision direct mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 0, 0, 0x20u, 0x12u, 0x02u);
        write_normal_sample(inputs, true, 8, 0, 0x04u, 0x14u, 0x04u);
        const TerrainCollision collision = make_collision(inputs);
        const TerrainCollisionResult single = collision.normal_query(
            query(phase, 0.0f, 0u, 1u), TerrainCollisionPrecision::Single);
        const TerrainCollisionResult doubled = collision.normal_query(
            query(phase, 0.0f, 0u, 1u), TerrainCollisionPrecision::Double);
        if (!check_outputs(
                single,
                0.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal single precision offset spill mismatch") ||
            !check_outputs(
                doubled,
                0.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEC00u)},
                std::optional<std::uint32_t>{4u},
                "normal double precision offset spill mismatch")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        inputs.bounds = TerrainCollisionBounds{8, 0, 64, 64};
        write_normal_sample(inputs, true, 8, 0, 0x04u, 0x12u, 0x02u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(7.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                -7.0f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
                std::optional<std::uint32_t>{2u},
                "normal bounds clamp lost original phase")) {
            return false;
        }
    }
    {
        CollisionInputs inputs = make_inputs();
        write_normal_sample(inputs, true, 7, 0, 0x04u, 0x12u, 0x02u);
        const TerrainCollisionResult result = make_collision(inputs).normal_query(
            query(7.5f, 0.0f, 0x40u, 1u), TerrainCollisionPrecision::Single);
        if (!check_outputs(
                result,
                0.5f,
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
                std::optional<std::uint32_t>{2u},
                "normal skipped unsafe probe was rejected")) {
            return false;
        }
    }
    const TerrainCollision collision = make_collision(make_inputs());
    return expects_invalid_argument(
               [&]() {
                   collision.normal_query(
                       query(7.5f, 0.0f, 0x40u, 1u), TerrainCollisionPrecision::Single);
               },
               "normal reached unsafe probe was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.normal_query(
                       query(65521.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
               },
               "normal out-of-range starting coordinate was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.normal_query(
                       query(0.0f, 0.0f, 0u, 4u), TerrainCollisionPrecision::Single);
               },
               "normal unsupported vector was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.normal_query(
                       query(0.0f, 0.0f, 0x02u, 0u), TerrainCollisionPrecision::Single);
               },
               "normal unsupported flags were accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.normal_query(
                       query(0.0f, 0.0f, 0u, 0u),
                       static_cast<TerrainCollisionPrecision>(9));
               },
               "normal unsupported precision was accepted");
}

bool test_construction_query_and_ownership_guards() {
    CollisionInputs baseline = make_inputs();
    write_height(baseline.height, 0u, 0u, 0u, 0u, 0x0004u);
    baseline.angle.records[0u] = 0x12u;
    baseline.attribute.records[0u] = 0x02u;
    const std::vector<std::uint16_t> first_before = baseline.first.cells;
    const std::vector<std::uint8_t> height_before = baseline.height.records;
    const TerrainCollision collision = make_collision(baseline);
    if (!check(baseline.first.cells == first_before && baseline.height.records == height_before,
               "terrain collision construction mutated caller input")) {
        return false;
    }
    baseline.first.cells[0u] = 0x0001u;
    baseline.height.records[0u] = 0u;
    baseline.angle.records[0u] = 0u;
    baseline.attribute.records[0u] = 0u;
    if (!check_outputs(
            collision.fast_query(query(0.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single),
            0.0f,
            std::optional<std::uint16_t>{static_cast<std::uint16_t>(0xEE00u)},
            std::optional<std::uint32_t>{2u},
            "terrain collision retained caller storage")) {
        return false;
    }

    CollisionInputs bad_grid_size = make_inputs();
    bad_grid_size.first.cells.clear();
    CollisionInputs bad_grid_width = make_inputs();
    bad_grid_width.second.width = 2u;
    bad_grid_width.second.cells = {0u, 0u};
    CollisionInputs bad_cell_width = make_inputs();
    bad_cell_width.first.cell_bytes = 1u;
    CollisionInputs bad_kind = make_inputs();
    bad_kind.height.kind = TerrainRecordKind::Angle;
    CollisionInputs bad_count = make_inputs();
    bad_count.height.record_count = 0u;
    CollisionInputs bad_storage = make_inputs();
    bad_storage.height.records.pop_back();
    CollisionInputs bad_chip_record = make_inputs();
    bad_chip_record.height.chip_records[0u] = 1u;
    CollisionInputs bad_grid_chip = make_inputs(1u);
    CollisionInputs bad_height_byte = make_inputs();
    bad_height_byte.height.records[0u] = 0x40u;
    CollisionInputs bad_bounds = make_inputs();
    bad_bounds.bounds.left = -1;
    if (!expects_stage_data_error(
            [&]() { (void)make_collision(bad_grid_size); },
            "short terrain grid storage was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_grid_width); },
            "mismatched terrain grid dimensions were accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_cell_width); },
            "non-word terrain grid was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_kind); },
            "wrong terrain table kind was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_count); },
            "zero terrain record count was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_storage); },
            "short terrain table storage was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_chip_record); },
            "out-of-range terrain table record was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_grid_chip); },
            "unmapped terrain grid chip was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_height_byte); },
            "out-of-range terrain height byte was accepted") ||
        !expects_stage_data_error(
            [&]() { (void)make_collision(bad_bounds); },
            "invalid terrain bounds were accepted")) {
        return false;
    }

    const TerrainCollisionQuery valid_signed_zero = query(-0.0f, 0.0f, 0u, 0u);
    const TerrainCollisionResult signed_zero = collision.fast_query(
        valid_signed_zero,
        TerrainCollisionPrecision::Single);
    if (!check_float(signed_zero.distance, 0.0f, "signed zero query was rejected or changed")) {
        return false;
    }
    return expects_invalid_argument(
               [&]() {
                   collision.fast_query(query(0.0f, 0.0f, 0u, 0u),
                                        static_cast<TerrainCollisionPrecision>(9));
               },
               "unsupported terrain precision was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.fast_query(
                       query(std::numeric_limits<float>::infinity(), 0.0f, 0u, 0u),
                       TerrainCollisionPrecision::Single);
               },
               "infinite terrain coordinate was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.fast_query(
                       query(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0u, 0u),
                       TerrainCollisionPrecision::Single);
               },
               "NaN terrain coordinate was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.fast_query(query(65537.0f, 0.0f, 0u, 0u), TerrainCollisionPrecision::Single);
               },
               "out-of-range terrain coordinate was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.fast_query(query(0.0f, 0.0f, 0u, 4u), TerrainCollisionPrecision::Single);
               },
               "unsupported terrain vector was accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.fast_query(query(0.0f, 0.0f, 0x02u, 0u), TerrainCollisionPrecision::Single);
               },
               "unsupported terrain flags were accepted") &&
           expects_invalid_argument(
               [&]() {
                   collision.fast_query(query(-0.5f, 0.0f, 0x40u, 0u), TerrainCollisionPrecision::Single);
               },
               "unsafe fractional negative terrain coordinate was accepted");
}

}

int main() {
    bool passed = true;
    passed = test_all_transform_literals() && passed;
    passed = test_endpoint_direction_and_mask_outputs() && passed;
    passed = test_layer_selection_and_chip_indirection() && passed;
    passed = test_clamp_boundaries_and_original_phase() && passed;
    passed = test_constrained_fractional_sampling() && passed;
    passed = test_precision_cancellation() && passed;
    passed = test_normal_ordered_probes_and_metadata() && passed;
    passed = test_normal_axes_precision_bounds_and_domain() && passed;
    passed = test_construction_query_and_ownership_guards() && passed;
    return passed ? 0 : 1;
}
