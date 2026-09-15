#pragma once

#include "terrain_collision.h"

#include <cstdint>

enum class ObjectContactStatus {
    Applied,
    UnsupportedAlternateBlockTerrain,
    UnsupportedPseudoFall,
    UnsupportedMovingObjectOverlay,
    UnsupportedTerrainDisabled,
    UnsupportedFallDirection,
    UnsupportedSurfaceOrientation,
    UnsupportedSmoothOrientation,
};

struct ObjectContactField {
    std::int16_t left;
    std::int16_t top;
    std::int16_t right;
    std::int16_t bottom;
};

struct ObjectContactFieldAdjustments {
    std::int8_t width_left_front;
    std::int8_t width_left_back;
    std::int8_t width_right_front;
    std::int8_t width_right_back;
    std::int8_t height_down_left;
    std::int8_t height_down_right;
    std::int8_t width_down_front = 0;
    std::int8_t width_down_back = 0;
    std::int8_t width_up_front = 0;
    std::int8_t width_up_back = 0;
    std::int8_t height_left_right = 0;
    std::int8_t height_left_left = 0;
    std::int8_t height_up_right = 0;
    std::int8_t height_up_left = 0;
    std::int8_t height_right_right = 0;
    std::int8_t height_right_left = 0;
};

struct ObjectContactMetadata {
    std::uint16_t primary_direction;
    std::uint16_t secondary_direction;
    std::uint32_t attribute;
};

struct ObjectContactEnvironment {
    bool alternate_block_terrain;
    bool moving_object_overlay_enabled;
    std::uint16_t pseudofall_direction;
    std::int8_t through_offset;
    bool normal_stage_vertical_width_ground_stop = true;
};

struct ObjectContactState {
    float position_x;
    float position_y;
    float velocity_x;
    float velocity_y;
    float movement_x;
    float movement_y;
    float ground_speed;
    std::uint16_t orientation;
    std::uint16_t fall_direction;
    std::uint32_t move_flags;
    std::uint32_t collision_flags;
    std::uint32_t previous_collision_flags;
    std::uint32_t system_flags;
    bool use_secondary_terrain;
    bool flipped_horizontal;
    bool surface_clearance;
    ObjectContactField field;
    ObjectContactFieldAdjustments adjustments;
    ObjectContactMetadata metadata;
};

ObjectContactStatus resolve_ordinary_object_contact(
    ObjectContactState& state,
    const ObjectContactEnvironment& environment,
    const TerrainCollision& terrain,
    TerrainCollisionPrecision precision);
