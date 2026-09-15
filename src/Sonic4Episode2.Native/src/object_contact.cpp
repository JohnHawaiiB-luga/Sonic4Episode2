#include "object_contact.h"
#include "nn_trig.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace {

constexpr std::uint32_t kMoveGrounded = 0x00000001u;
constexpr std::uint32_t kMoveSecondaryBlocked = 0x00000002u;
constexpr std::uint32_t kMoveFirstWidthBlocked = 0x00000004u;
constexpr std::uint32_t kMoveSecondWidthBlocked = 0x00000008u;
constexpr std::uint32_t kMoveFixedOrientation = 0x00000010u;
constexpr std::uint32_t kMoveSkipClearanceProbe = 0x00000020u;
constexpr std::uint32_t kMoveUseSurfaceDirection = 0x00000040u;
constexpr std::uint32_t kMoveSkipWidth = 0x00000400u;
constexpr std::uint32_t kMoveSkipHeight = 0x00000800u;
constexpr std::uint32_t kMoveTerrainDisabled = 0x00001000u;
constexpr std::uint32_t kMovePreserveVelocity = 0x00004000u;
constexpr std::uint32_t kMoveOneUnitContact = 0x00010000u;
constexpr std::uint32_t kMoveBoundaryTerrain = 0x00080000u;
constexpr std::uint32_t kMoveSurfaceClearance = 0x00100000u;
constexpr std::uint32_t kMoveClearanceBypass = 0x00400000u;
constexpr std::uint32_t kMoveSmoothOrientation = 0x00800000u;
constexpr std::uint32_t kMoveHoldOrientation = 0x10000000u;
constexpr std::uint32_t kMoveProtectVelocity = 0x20000000u;
constexpr std::uint32_t kMoveExtendedCorrection = 0x40000000u;
constexpr std::uint16_t kTerrainLayerFlag = 0x0001u;
constexpr std::uint16_t kTerrainBoundaryFlag = 0x0040u;
constexpr std::uint16_t kTerrainMaskFlag = 0x0080u;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(TerrainCollisionPrecision precision) {
    if (precision != TerrainCollisionPrecision::Single &&
        precision != TerrainCollisionPrecision::Double) {
        fail("object contact precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > 16777216.0f) {
        fail(message);
    }
}

void validate_state(const ObjectContactState& state) {
    require_finite(state.position_x, "object contact X position is nonfinite");
    require_finite(state.position_y, "object contact Y position is nonfinite");
    require_finite(state.velocity_x, "object contact X velocity is nonfinite");
    require_finite(state.velocity_y, "object contact Y velocity is nonfinite");
    require_finite(state.movement_x, "object contact X movement is nonfinite");
    require_finite(state.movement_y, "object contact Y movement is nonfinite");
    require_finite(state.ground_speed, "object contact ground speed is nonfinite");
    if (state.field.left >= state.field.right || state.field.top >= state.field.bottom) {
        fail("object contact field is invalid");
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

    float spill(double value) const {
        return static_cast<float>(value);
    }

    float add(float left, float right) const {
        return spill(rounded(static_cast<double>(left) + static_cast<double>(right)));
    }

    float subtract(float left, float right) const {
        return spill(rounded(static_cast<double>(left) - static_cast<double>(right)));
    }
};

struct ContactPosition {
    float x;
    float y;
};

bool has_move_flag(const ObjectContactState& state, std::uint32_t flag) {
    return (state.move_flags & flag) != 0u;
}

void set_move_flag(ObjectContactState& state, std::uint32_t flag) {
    state.move_flags |= flag;
}

void clear_move_flag(ObjectContactState& state, std::uint32_t flag) {
    state.move_flags &= ~flag;
}

std::uint8_t angle_quadrant(std::uint16_t angle) {
    const std::uint16_t shifted = static_cast<std::uint16_t>(angle + 0x2000u);
    return static_cast<std::uint8_t>((shifted & 0xc000u) >> 14u);
}

float truncate_toward_zero(float value, const Arithmetic& arithmetic) {
    if (value < 0.0f) {
        return -arithmetic.spill(std::floor(static_cast<double>(-value)));
    }
    return arithmetic.spill(std::floor(static_cast<double>(value)));
}

float add_offset(float value, int offset, const Arithmetic& arithmetic) {
    return arithmetic.add(value, static_cast<float>(offset));
}

std::uint16_t terrain_flags(const ObjectContactState& state) {
    std::uint16_t flags = state.surface_clearance && !has_move_flag(state, kMoveSkipClearanceProbe)
        ? 0u
        : kTerrainMaskFlag;
    if (state.use_secondary_terrain) {
        flags = static_cast<std::uint16_t>(flags | kTerrainLayerFlag);
    }
    if (has_move_flag(state, kMoveBoundaryTerrain)) {
        flags = static_cast<std::uint16_t>(flags | kTerrainBoundaryFlag);
    }
    return flags;
}

float normal_distance(
    const TerrainCollision& terrain,
    float x,
    float y,
    std::uint16_t flags,
    std::uint8_t vector,
    std::uint16_t* direction,
    std::uint32_t* attribute,
    TerrainCollisionPrecision precision) {
    const std::optional<std::uint16_t> direction_input = direction == nullptr
        ? std::nullopt
        : std::optional<std::uint16_t>{*direction};
    const std::optional<std::uint32_t> attribute_input = attribute == nullptr
        ? std::nullopt
        : std::optional<std::uint32_t>{*attribute};
    const TerrainCollisionResult result = terrain.normal_query(
        TerrainCollisionQuery{x, y, flags, vector, direction_input, attribute_input},
        precision);
    if (direction != nullptr && result.direction.has_value()) {
        *direction = *result.direction;
    }
    if (attribute != nullptr && result.attribute.has_value()) {
        *attribute = *result.attribute;
    }
    return result.distance;
}

float minimum(float first, float second) {
    return first < second ? first : second;
}

void apply_attribute(ObjectContactState& state) {
    if ((state.metadata.attribute & 0x02u) != 0u) {
        state.collision_flags |= 0x01u;
    }
    if ((state.metadata.attribute & 0x04u) != 0u) {
        state.collision_flags |= 0x04u;
    }
    if ((state.metadata.attribute & 0x08u) != 0u) state.collision_flags |= 0x10u;
    if ((state.metadata.attribute & 0x10u) != 0u) state.collision_flags |= 0x20u;
    if ((state.metadata.attribute & 0x01u) != 0u) state.collision_flags |= 0x02u;
    if ((state.metadata.attribute & 0x20u) != 0u) state.collision_flags |= 0x40u;
}

void move_position(
    ContactPosition& position,
    float distance,
    std::uint8_t vector,
    const Arithmetic& arithmetic) {
    switch (vector) {
    case 0u:
        position.x = arithmetic.add(position.x, distance);
        break;
    case 1u:
        position.x = arithmetic.subtract(position.x, distance);
        break;
    case 2u:
        position.y = arithmetic.add(position.y, distance);
        break;
    default:
        position.y = arithmetic.subtract(position.y, distance);
        break;
    }
}

struct ProbePair {
    int primary_x;
    int primary_y;
    int secondary_x;
    int secondary_y;
    std::uint8_t vector;
};

struct WidthPlan {
    std::uint8_t quadrant;
    bool inverted;
};

struct HeightGeometry {
    ProbePair support;
    ProbePair clearance;
    ProbePair opposite;
    float normal_motion;
    std::uint8_t support_quadrant;
};

std::uint8_t support_quadrant(const ObjectContactState& state) {
    return has_move_flag(state, kMoveFixedOrientation) ? 0u : angle_quadrant(state.orientation);
}

float pair_distance(
    const TerrainCollision& terrain,
    const ContactPosition& position,
    std::uint16_t flags,
    const ProbePair& probes,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    const float primary = normal_distance(
        terrain,
        add_offset(position.x, probes.primary_x, arithmetic),
        add_offset(position.y, probes.primary_y, arithmetic),
        flags,
        probes.vector,
        nullptr,
        nullptr,
        precision);
    const float secondary = normal_distance(
        terrain,
        add_offset(position.x, probes.secondary_x, arithmetic),
        add_offset(position.y, probes.secondary_y, arithmetic),
        flags,
        probes.vector,
        nullptr,
        nullptr,
        precision);
    return minimum(primary, secondary);
}

int midpoint(int first, int second) {
    return first >= second
        ? second + (std::abs(first) + std::abs(second)) / 2
        : first + (std::abs(first) + std::abs(second)) / 2;
}

ProbePair simple_over_probes(const ObjectContactState& state) {
    const int left = static_cast<int>(state.field.left);
    const int top = static_cast<int>(state.field.top);
    const int right = static_cast<int>(state.field.right);
    switch (support_quadrant(state)) {
    case 1u:
        return {-top, left + 2, -top, right - 2, 0u};
    case 2u:
        return {right - 2, -top, left + 2, -top, 2u};
    case 3u:
        return {top, left + 2, top, right - 2, 1u};
    default:
        return {right - 2, top, left + 2, top, 3u};
    }
}

WidthPlan width_plan(const ObjectContactState& state, bool second_width_pass) {
    const std::uint8_t visual_quadrant = angle_quadrant(state.orientation);
    std::uint16_t direction = 0x4000u;
    if (visual_quadrant == 2u) {
        direction = static_cast<std::uint16_t>(direction + 0x8000u);
    }
    bool inverted = !state.flipped_horizontal;
    if (inverted) {
        direction = static_cast<std::uint16_t>(0u - direction);
    }
    if (visual_quadrant == 2u) {
        inverted = !inverted;
    }
    if (!has_move_flag(state, kMoveFixedOrientation) ||
        (second_width_pass && has_move_flag(state, kMoveGrounded))) {
        direction = static_cast<std::uint16_t>(direction + state.orientation);
    }
    return {angle_quadrant(direction), inverted};
}

ProbePair width_probes(
    const ObjectContactState& state,
    const WidthPlan& plan,
    int depth,
    bool opposite) {
    const int left = static_cast<int>(state.field.left);
    const int top = static_cast<int>(state.field.top);
    const int right = static_cast<int>(state.field.right);
    const int bottom = static_cast<int>(state.field.bottom);
    int primary_x = 0;
    int primary_y = 0;
    int secondary_x = 0;
    int secondary_y = 0;
    std::uint8_t vector = 0u;
    const std::uint8_t quadrant = static_cast<std::uint8_t>(
        opposite ? (plan.quadrant + 2u) & 3u : plan.quadrant);
    switch (quadrant) {
    case 0u:
        primary_x = bottom - static_cast<int>(state.adjustments.width_down_back);
        secondary_x = top + depth;
        if (opposite) {
            primary_x = -primary_x;
            secondary_x = -secondary_x;
        }
        primary_y = secondary_y = right + static_cast<int>(state.adjustments.width_down_front);
        vector = 2u;
        if (plan.inverted) {
            primary_x = -primary_x;
            secondary_x = -secondary_x;
        }
        break;
    case 1u:
        primary_x = secondary_x = left - static_cast<int>(state.adjustments.width_left_front);
        primary_y = bottom - static_cast<int>(state.adjustments.width_left_back);
        secondary_y = top + depth;
        if (opposite) {
            primary_y = -primary_y;
            secondary_y = -secondary_y;
        }
        vector = 1u;
        if (plan.inverted) {
            primary_y = -primary_y;
            secondary_y = -secondary_y;
        }
        break;
    case 2u:
        primary_x = bottom - static_cast<int>(state.adjustments.width_up_back);
        secondary_x = top + depth;
        if (!opposite) {
            primary_x = -primary_x;
            secondary_x = -secondary_x;
        }
        primary_y = secondary_y = left - static_cast<int>(state.adjustments.width_up_front);
        vector = 3u;
        if (plan.inverted) {
            primary_x = -primary_x;
            secondary_x = -secondary_x;
        }
        break;
    default:
        primary_x = secondary_x = right + static_cast<int>(state.adjustments.width_right_front);
        primary_y = bottom - static_cast<int>(state.adjustments.width_right_back);
        secondary_y = top + depth;
        if (!opposite) {
            primary_y = -primary_y;
            secondary_y = -secondary_y;
        }
        vector = 0u;
        if (plan.inverted) {
            primary_y = -primary_y;
            secondary_y = -secondary_y;
        }
        break;
    }
    return {primary_x, primary_y, secondary_x, secondary_y, vector};
}

HeightGeometry height_geometry(
    const ObjectContactState& state,
    const ObjectContactEnvironment& environment) {
    const int left = static_cast<int>(state.field.left);
    const int top = static_cast<int>(state.field.top);
    const int right = static_cast<int>(state.field.right);
    const int bottom = static_cast<int>(state.field.bottom);
    switch (support_quadrant(state)) {
    case 1u: {
        const ProbePair support{
            -bottom,
            left - static_cast<int>(state.adjustments.height_left_left),
            -bottom,
            right + static_cast<int>(state.adjustments.height_left_right),
            1u,
        };
        const int clearance_x = bottom - std::abs(right);
        return {
            support,
            {support.primary_x + clearance_x, support.primary_y,
                support.secondary_x + clearance_x, support.secondary_y, support.vector},
            {-top - 2, support.primary_y, -top - 2, support.secondary_y, 0u},
            -state.movement_x,
            1u,
        };
    }
    case 2u: {
        const ProbePair support{
            right + static_cast<int>(state.adjustments.height_up_right),
            -bottom,
            left - static_cast<int>(state.adjustments.height_up_left),
            -bottom,
            3u,
        };
        return {
            support,
            {support.primary_x, support.primary_y + static_cast<int>(environment.through_offset),
                support.secondary_x, support.secondary_y + static_cast<int>(environment.through_offset), support.vector},
            {support.primary_x, -top - 2, support.secondary_x, -top - 2, 2u},
            -state.movement_y,
            2u,
        };
    }
    case 3u: {
        const ProbePair support{
            bottom,
            left - static_cast<int>(state.adjustments.height_right_left),
            bottom,
            right + static_cast<int>(state.adjustments.height_right_right),
            0u,
        };
        const int clearance_x = -(bottom - std::abs(left));
        return {
            support,
            {support.primary_x + clearance_x, support.primary_y,
                support.secondary_x + clearance_x, support.secondary_y, support.vector},
            {top + 2, support.primary_y, top + 2, support.secondary_y, 1u},
            state.movement_x,
            3u,
        };
    }
    default: {
        const ProbePair support{
            right + static_cast<int>(state.adjustments.height_down_right),
            bottom,
            left - static_cast<int>(state.adjustments.height_down_left),
            bottom,
            2u,
        };
        return {
            support,
            {support.primary_x, support.primary_y - static_cast<int>(environment.through_offset),
                support.secondary_x, support.secondary_y - static_cast<int>(environment.through_offset), support.vector},
            {support.primary_x, top + 2, support.secondary_x, top + 2, 3u},
            state.movement_y,
            0u,
        };
    }
    }
}

float simple_over(
    const ObjectContactState& state,
    const TerrainCollision& terrain,
    const ContactPosition& position,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    const std::uint16_t flags = terrain_flags(state);
    const float result = pair_distance(
        terrain,
        position,
        flags,
        simple_over_probes(state),
        arithmetic,
        precision);
    return result <= 0.0f ? result : 0.0f;
}

int width_depth(float clearance, const ObjectContactField& field) {
    int depth = clearance <= -4.0f ? -static_cast<int>(clearance) + 1 : 4;
    const int height = static_cast<int>(field.bottom) - static_cast<int>(field.top);
    if (depth >= height) {
        depth = height - 1;
    }
    return depth;
}

float width_distance(
    const ObjectContactState& state,
    const TerrainCollision& terrain,
    const ContactPosition& position,
    const ProbePair& probes,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    const std::uint16_t flags = static_cast<std::uint16_t>(terrain_flags(state) | kTerrainMaskFlag);
    return pair_distance(terrain, position, flags, probes, arithmetic, precision);
}

void resolve_first_width(
    ObjectContactState& state,
    const TerrainCollision& terrain,
    ContactPosition& position,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    const int depth = width_depth(simple_over(state, terrain, position, arithmetic, precision), state.field);
    const WidthPlan plan = width_plan(state, false);
    for (unsigned wall = 0u; wall < 2u; ++wall) {
        const ProbePair probes = width_probes(state, plan, depth, wall != 0u);
        const float distance = width_distance(state, terrain, position, probes, arithmetic, precision);
        if (distance <= 0.0f) {
            move_position(position, distance, probes.vector, arithmetic);
        }
    }
}

void stop_for_width(
    ObjectContactState& state,
    const ObjectContactEnvironment& environment,
    std::uint8_t vector,
    bool first_width_probe) {
    if (has_move_flag(state, kMovePreserveVelocity)) {
        return;
    }
    if (!first_width_probe) {
        if ((vector == 0u && state.ground_speed > 0.0f) ||
            (vector == 1u && state.ground_speed < 0.0f)) {
            state.ground_speed = 0.0f;
        }
        if (!has_move_flag(state, kMoveProtectVelocity) &&
            ((vector == 0u && state.velocity_x > 0.0f) ||
             (vector == 1u && state.velocity_x < 0.0f))) {
            state.velocity_x = 0.0f;
        }
        return;
    }
    if ((vector == 0u && state.movement_x > 0.0f) ||
        (vector == 1u && state.movement_x < 0.0f)) {
        state.ground_speed = 0.0f;
        if (!has_move_flag(state, kMoveProtectVelocity)) state.velocity_x = 0.0f;
    }
    if (first_width_probe && (vector & 2u) != 0u && environment.normal_stage_vertical_width_ground_stop) {
        state.ground_speed = 0.0f;
    }
}

void resolve_second_width(
    ObjectContactState& state,
    const ObjectContactEnvironment& environment,
    const TerrainCollision& terrain,
    const ContactPosition& position,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    const int depth = width_depth(simple_over(state, terrain, position, arithmetic, precision), state.field);
    const WidthPlan plan = width_plan(state, true);
    float first_distance = 0.0f;
    for (unsigned wall = 0u; wall < 2u; ++wall) {
        const ProbePair probes = width_probes(state, plan, depth, wall != 0u);
        const float distance = width_distance(state, terrain, position, probes, arithmetic, precision);
        if (wall == 0u) first_distance = distance;
        if (distance > 0.0f) continue;
        if (wall == 0u) {
            set_move_flag(state, kMoveFirstWidthBlocked);
        } else if (!has_move_flag(state, kMoveFirstWidthBlocked) ||
                   first_distance < 0.0f || distance < 0.0f) {
            set_move_flag(state, kMoveSecondWidthBlocked);
        }
        stop_for_width(state, environment, probes.vector, wall == 0u);
    }
}

bool system_permits_grounding(const ObjectContactState& state, std::uint8_t quadrant) {
    return (state.system_flags & (0x00000011u << quadrant)) == 0u;
}

bool has_ground_contact(const ObjectContactState& state) {
    return has_move_flag(state, kMoveGrounded);
}

bool within_side_range(std::uint16_t direction) {
    return direction >= 0x4000u && direction <= 0xc000u;
}

void update_orientation_and_velocity(
    ObjectContactState& state,
    float primary_distance,
    float secondary_distance,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    if (!has_ground_contact(state)) {
        return;
    }
    if (!has_move_flag(state, kMoveHoldOrientation) &&
        (state.collision_flags & 0x31u) == 0u &&
        has_move_flag(state, kMoveUseSurfaceDirection)) {
        std::uint16_t selected = state.metadata.primary_direction;
        if (primary_distance > secondary_distance) {
            selected = state.metadata.secondary_direction;
        } else if (primary_distance == secondary_distance) {
            const std::uint16_t current = static_cast<std::uint16_t>(
                state.orientation + state.fall_direction);
            const int primary_difference = std::abs(
                static_cast<int>(current) - static_cast<int>(state.metadata.primary_direction));
            const int secondary_difference = std::abs(
                static_cast<int>(current) - static_cast<int>(state.metadata.secondary_direction));
            if (primary_difference > secondary_difference) {
                selected = state.metadata.secondary_direction;
            }
        }
        state.orientation = static_cast<std::uint16_t>(selected - state.fall_direction);
    }
    if (has_move_flag(state, kMovePreserveVelocity) || has_move_flag(state, kMoveProtectVelocity)) {
        return;
    }
    switch (support_quadrant(state)) {
    case 1u:
        if (state.velocity_x < 0.0f) state.velocity_x = 0.0f;
        break;
    case 2u:
        if (state.velocity_y < 0.0f) state.velocity_y = 0.0f;
        break;
    case 3u:
        if (state.velocity_x > 0.0f) state.velocity_x = 0.0f;
        break;
    default:
        if (state.velocity_y > 0.0f) {
            if (has_move_flag(state, 0x00020000u)) {
                const float sine = nn_sin_cos(state.orientation, precision == TerrainCollisionPrecision::Single
                    ? CameraPrecision::Single : CameraPrecision::Double).sine;
                const double projected = arithmetic.rounded(static_cast<double>(sine) * state.velocity_y);
                state.velocity_x = arithmetic.spill(arithmetic.rounded(projected + state.velocity_x));
            }
            state.velocity_y = 0.0f;
        }
        break;
    }
}

void resolve_height(
    ObjectContactState& state,
    const ObjectContactEnvironment& environment,
    const TerrainCollision& terrain,
    ContactPosition& position,
    const Arithmetic& arithmetic,
    TerrainCollisionPrecision precision) {
    const std::uint32_t initial_collision_flags = state.collision_flags;
    const HeightGeometry geometry = height_geometry(state, environment);
    const std::uint16_t initial_flags = terrain_flags(state);

    if (!has_move_flag(state, kMoveSkipClearanceProbe)) {
        if (!has_move_flag(state, kMoveClearanceBypass)) {
            const std::uint16_t clearance_flags = static_cast<std::uint16_t>(initial_flags & ~kTerrainMaskFlag);
            const float primary_clearance = normal_distance(
                terrain,
                add_offset(position.x, geometry.clearance.primary_x, arithmetic),
                add_offset(position.y, geometry.clearance.primary_y, arithmetic),
                clearance_flags,
                geometry.clearance.vector,
                nullptr,
                nullptr,
                precision);
            const float secondary_clearance = normal_distance(
                terrain,
                add_offset(position.x, geometry.clearance.secondary_x, arithmetic),
                add_offset(position.y, geometry.clearance.secondary_y, arithmetic),
                clearance_flags,
                geometry.clearance.vector,
                nullptr,
                nullptr,
                precision);
            state.surface_clearance = minimum(primary_clearance, secondary_clearance) >= 0.0f;
        } else {
            state.surface_clearance = (geometry.support.vector & 2u) != 0u ||
                (state.previous_collision_flags & kMoveFirstWidthBlocked) != 0u;
        }
        if (state.surface_clearance) {
            set_move_flag(state, kMoveSurfaceClearance);
        } else {
            clear_move_flag(state, kMoveSurfaceClearance);
        }
    }

    std::uint16_t main_flags = initial_flags;
    if (!state.surface_clearance) {
        main_flags = static_cast<std::uint16_t>(main_flags | kTerrainMaskFlag);
    }

    std::uint16_t primary_direction = state.orientation;
    std::uint16_t secondary_direction = state.orientation;
    const float primary_distance = normal_distance(
        terrain,
        add_offset(position.x, geometry.support.primary_x, arithmetic),
        add_offset(position.y, geometry.support.primary_y, arithmetic),
        main_flags,
        geometry.support.vector,
        &primary_direction,
        &state.metadata.attribute,
        precision);
    apply_attribute(state);
    const float secondary_distance = normal_distance(
        terrain,
        add_offset(position.x, geometry.support.secondary_x, arithmetic),
        add_offset(position.y, geometry.support.secondary_y, arithmetic),
        main_flags,
        geometry.support.vector,
        &secondary_direction,
        &state.metadata.attribute,
        precision);
    apply_attribute(state);
    state.metadata.primary_direction = primary_direction;
    state.metadata.secondary_direction = secondary_direction;

    const int center_x = (geometry.support_quadrant & 1u) != 0u
        ? geometry.support.primary_x
        : midpoint(geometry.support.primary_x, geometry.support.secondary_x);
    const int center_y = (geometry.support_quadrant & 1u) != 0u
        ? midpoint(geometry.support.primary_y, geometry.support.secondary_y)
        : geometry.support.secondary_y;
    static_cast<void>(normal_distance(
        terrain,
        add_offset(position.x, center_x, arithmetic),
        add_offset(position.y, center_y, arithmetic),
        main_flags,
        geometry.support.vector,
        nullptr,
        &state.metadata.attribute,
        precision));
    apply_attribute(state);

    float correction_primary = primary_distance;
    float correction_secondary = secondary_distance;
    const std::uint16_t primary_with_fall = static_cast<std::uint16_t>(
        state.metadata.primary_direction + state.fall_direction);
    const std::uint16_t secondary_with_fall = static_cast<std::uint16_t>(
        state.metadata.secondary_direction + state.fall_direction);
    if ((state.collision_flags & 0x04u) != 0u && (state.collision_flags & 0x30u) == 0u &&
        has_move_flag(state, kMoveFixedOrientation) && geometry.normal_motion > 0.0f &&
        (state.collision_flags & 0x01u) == 0u &&
        (within_side_range(primary_with_fall) || within_side_range(secondary_with_fall))) {
        correction_primary = 24.0f;
        correction_secondary = 24.0f;
    }

    const float correction = minimum(correction_primary, correction_secondary);
    bool extended_primary = false;
    if (correction != 0.0f) {
        if (correction < 0.0f) {
            if ((!has_move_flag(state, kMoveFixedOrientation) || geometry.normal_motion >= 0.0f) &&
                system_permits_grounding(state, geometry.support_quadrant)) {
                set_move_flag(state, kMoveGrounded);
            }
            const bool can_correct = has_move_flag(state, kMoveExtendedCorrection)
                ? correction >= -28.0f
                : correction >= -14.0f && has_ground_contact(state);
            if (can_correct) {
                extended_primary = has_move_flag(state, kMoveExtendedCorrection) && correction < -16.0f;
                move_position(position, correction, geometry.support.vector, arithmetic);
                clear_move_flag(state, kMoveOneUnitContact);
            }
        } else if (!has_move_flag(state, kMoveFixedOrientation)) {
            if (correction == 1.0f) {
                set_move_flag(state, kMoveOneUnitContact);
            }
            float limit = arithmetic.add(std::fabs(state.movement_x), 3.0f);
            if (limit > 11.0f) {
                limit = 11.0f;
            }
            if (correction <= limit && system_permits_grounding(state, geometry.support_quadrant)) {
                set_move_flag(state, kMoveGrounded);
                move_position(position, correction, geometry.support.vector, arithmetic);
            } else {
                clear_move_flag(state, kMoveGrounded);
            }
        }
    } else if ((!has_move_flag(state, kMoveFixedOrientation) || state.velocity_y >= 0.0f) &&
               system_permits_grounding(state, geometry.support_quadrant)) {
        set_move_flag(state, kMoveGrounded);
    }

    if (!has_ground_contact(state)) state.collision_flags = initial_collision_flags;
    update_orientation_and_velocity(state, correction_primary, correction_secondary, arithmetic, precision);

    if (has_move_flag(state, kMoveExtendedCorrection) || geometry.normal_motion < 256.0f) {
        const std::uint16_t opposite_flags = static_cast<std::uint16_t>(main_flags | kTerrainMaskFlag);
        const float opposite_primary = normal_distance(
            terrain,
            add_offset(position.x, geometry.opposite.primary_x, arithmetic),
            add_offset(position.y, geometry.opposite.primary_y, arithmetic),
            opposite_flags,
            geometry.opposite.vector,
            nullptr,
            nullptr,
            precision);
        const float opposite_secondary = normal_distance(
            terrain,
            add_offset(position.x, geometry.opposite.secondary_x, arithmetic),
            add_offset(position.y, geometry.opposite.secondary_y, arithmetic),
            opposite_flags,
            geometry.opposite.vector,
            nullptr,
            nullptr,
            precision);
        const float opposite = minimum(opposite_primary, opposite_secondary);
        if (opposite <= 0.0f) {
            set_move_flag(state, kMoveSecondaryBlocked);
            const bool can_correct = has_move_flag(state, kMoveExtendedCorrection)
                ? opposite >= -28.0f
                : opposite >= -14.0f;
            if (can_correct) {
                if (extended_primary && has_ground_contact(state) && correction > opposite) {
                    move_position(position, arithmetic.subtract(opposite, correction), geometry.opposite.vector, arithmetic);
                } else {
                    move_position(position, opposite, geometry.opposite.vector, arithmetic);
                }
                if (!has_move_flag(state, kMovePreserveVelocity) &&
                    !has_move_flag(state, kMoveProtectVelocity) && geometry.normal_motion < 0.0f) {
                    if ((geometry.opposite.vector & 2u) != 0u) {
                        state.velocity_y = 0.0f;
                    } else {
                        state.velocity_x = 0.0f;
                    }
                }
            }
        }
    }
}

void store_position(ObjectContactState& state, const ContactPosition& position, const Arithmetic& arithmetic) {
    state.position_x = arithmetic.spill(arithmetic.rounded(static_cast<double>(state.position_x) -
        arithmetic.rounded(static_cast<double>(state.position_x) - position.x)));
    state.position_y = arithmetic.spill(arithmetic.rounded(static_cast<double>(state.position_y) -
        arithmetic.rounded(static_cast<double>(state.position_y) - position.y)));
}

void snap_grounded_position(ObjectContactState& state, const Arithmetic& arithmetic) {
    if (!has_ground_contact(state) ||
        (state.orientation != 0u && state.orientation != 0x8000u) ||
        (state.fall_direction & 0x3fffu) != 0u) {
        return;
    }
    const std::uint16_t shifted_fall = static_cast<std::uint16_t>(state.fall_direction + 0x2000u);
    if ((shifted_fall & 0x4000u) != 0u) {
        state.position_x = truncate_toward_zero(state.position_x, arithmetic);
    } else {
        state.position_y = truncate_toward_zero(state.position_y, arithmetic);
    }
}

}

ObjectContactStatus resolve_ordinary_object_contact(
    ObjectContactState& state,
    const ObjectContactEnvironment& environment,
    const TerrainCollision& terrain,
    TerrainCollisionPrecision precision) {
    require_precision(precision);
    validate_state(state);
    if (environment.alternate_block_terrain) {
        return ObjectContactStatus::UnsupportedAlternateBlockTerrain;
    }
    if (environment.pseudofall_direction != 0u) {
        return ObjectContactStatus::UnsupportedPseudoFall;
    }
    if (environment.moving_object_overlay_enabled) {
        return ObjectContactStatus::UnsupportedMovingObjectOverlay;
    }
    if (has_move_flag(state, kMoveTerrainDisabled)) {
        return ObjectContactStatus::UnsupportedTerrainDisabled;
    }
    if (state.fall_direction != 0u) {
        return ObjectContactStatus::UnsupportedFallDirection;
    }
    if (has_move_flag(state, kMoveSmoothOrientation)) {
        return ObjectContactStatus::UnsupportedSmoothOrientation;
    }

    const Arithmetic arithmetic{precision};
    state.surface_clearance = has_move_flag(state, kMoveSurfaceClearance);
    state.collision_flags = 0u;
    state.metadata.primary_direction = state.orientation;
    state.metadata.secondary_direction = state.orientation;
    state.metadata.attribute = 0u;
    ContactPosition position{
        state.position_x,
        state.position_y,
    };

    if (!has_move_flag(state, kMoveSkipWidth)) {
        resolve_first_width(state, terrain, position, arithmetic, precision);
        store_position(state, position, arithmetic);
    }
    if (!has_move_flag(state, kMoveSkipHeight)) {
        position = {state.position_x, state.position_y};
        resolve_height(state, environment, terrain, position, arithmetic, precision);
        store_position(state, position, arithmetic);
    }
    snap_grounded_position(state, arithmetic);

    if (!has_move_flag(state, kMoveSkipWidth)) {
        const ContactPosition second_position{
            state.position_x,
            state.position_y,
        };
        resolve_second_width(state, environment, terrain, second_position, arithmetic, precision);
    }
    return ObjectContactStatus::Applied;
}
