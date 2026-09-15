#include "terrain_collision.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::uint16_t kChipMask = 0x0FFFu;
constexpr std::uint16_t kRotationMask = 0x3000u;
constexpr std::uint16_t kHorizontalFlip = 0x4000u;
constexpr std::uint16_t kVerticalFlip = 0x8000u;
constexpr std::uint16_t kBoundaryFlag = 0x0040u;
constexpr std::uint16_t kAttributeMaskFlag = 0x0080u;
constexpr std::uint16_t kSupportedFlags = 0x00C1u;
constexpr float kCoordinateLimit = 65536.0f;
constexpr float kNormalCoordinateLimit = 65520.0f;

[[noreturn]] void fail_data(const char* message) {
    throw StageDataError(message);
}

[[noreturn]] void fail_query(const char* message) {
    throw std::invalid_argument(message);
}

std::size_t checked_product(std::size_t left, std::size_t right, const char* message) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        fail_data(message);
    }
    return left * right;
}

void validate_grid(const StageGridData& grid) {
    if (grid.width == 0u || grid.height == 0u || grid.cell_bytes != 2u) {
        fail_data("terrain collision grid dimensions or cell width are invalid");
    }
    const std::size_t expected_cells = checked_product(
        static_cast<std::size_t>(grid.width),
        static_cast<std::size_t>(grid.height),
        "terrain collision grid dimensions are too large");
    if (grid.cells.size() != expected_cells) {
        fail_data("terrain collision grid storage does not match its dimensions");
    }
}

void validate_table(const TerrainTable& table, TerrainRecordKind kind) {
    if (table.kind != kind || table.record_count == 0u) {
        fail_data("terrain collision table kind or record count is invalid");
    }
    const std::size_t record_size = terrain_record_size(kind);
    const std::size_t expected_bytes = checked_product(
        static_cast<std::size_t>(table.record_count),
        record_size,
        "terrain collision table storage is too large");
    if (table.records.size() != expected_bytes) {
        fail_data("terrain collision table storage does not match its record count");
    }
    for (const std::uint16_t record : table.chip_records) {
        if (record >= table.record_count) {
            fail_data("terrain collision chip record index is out of range");
        }
    }
    if (kind == TerrainRecordKind::Height) {
        for (const std::uint8_t value : table.records) {
            if (value > 0x3Fu) {
                fail_data("terrain collision height value is out of range");
            }
        }
    }
}

void validate_grid_chips(
    const StageGridData& grid,
    const TerrainTable& height,
    const TerrainTable& angle,
    const TerrainTable& attribute) {
    for (const std::uint16_t cell : grid.cells) {
        const std::size_t chip = static_cast<std::size_t>(cell & kChipMask);
        if (chip >= height.chip_records.size() ||
            chip >= angle.chip_records.size() ||
            chip >= attribute.chip_records.size()) {
            fail_data("terrain collision grid chip is unmapped");
        }
    }
}

void validate_bounds(const TerrainCollisionBounds& bounds, const StageGridData& grid) {
    const std::int32_t width = static_cast<std::int32_t>(grid.width) * 64;
    const std::int32_t height = static_cast<std::int32_t>(grid.height) * 64;
    if (bounds.left < 0 || bounds.top < 0 ||
        bounds.left >= bounds.right_exclusive || bounds.top >= bounds.bottom_exclusive ||
        bounds.right_exclusive > width || bounds.bottom_exclusive > height) {
        fail_data("terrain collision bounds are invalid");
    }
}

void require_precision(TerrainCollisionPrecision precision) {
    if (precision != TerrainCollisionPrecision::Single &&
        precision != TerrainCollisionPrecision::Double) {
        fail_query("terrain collision precision is unsupported");
    }
}

void require_coordinate(float value) {
    if (!std::isfinite(value) || std::fabs(value) > kCoordinateLimit) {
        fail_query("terrain collision coordinate is nonfinite or out of range");
    }
}

void require_normal_start_coordinate(float value) {
    if (!std::isfinite(value) || std::fabs(value) > kNormalCoordinateLimit) {
        fail_query("terrain normal coordinate is nonfinite or out of range");
    }
}

void require_safe_constrained_coordinates(float x, float y) {
    if ((x > -1.0f && x < 0.0f) || (y > -1.0f && y < 0.0f)) {
        fail_query("terrain collision coordinate has an unsafe fractional negative value");
    }
}

struct Arithmetic {
    TerrainCollisionPrecision precision;

    double rounded(double value) const {
        if (precision == TerrainCollisionPrecision::Double || !std::isfinite(value)) {
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

struct CoordinatePair {
    std::int32_t x;
    std::int32_t y;
};

struct CellInfo {
    std::uint16_t chip;
    std::uint8_t rotation;
    bool horizontal_flip;
    bool vertical_flip;
};

struct TerrainMetadata {
    std::uint8_t angle;
    std::uint8_t attribute;
    CellInfo cell;
};

std::int32_t truncated(float value) {
    return static_cast<std::int32_t>(std::trunc(value));
}

CoordinatePair transform_pair(
    std::int32_t x,
    std::int32_t y,
    std::int32_t maximum,
    const CellInfo& cell) {
    CoordinatePair transformed{};
    switch (cell.rotation) {
    case 0u:
        transformed = {x, y};
        break;
    case 1u:
        transformed = {maximum - y, x};
        break;
    case 2u:
        transformed = {maximum - x, maximum - y};
        break;
    default:
        transformed = {y, maximum - x};
        break;
    }
    if (cell.horizontal_flip) {
        transformed.x = maximum - transformed.x;
    }
    if (cell.vertical_flip) {
        transformed.y = maximum - transformed.y;
    }
    return transformed;
}

CellInfo cell_info(std::uint16_t cell) {
    return CellInfo{
        static_cast<std::uint16_t>(cell & kChipMask),
        static_cast<std::uint8_t>((cell & kRotationMask) >> 12u),
        (cell & kHorizontalFlip) != 0u,
        (cell & kVerticalFlip) != 0u,
    };
}

CellInfo cell_at(
    const StageGridData& grid,
    std::int32_t x,
    std::int32_t y) {
    const std::size_t index =
        static_cast<std::size_t>(y >> 6) * static_cast<std::size_t>(grid.width) +
        static_cast<std::size_t>(x >> 6);
    return cell_info(grid.cells[index]);
}

std::uint8_t table_byte(
    const TerrainTable& table,
    std::uint16_t chip,
    std::size_t index) {
    const std::size_t record_size = terrain_record_size(table.kind);
    const std::size_t record = static_cast<std::size_t>(table.chip_records[chip]);
    return table.records[record * record_size + index];
}

std::uint16_t read_height_word(
    const TerrainTable& table,
    std::uint16_t chip,
    std::size_t index) {
    const std::uint8_t low = table_byte(table, chip, index);
    const std::uint8_t high = table_byte(table, chip, index + 1u);
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(low) |
        (static_cast<std::uint16_t>(high) << 8u));
}

std::uint8_t flip6(std::uint8_t value) {
    return (value & 0x1Fu) != 0u
        ? static_cast<std::uint8_t>((static_cast<unsigned int>(value) - 0x20u) & 0x3Fu)
        : value;
}

float decode_height(std::uint8_t value, const Arithmetic& arithmetic) {
    std::int32_t signed_value = static_cast<std::int32_t>(value);
    if ((signed_value & 0x20) != 0) {
        signed_value -= 0x40;
    }
    if (signed_value == -32) {
        signed_value = 32;
    }
    return arithmetic.spill(arithmetic.multiply(static_cast<double>(signed_value), 0.25));
}

float sample_raw_height(
    const StageGridData& grid,
    const TerrainTable& table,
    float x,
    float y,
    bool sample_x,
    const Arithmetic& arithmetic) {
    const std::int32_t qx = truncated(arithmetic.spill(
        arithmetic.multiply(static_cast<double>(x), 4.0)));
    const std::int32_t qy = truncated(arithmetic.spill(
        arithmetic.multiply(static_cast<double>(y), 4.0)));
    const std::size_t cell_index =
        static_cast<std::size_t>(qy >> 8) * static_cast<std::size_t>(grid.width) +
        static_cast<std::size_t>(qx >> 8);
    const CellInfo cell = cell_info(grid.cells[cell_index]);
    const CoordinatePair block = transform_pair(
        (qx >> 5) & 7,
        (qy >> 5) & 7,
        7,
        cell);
    const CoordinatePair inner = transform_pair(qx & 31, qy & 31, 31, cell);
    const std::size_t word = sample_x
        ? (cell.rotation & 1u) != 0u ? static_cast<std::size_t>(inner.x)
                                     : static_cast<std::size_t>(inner.y)
        : (cell.rotation & 1u) != 0u ? static_cast<std::size_t>(inner.y)
                                     : static_cast<std::size_t>(inner.x);
    const std::size_t offset =
        (static_cast<std::size_t>(block.y) * 8u + static_cast<std::size_t>(block.x)) * 64u +
        word * 2u;
    const std::uint16_t packed = read_height_word(table, cell.chip, offset);
    std::uint8_t low = static_cast<std::uint8_t>(packed & 0x00FFu);
    std::uint8_t high = static_cast<std::uint8_t>((packed >> 8u) & 0x00FFu);
    if (cell.horizontal_flip) {
        low = flip6(low);
    }
    if (cell.vertical_flip) {
        high = flip6(high);
    }

    std::uint8_t selected = 0u;
    switch (cell.rotation) {
    case 0u:
        selected = sample_x ? low : high;
        break;
    case 1u:
        selected = sample_x ? high : flip6(low);
        break;
    case 2u:
        selected = sample_x ? flip6(low) : flip6(high);
        break;
    default:
        selected = sample_x ? flip6(high) : low;
        break;
    }
    return decode_height(selected, arithmetic);
}

TerrainMetadata sample_metadata(
    const StageGridData& grid,
    const TerrainTable& angle_table,
    const TerrainTable& attribute_table,
    float x,
    float y) {
    const std::int32_t ix = truncated(x);
    const std::int32_t iy = truncated(y);
    const CellInfo cell = cell_at(grid, ix, iy);
    const CoordinatePair slot = transform_pair((ix >> 3) & 7, (iy >> 3) & 7, 7, cell);
    const std::size_t index =
        static_cast<std::size_t>(slot.y) * 8u + static_cast<std::size_t>(slot.x);
    return TerrainMetadata{
        table_byte(angle_table, cell.chip, index),
        table_byte(attribute_table, cell.chip, index),
        cell,
    };
}

struct RawTerrainSample {
    float height;
    std::optional<TerrainMetadata> metadata;
};

std::int32_t boundary_candidate(
    std::int32_t coordinate,
    std::int32_t lower_bound,
    std::int32_t upper_bound);

RawTerrainSample sample_raw_terrain(
    const StageGridData& grid,
    const TerrainTable& height_table,
    const TerrainTable& angle_table,
    const TerrainTable& attribute_table,
    const TerrainCollisionBounds& bounds,
    float x,
    float y,
    std::uint16_t flags,
    bool sample_x,
    const Arithmetic& arithmetic) {
    float sample_x_coordinate = x;
    float sample_y_coordinate = y;
    std::int32_t boundary = 0;
    if ((flags & kBoundaryFlag) != 0u) {
        const std::int32_t truncated_x = truncated(x);
        const std::int32_t truncated_y = truncated(y);
        const std::int32_t width = static_cast<std::int32_t>(grid.width) * 64;
        const std::int32_t height = static_cast<std::int32_t>(grid.height) * 64;
        if (truncated_x < 0 || truncated_x >= width ||
            truncated_y < 0 || truncated_y >= height) {
            return RawTerrainSample{8.0f, std::nullopt};
        }
        boundary = sample_x
            ? boundary_candidate(truncated_x, bounds.left, bounds.right_exclusive - 1)
            : boundary_candidate(truncated_y, bounds.top, bounds.bottom_exclusive - 1);
        if (!sample_x && boundary != 0) {
            return RawTerrainSample{static_cast<float>(boundary), std::nullopt};
        }
    } else {
        sample_x_coordinate = std::clamp(
            sample_x_coordinate,
            static_cast<float>(bounds.left),
            static_cast<float>(bounds.right_exclusive - 1));
        sample_y_coordinate = std::clamp(
            sample_y_coordinate,
            static_cast<float>(bounds.top),
            static_cast<float>(bounds.bottom_exclusive - 1));
    }

    float height = sample_raw_height(
        grid,
        height_table,
        sample_x_coordinate,
        sample_y_coordinate,
        sample_x,
        arithmetic);
    const TerrainMetadata metadata = sample_metadata(
        grid,
        angle_table,
        attribute_table,
        sample_x_coordinate,
        sample_y_coordinate);
    if ((flags & kAttributeMaskFlag) != 0u && (metadata.attribute & 0x01u) != 0u) {
        height = 0.0f;
    }
    if (sample_x && boundary != 0 && std::fabs(height) < std::fabs(static_cast<float>(boundary))) {
        return RawTerrainSample{static_cast<float>(boundary), std::nullopt};
    }
    return RawTerrainSample{height, metadata};
}

std::uint16_t transform_direction(std::uint8_t raw, const CellInfo& cell) {
    std::uint16_t value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw) << 8u);
    if (cell.horizontal_flip) {
        value = static_cast<std::uint16_t>(0u - value);
    }
    if (cell.vertical_flip) {
        value = static_cast<std::uint16_t>(0x8000u - value);
    }
    value = static_cast<std::uint16_t>(0u - value);
    value = static_cast<std::uint16_t>(
        value - static_cast<std::uint16_t>(static_cast<std::uint16_t>(cell.rotation) << 14u));
    if (((value ^ 0x2000u) & 0x3FFFu) == 0u) {
        value = static_cast<std::uint16_t>(
            value + ((value & 0x4000u) != 0u ? 0x0100u : 0xFF00u));
    }
    return value;
}

std::int32_t boundary_candidate(
    std::int32_t coordinate,
    std::int32_t lower_bound,
    std::int32_t upper_bound) {
    const std::int32_t tile = coordinate & ~std::int32_t{7};
    if (tile > upper_bound || coordinate < lower_bound - 7) {
        return 8;
    }
    if (tile + 8 > upper_bound) {
        const std::int32_t remainder = upper_bound & 7;
        return remainder != 0 ? remainder : 8;
    }
    if (tile < lower_bound) {
        const std::int32_t remainder = lower_bound & 7;
        return remainder != 0 ? -remainder : 8;
    }
    return 0;
}

float phase_for_axis(float axis, const Arithmetic& arithmetic) {
    double integer = 0.0;
    const float fraction = arithmetic.spill(std::modf(static_cast<double>(axis), &integer));
    const std::uint32_t cycle = static_cast<std::uint32_t>(truncated(axis)) & 7u;
    return arithmetic.spill(arithmetic.add(
        static_cast<double>(fraction),
        static_cast<double>(cycle)));
}

float query_distance(
    float height,
    float phase,
    bool positive,
    const Arithmetic& arithmetic) {
    const double phase_plus_one = arithmetic.add(static_cast<double>(phase), 1.0);
    if (height == 0.0f) {
        return positive
            ? arithmetic.spill(arithmetic.subtract(8.0, static_cast<double>(phase)))
            : arithmetic.spill(phase_plus_one);
    }
    if (height == 8.0f) {
        return positive
            ? arithmetic.spill(arithmetic.subtract(0.0, phase_plus_one))
            : arithmetic.spill(arithmetic.subtract(static_cast<double>(phase), 8.0));
    }
    if (height > 0.0f) {
        return positive
            ? arithmetic.spill(arithmetic.subtract(
                  static_cast<double>(height), phase_plus_one))
            : arithmetic.spill(arithmetic.subtract(8.0, static_cast<double>(phase)));
    }
    return positive
        ? arithmetic.spill(arithmetic.subtract(0.0, phase_plus_one))
        : arithmetic.spill(arithmetic.add(static_cast<double>(height), static_cast<double>(phase)));
}

void update_outputs(
    TerrainCollisionResult& result,
    const TerrainCollisionQuery& query,
    const TerrainMetadata& metadata) {
    if (query.direction.has_value()) {
        result.direction = transform_direction(metadata.angle, metadata.cell);
    }
    if (query.attribute.has_value()) {
        result.attribute = static_cast<std::uint32_t>(metadata.attribute);
    }
}

float normal_diff(
    float height,
    float phase,
    bool positive,
    const Arithmetic& arithmetic) {
    const double phase_plus_one = arithmetic.add(static_cast<double>(phase), 1.0);
    if (height > 0.0f) {
        return positive
            ? arithmetic.spill(arithmetic.subtract(static_cast<double>(height), phase_plus_one))
            : arithmetic.spill(arithmetic.subtract(8.0, static_cast<double>(phase)));
    }
    return positive
        ? arithmetic.spill(arithmetic.subtract(0.0, phase_plus_one))
        : arithmetic.spill(arithmetic.add(static_cast<double>(height), static_cast<double>(phase)));
}

float normal_forward(float phase, bool positive, const Arithmetic& arithmetic) {
    if (positive) {
        return arithmetic.spill(arithmetic.subtract(8.0, static_cast<double>(phase)));
    }
    return arithmetic.spill(arithmetic.add(static_cast<double>(phase), 1.0));
}

float normal_back(float phase, bool positive, const Arithmetic& arithmetic) {
    const double phase_plus_one = arithmetic.add(static_cast<double>(phase), 1.0);
    return positive
        ? arithmetic.spill(arithmetic.subtract(0.0, phase_plus_one))
        : arithmetic.spill(arithmetic.subtract(static_cast<double>(phase), 8.0));
}

float normal_forward_reverse(float phase, bool positive, const Arithmetic& arithmetic) {
    if (!positive) {
        return arithmetic.spill(static_cast<double>(phase));
    }
    const double phase_plus_one = arithmetic.add(static_cast<double>(phase), 1.0);
    return arithmetic.spill(arithmetic.subtract(8.0, phase_plus_one));
}

float normal_offset(float value, float offset, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.add(static_cast<double>(value), static_cast<double>(offset)));
}

struct ProbeCoordinates {
    float x;
    float y;
};

ProbeCoordinates probe_coordinates(
    float x,
    float y,
    bool sample_x,
    float offset,
    const Arithmetic& arithmetic) {
    return ProbeCoordinates{
        arithmetic.spill(arithmetic.add(
            static_cast<double>(x), sample_x ? static_cast<double>(offset) : 0.0)),
        arithmetic.spill(arithmetic.add(
            static_cast<double>(y), sample_x ? 0.0 : static_cast<double>(offset))),
    };
}

}

TerrainCollision::TerrainCollision(
    StageGridData first_grid,
    StageGridData second_grid,
    TerrainTable height_table,
    TerrainTable angle_table,
    TerrainTable attribute_table,
    TerrainCollisionBounds bounds)
    : first_grid_(std::move(first_grid)),
      second_grid_(std::move(second_grid)),
      height_table_(std::move(height_table)),
      angle_table_(std::move(angle_table)),
      attribute_table_(std::move(attribute_table)),
      bounds_(bounds) {
    validate_grid(first_grid_);
    validate_grid(second_grid_);
    if (first_grid_.width != second_grid_.width || first_grid_.height != second_grid_.height) {
        fail_data("terrain collision grid dimensions do not match");
    }
    validate_table(height_table_, TerrainRecordKind::Height);
    validate_table(angle_table_, TerrainRecordKind::Angle);
    validate_table(attribute_table_, TerrainRecordKind::Attribute);
    validate_grid_chips(first_grid_, height_table_, angle_table_, attribute_table_);
    validate_grid_chips(second_grid_, height_table_, angle_table_, attribute_table_);
    validate_bounds(bounds_, first_grid_);
}

TerrainCollisionResult TerrainCollision::fast_query(
    const TerrainCollisionQuery& query,
    TerrainCollisionPrecision precision) const {
    require_precision(precision);
    require_coordinate(query.x);
    require_coordinate(query.y);
    if (query.vector > 3u) {
        fail_query("terrain collision vector is unsupported");
    }
    if ((query.flags & static_cast<std::uint16_t>(~kSupportedFlags)) != 0u) {
        fail_query("terrain collision flags are unsupported");
    }

    const bool constrained = (query.flags & kBoundaryFlag) != 0u;
    if (constrained) {
        require_safe_constrained_coordinates(query.x, query.y);
    }

    const Arithmetic arithmetic{precision};
    const bool sample_x = (query.vector & 0x02u) == 0u;
    const bool positive = (query.vector & 0x01u) == 0u;
    const float axis = sample_x ? query.x : query.y;
    const float phase = phase_for_axis(axis, arithmetic);
    TerrainCollisionResult result{0.0f, query.direction, query.attribute};
    const StageGridData& grid = (query.flags & 0x0001u) != 0u ? second_grid_ : first_grid_;

    const RawTerrainSample sample = sample_raw_terrain(
        grid,
        height_table_,
        angle_table_,
        attribute_table_,
        bounds_,
        query.x,
        query.y,
        query.flags,
        sample_x,
        arithmetic);
    if (sample.height != 0.0f && sample.metadata.has_value()) {
        update_outputs(result, query, *sample.metadata);
    }
    result.distance = query_distance(sample.height, phase, positive, arithmetic);
    return result;
}

TerrainCollisionResult TerrainCollision::normal_query(
    const TerrainCollisionQuery& query,
    TerrainCollisionPrecision precision) const {
    require_precision(precision);
    require_normal_start_coordinate(query.x);
    require_normal_start_coordinate(query.y);
    if (query.vector > 3u) {
        fail_query("terrain normal vector is unsupported");
    }
    if ((query.flags & static_cast<std::uint16_t>(~kSupportedFlags)) != 0u) {
        fail_query("terrain normal flags are unsupported");
    }

    const Arithmetic arithmetic{precision};
    const bool sample_x = (query.vector & 0x02u) == 0u;
    const bool positive = (query.vector & 0x01u) == 0u;
    const bool constrained = (query.flags & kBoundaryFlag) != 0u;
    const float phase = phase_for_axis(sample_x ? query.x : query.y, arithmetic);
    const StageGridData& grid = (query.flags & 0x0001u) != 0u ? second_grid_ : first_grid_;
    const TerrainCollisionResult input_result{0.0f, query.direction, query.attribute};
    TerrainCollisionResult result = input_result;

    const auto sample = [&](const ProbeCoordinates& coordinates) {
        require_coordinate(coordinates.x);
        require_coordinate(coordinates.y);
        if (constrained) {
            require_safe_constrained_coordinates(coordinates.x, coordinates.y);
        }
        const RawTerrainSample terrain = sample_raw_terrain(
            grid,
            height_table_,
            angle_table_,
            attribute_table_,
            bounds_,
            coordinates.x,
            coordinates.y,
            query.flags,
            sample_x,
            arithmetic);
        if (terrain.height != 0.0f && terrain.metadata.has_value()) {
            update_outputs(result, query, *terrain.metadata);
        }
        return terrain.height;
    };

    const float first = sample(ProbeCoordinates{query.x, query.y});
    if (first == 0.0f) {
        const ProbeCoordinates second_coordinates = probe_coordinates(
            query.x,
            query.y,
            sample_x,
            positive ? 8.0f : -8.0f,
            arithmetic);
        const float second = sample(second_coordinates);
        if (second != 0.0f) {
            result.distance = second == 8.0f
                ? normal_offset(normal_back(phase, positive, arithmetic), 8.0f, arithmetic)
                : normal_offset(normal_diff(second, phase, positive, arithmetic), 8.0f, arithmetic);
            return result;
        }

        const ProbeCoordinates third_coordinates = probe_coordinates(
            query.x,
            query.y,
            sample_x,
            positive ? 16.0f : -16.0f,
            arithmetic);
        const float third = sample(third_coordinates);
        if (third == 0.0f) {
            result = input_result;
            result.distance = normal_offset(normal_forward(phase, positive, arithmetic), 16.0f, arithmetic);
            return result;
        }
        result.distance = third == 8.0f
            ? normal_offset(normal_back(phase, positive, arithmetic), 16.0f, arithmetic)
            : normal_offset(normal_diff(third, phase, positive, arithmetic), 16.0f, arithmetic);
        return result;
    }
    if (first != 8.0f) {
        result.distance = normal_diff(first, phase, positive, arithmetic);
        return result;
    }

    const TerrainCollisionResult first_result = result;
    const ProbeCoordinates second_coordinates = probe_coordinates(
        query.x,
        query.y,
        sample_x,
        positive ? -8.0f : 8.0f,
        arithmetic);
    const float second = sample(second_coordinates);
    if (second == 0.0f) {
        result = first_result;
        result.distance = normal_offset(
            normal_forward_reverse(phase, positive, arithmetic), -8.0f, arithmetic);
        return result;
    }
    if (second != 8.0f) {
        result.distance = normal_offset(normal_diff(second, phase, positive, arithmetic), -8.0f, arithmetic);
        return result;
    }

    const TerrainCollisionResult second_result = result;
    const ProbeCoordinates third_coordinates = probe_coordinates(
        query.x,
        query.y,
        sample_x,
        positive ? -16.0f : 16.0f,
        arithmetic);
    const float third = sample(third_coordinates);
    if (third == 0.0f) {
        result = second_result;
        result.distance = normal_offset(
            normal_forward_reverse(phase, positive, arithmetic), -16.0f, arithmetic);
        return result;
    }
    result.distance = third == 8.0f
        ? normal_offset(normal_back(phase, positive, arithmetic), -16.0f, arithmetic)
        : normal_offset(normal_diff(third, phase, positive, arithmetic), -16.0f, arithmetic);
    return result;
}
