#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr std::size_t kEventCaseCount = 9u;
constexpr std::size_t kEventArgumentByteCount = 8u;
constexpr std::size_t kEventFlowMaxTableSize = 32768u;
constexpr std::int32_t kEventFlowMinimumId = -32768;
constexpr std::int32_t kEventFlowMaximumId = 32767;

using EventId = std::int32_t;

struct EventCases {
    std::array<EventId, kEventCaseCount> destination_ids;
};

struct EventFlowState {
    EventId current_id = 0;
    EventId previous_id = -1;
    EventId requested_id = -1;
    std::int32_t selected_case = 0;
    EventId cached_destination_id = -1;
    bool scheduled = false;
    std::size_t argument_size = 0u;
    std::array<std::uint8_t, kEventArgumentByteCount> argument_bytes{};
};

void select_event_case(
    const std::vector<EventCases>& table,
    EventFlowState& state,
    std::int32_t selected_case);

void schedule_requested_event(const std::vector<EventCases>& table, EventFlowState& state);

// Commits only the scheduled state transition; callback dispatch is outside this API.
void commit_scheduled_event_state(const std::vector<EventCases>& table, EventFlowState& state);
