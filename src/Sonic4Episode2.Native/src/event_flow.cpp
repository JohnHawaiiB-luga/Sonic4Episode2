#include "event_flow.h"

#include <stdexcept>

namespace {

bool is_signed_event_id(EventId value) {
    return value >= kEventFlowMinimumId && value <= kEventFlowMaximumId;
}

void validate_table(const std::vector<EventCases>& table) {
    if (table.empty() || table.size() > kEventFlowMaxTableSize) {
        throw std::invalid_argument("invalid event table");
    }
}

void validate_event_id(EventId value, const char* message) {
    if (!is_signed_event_id(value)) {
        throw std::invalid_argument(message);
    }
}

void validate_table_index(
    const std::vector<EventCases>& table,
    EventId value,
    const char* message) {
    validate_event_id(value, message);
    if (value < 0 || static_cast<std::size_t>(value) >= table.size()) {
        throw std::invalid_argument(message);
    }
}

void validate_current_event(const std::vector<EventCases>& table, const EventFlowState& state) {
    validate_table_index(table, state.current_id, "invalid current event id");
}

}

void select_event_case(
    const std::vector<EventCases>& table,
    EventFlowState& state,
    std::int32_t selected_case) {
    validate_table(table);
    validate_current_event(table, state);
    if (selected_case < 0 || static_cast<std::size_t>(selected_case) >= kEventCaseCount) {
        throw std::invalid_argument("invalid event case");
    }

    const EventCases& current = table[static_cast<std::size_t>(state.current_id)];
    std::int32_t resolved_case = selected_case;
    EventId destination = current.destination_ids[static_cast<std::size_t>(selected_case)];
    if (destination == 0) {
        destination = current.destination_ids[0u];
        resolved_case = 0;
    }
    validate_event_id(destination, "invalid selected event destination");

    state.selected_case = resolved_case;
    if (destination > 0 && static_cast<std::size_t>(destination) < table.size()) {
        state.requested_id = destination;
        state.cached_destination_id = destination;
    }
}

void schedule_requested_event(const std::vector<EventCases>& table, EventFlowState& state) {
    validate_table(table);
    validate_current_event(table, state);
    validate_event_id(state.requested_id, "invalid requested event id");

    EventId requested_id = state.requested_id;
    if (requested_id < 0) {
        requested_id = table[static_cast<std::size_t>(state.current_id)].destination_ids[0u];
        validate_event_id(requested_id, "invalid scheduled event destination");
    }

    state.requested_id = requested_id;
    state.scheduled = true;
    state.argument_size = 0u;
    state.argument_bytes.fill(0u);
}

void commit_scheduled_event_state(const std::vector<EventCases>& table, EventFlowState& state) {
    if (!state.scheduled) {
        return;
    }

    validate_table(table);
    validate_table_index(table, state.requested_id, "invalid scheduled event request");
    validate_current_event(table, state);

    const EventCases& next = table[static_cast<std::size_t>(state.requested_id)];
    const EventId next_case_one = next.destination_ids[1u];
    validate_event_id(next_case_one, "invalid next event case one");

    EventId automatic_request = -1;
    const bool has_automatic_request = next_case_one <= 0;
    if (has_automatic_request) {
        automatic_request = next.destination_ids[0u];
        validate_event_id(automatic_request, "invalid automatic event request");
    }

    state.previous_id = state.current_id;
    state.current_id = state.requested_id;
    state.requested_id = -1;
    if (has_automatic_request) {
        state.requested_id = automatic_request;
        state.cached_destination_id = automatic_request;
    }
    state.scheduled = false;
}
