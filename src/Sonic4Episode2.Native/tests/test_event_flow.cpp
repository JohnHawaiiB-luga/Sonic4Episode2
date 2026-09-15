#include "event_flow.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

EventCases make_cases(
    std::int32_t case0,
    std::int32_t case1,
    std::int32_t case2,
    std::int32_t case3,
    std::int32_t case4,
    std::int32_t case5,
    std::int32_t case6,
    std::int32_t case7,
    std::int32_t case8 = 0) {
    EventCases cases{};
    cases.destination_ids = {case0, case1, case2, case3, case4, case5, case6, case7, case8};
    return cases;
}

std::vector<EventCases> make_table() {
    return {
        make_cases(1, -4, 2, 0, 9, 5, 6, 7),
        make_cases(2, 1, 0, 0, 0, 0, 0, 0),
        make_cases(-4, 0, 0, 0, 0, 0, 0, 0),
        make_cases(3, 1, 0, 0, 0, 0, 0, 0),
    };
}

EventFlowState make_state() {
    EventFlowState state{};
    state.current_id = 0;
    state.previous_id = -5;
    state.requested_id = -6;
    state.selected_case = 7;
    state.cached_destination_id = 89;
    state.scheduled = true;
    state.argument_size = 5u;
    for (std::size_t index = 0u; index < state.argument_bytes.size(); ++index) {
        state.argument_bytes[index] = static_cast<std::uint8_t>(0xA0u + index);
    }
    return state;
}

bool same_state(const EventFlowState& expected, const EventFlowState& actual) {
    return expected.current_id == actual.current_id &&
           expected.previous_id == actual.previous_id &&
           expected.requested_id == actual.requested_id &&
           expected.selected_case == actual.selected_case &&
           expected.cached_destination_id == actual.cached_destination_id &&
           expected.scheduled == actual.scheduled &&
           expected.argument_size == actual.argument_size &&
           expected.argument_bytes == actual.argument_bytes;
}

bool has_zero_arguments(const EventFlowState& state) {
    if (state.argument_size != 0u) {
        return false;
    }

    for (std::uint8_t byte : state.argument_bytes) {
        if (byte != 0u) {
            return false;
        }
    }
    return true;
}

template <typename Callable>
bool expects_invalid_without_mutation(
    EventFlowState& state,
    Callable&& callable,
    const char* message) {
    const EventFlowState before = state;
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return check(same_state(before, state), message);
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }

    std::fprintf(stderr, "%s (did not reject)\n", message);
    return false;
}

bool test_selection_sets_valid_destination_without_touching_other_state() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    const EventFlowState before = state;

    select_event_case(table, state, 2);

    return check(state.selected_case == 2, "selection did not store the selected case") &&
           check(
               state.requested_id == 2 && state.cached_destination_id == 2,
               "selection did not store the valid destination") &&
           check(
               state.current_id == before.current_id && state.previous_id == before.previous_id &&
                   state.scheduled == before.scheduled && state.argument_size == before.argument_size &&
                   state.argument_bytes == before.argument_bytes,
               "selection touched scheduling, identity, or arguments");
}

bool test_selection_uses_case_zero_for_zero_destination() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    const EventFlowState before = state;

    select_event_case(table, state, 3);

    return check(state.selected_case == 0, "zero destination selection did not store fallback case zero") &&
           check(
               state.requested_id == 1 && state.cached_destination_id == 1,
               "zero destination did not fall back to case zero") &&
           check(
               state.current_id == before.current_id && state.previous_id == before.previous_id &&
                   state.scheduled == before.scheduled && state.argument_size == before.argument_size &&
                   state.argument_bytes == before.argument_bytes,
               "fallback selection changed unrelated state");
}

bool test_selection_supports_ninth_case_zero_fallback() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    const EventFlowState before = state;

    try {
        select_event_case(table, state, 8);
    } catch (const std::invalid_argument&) {
        return check(false, "ninth case was rejected");
    } catch (...) {
        return check(false, "ninth case threw the wrong exception type");
    }

    return check(state.selected_case == 0, "zero ninth case did not resolve to case zero") &&
           check(
               state.requested_id == 1 && state.cached_destination_id == 1,
               "zero ninth case did not use case zero destination") &&
           check(
               state.current_id == before.current_id && state.previous_id == before.previous_id &&
                   state.scheduled == before.scheduled && state.argument_size == before.argument_size &&
                   state.argument_bytes == before.argument_bytes,
               "ninth case fallback changed unrelated state");
}

bool test_selection_uses_nonzero_ninth_case() {
    std::vector<EventCases> table = make_table();
    table[0] = make_cases(1, -4, 2, 0, 9, 5, 6, 7, 2);
    EventFlowState state = make_state();
    const EventFlowState before = state;

    select_event_case(table, state, 8);

    return check(state.selected_case == 8, "nonzero ninth case did not retain case eight") &&
           check(
               state.requested_id == 2 && state.cached_destination_id == 2,
               "nonzero ninth case did not select its destination") &&
           check(
               state.current_id == before.current_id && state.previous_id == before.previous_id &&
                   state.scheduled == before.scheduled && state.argument_size == before.argument_size &&
                   state.argument_bytes == before.argument_bytes,
               "nonzero ninth case changed unrelated state");
}

bool test_selection_preserves_request_and_cache_for_invalid_destinations() {
    const std::vector<EventCases> direct_negative_table = make_table();
    EventFlowState direct_negative = make_state();
    direct_negative.requested_id = 2;
    direct_negative.cached_destination_id = 88;
    const EventFlowState before_direct_negative = direct_negative;
    select_event_case(direct_negative_table, direct_negative, 1);
    if (!check(
            direct_negative.selected_case == 1 &&
                direct_negative.requested_id == before_direct_negative.requested_id &&
                direct_negative.cached_destination_id == before_direct_negative.cached_destination_id,
            "negative destination replaced request or cached destination")) {
        return false;
    }

    std::vector<EventCases> negative_fallback_table = make_table();
    negative_fallback_table[0] = make_cases(-4, 1, 2, 0, 9, 5, 6, 7);
    EventFlowState negative_fallback = make_state();
    negative_fallback.requested_id = 2;
    negative_fallback.cached_destination_id = 88;

    const EventFlowState before_negative = negative_fallback;
    select_event_case(negative_fallback_table, negative_fallback, 3);
    if (!check(
            negative_fallback.selected_case == 0 &&
                negative_fallback.requested_id == before_negative.requested_id &&
                negative_fallback.cached_destination_id == before_negative.cached_destination_id,
            "negative fallback destination replaced request or cached destination") ||
        !check(
            negative_fallback.current_id == before_negative.current_id &&
                negative_fallback.previous_id == before_negative.previous_id &&
                negative_fallback.scheduled == before_negative.scheduled &&
                negative_fallback.argument_size == before_negative.argument_size &&
                negative_fallback.argument_bytes == before_negative.argument_bytes,
            "negative fallback destination changed unrelated state")) {
        return false;
    }

    std::vector<EventCases> zero_fallback_table = make_table();
    zero_fallback_table[0] = make_cases(0, 1, 2, 0, 9, 5, 6, 7);
    EventFlowState zero_fallback = make_state();
    zero_fallback.requested_id = 2;
    zero_fallback.cached_destination_id = 88;
    const EventFlowState before_zero = zero_fallback;
    select_event_case(zero_fallback_table, zero_fallback, 3);
    if (!check(
            zero_fallback.selected_case == 0 &&
                zero_fallback.requested_id == before_zero.requested_id &&
                zero_fallback.cached_destination_id == before_zero.cached_destination_id,
            "zero fallback outside the table replaced request or cache")) {
        return false;
    }

    const std::vector<EventCases> table = make_table();
    EventFlowState out_of_range = make_state();
    out_of_range.requested_id = 2;
    out_of_range.cached_destination_id = 88;
    const EventFlowState before_out_of_range = out_of_range;
    select_event_case(table, out_of_range, 4);
    return check(
               out_of_range.selected_case == 4 &&
                   out_of_range.requested_id == before_out_of_range.requested_id &&
                   out_of_range.cached_destination_id == before_out_of_range.cached_destination_id,
               "out-of-range destination replaced request or cached destination") &&
           check(
               out_of_range.current_id == before_out_of_range.current_id &&
                   out_of_range.previous_id == before_out_of_range.previous_id &&
                   out_of_range.scheduled == before_out_of_range.scheduled &&
                   out_of_range.argument_size == before_out_of_range.argument_size &&
                   out_of_range.argument_bytes == before_out_of_range.argument_bytes,
               "out-of-range destination changed unrelated state");
}

bool test_selection_rejects_invalid_table_current_or_case_without_mutation() {
    const std::vector<EventCases> table = make_table();
    const std::vector<EventCases> empty;
    const std::vector<EventCases> too_large(
        32769u,
        make_cases(0, 0, 0, 0, 0, 0, 0, 0));
    EventFlowState invalid_current = make_state();
    invalid_current.current_id = -1;
    EventFlowState out_of_range_current = make_state();
    out_of_range_current.current_id = 4;
    EventFlowState invalid_case = make_state();
    EventFlowState high_case = make_state();
    EventFlowState empty_state = make_state();
    EventFlowState too_large_state = make_state();

    return expects_invalid_without_mutation(
               invalid_current,
               [&]() { select_event_case(table, invalid_current, 0); },
               "negative current id was accepted by selection") &&
           expects_invalid_without_mutation(
               out_of_range_current,
               [&]() { select_event_case(table, out_of_range_current, 0); },
               "out-of-range current id was accepted by selection") &&
           expects_invalid_without_mutation(
               invalid_case,
               [&]() { select_event_case(table, invalid_case, -1); },
               "negative selected case was accepted") &&
           expects_invalid_without_mutation(
               high_case,
               [&]() { select_event_case(table, high_case, 9); },
               "case nine was accepted") &&
           expects_invalid_without_mutation(
               empty_state,
               [&]() { select_event_case(empty, empty_state, 0); },
               "empty event table was accepted by selection") &&
           expects_invalid_without_mutation(
               too_large_state,
               [&]() { select_event_case(too_large, too_large_state, 0); },
               "oversized event table was accepted by selection");
}

bool test_last_signed16_event_id_is_a_valid_table_index() {
    std::vector<EventCases> table(32768u, make_cases(0, 0, 0, 0, 0, 0, 0, 0));
    table[0] = make_cases(0, 0, 32767, 0, 0, 0, 0, 0);
    table[32767u] = make_cases(0, 1, 0, 0, 0, 0, 0, 0);

    EventFlowState selection = make_state();
    selection.current_id = 0;
    EventFlowState commit = make_state();
    commit.current_id = 0;
    commit.requested_id = 32767;
    commit.cached_destination_id = 88;

    try {
        select_event_case(table, selection, 2);
        commit_scheduled_event_state(table, commit);
    } catch (const std::invalid_argument&) {
        return check(false, "last signed16 event index was rejected");
    } catch (...) {
        return check(false, "last signed16 event index threw the wrong exception type");
    }

    return check(
               selection.selected_case == 2 && selection.requested_id == 32767 &&
                   selection.cached_destination_id == 32767,
               "selection did not accept the last signed16 event id") &&
           check(
               commit.previous_id == 0 && commit.current_id == 32767 &&
                   commit.requested_id == -1 && commit.cached_destination_id == 88 &&
                   !commit.scheduled,
               "commit did not accept the last signed16 event id");
}

bool test_schedule_uses_case_zero_for_negative_request_and_clears_arguments() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    state.scheduled = false;
    state.argument_size = state.argument_bytes.size();
    state.requested_id = -6;
    const EventFlowState before = state;

    schedule_requested_event(table, state);

    return check(state.requested_id == 1, "negative request did not use case zero") &&
           check(state.cached_destination_id == before.cached_destination_id,
                 "scheduling refreshed the stale cached destination") &&
           check(state.scheduled, "scheduling did not set the scheduled flag") &&
           check(has_zero_arguments(state), "scheduling did not clear all eight argument bytes") &&
           check(
               state.current_id == before.current_id && state.previous_id == before.previous_id &&
                   state.selected_case == before.selected_case,
               "scheduling changed current, previous, or selected case");
}

bool test_schedule_keeps_nonnegative_request_and_stale_cache() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    state.scheduled = false;
    state.requested_id = 12;
    state.cached_destination_id = 88;
    const EventFlowState before = state;

    schedule_requested_event(table, state);

    return check(state.requested_id == 12, "scheduling replaced a nonnegative pending request") &&
           check(state.cached_destination_id == 88, "scheduling updated the cached destination") &&
           check(state.scheduled && has_zero_arguments(state),
                 "scheduling did not set the flag and clear arguments") &&
           check(
               state.current_id == before.current_id && state.previous_id == before.previous_id &&
                   state.selected_case == before.selected_case,
               "scheduling changed current, previous, or selected case");
}

bool test_schedule_rejects_invalid_table_or_current_without_mutation() {
    const std::vector<EventCases> table = make_table();
    const std::vector<EventCases> empty;
    const std::vector<EventCases> too_large(
        32769u,
        make_cases(0, 0, 0, 0, 0, 0, 0, 0));
    EventFlowState negative_current = make_state();
    negative_current.current_id = -1;
    EventFlowState out_of_range_current = make_state();
    out_of_range_current.current_id = 4;
    EventFlowState empty_state = make_state();
    EventFlowState too_large_state = make_state();

    return expects_invalid_without_mutation(
               negative_current,
               [&]() { schedule_requested_event(table, negative_current); },
               "negative current id was accepted by scheduling") &&
           expects_invalid_without_mutation(
               out_of_range_current,
               [&]() { schedule_requested_event(table, out_of_range_current); },
               "out-of-range current id was accepted by scheduling") &&
           expects_invalid_without_mutation(
               empty_state,
               [&]() { schedule_requested_event(empty, empty_state); },
               "empty event table was accepted by scheduling") &&
           expects_invalid_without_mutation(
               too_large_state,
               [&]() { schedule_requested_event(too_large, too_large_state); },
               "oversized event table was accepted by scheduling");
}

bool test_commit_sets_automatic_next_for_nonpositive_case_one() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    state.current_id = 1;
    state.previous_id = 0;
    state.requested_id = 2;
    state.cached_destination_id = 88;
    state.scheduled = true;
    state.selected_case = 6;
    state.argument_size = 3u;
    const std::array<std::uint8_t, kEventArgumentByteCount> arguments = state.argument_bytes;

    commit_scheduled_event_state(table, state);

    return check(
               state.previous_id == 1 && state.current_id == 2 && state.requested_id == -4,
               "commit did not transfer identity and automatic request") &&
           check(state.cached_destination_id == -4,
                 "automatic next did not cache its signed sentinel") &&
           check(!state.scheduled, "commit did not clear the scheduled flag") &&
           check(
               state.selected_case == 6 && state.argument_size == 3u &&
                   state.argument_bytes == arguments,
               "commit changed selection or arguments");
}

bool test_commit_treats_negative_case_one_as_automatic_next() {
    std::vector<EventCases> table = make_table();
    table[2].destination_ids[0] = 3;
    table[2].destination_ids[1] = -1;
    EventFlowState state = make_state();
    state.current_id = 1;
    state.requested_id = 2;
    state.cached_destination_id = 88;

    commit_scheduled_event_state(table, state);

    return check(
               state.previous_id == 1 && state.current_id == 2 && state.requested_id == 3,
               "negative case one did not request case zero") &&
           check(state.cached_destination_id == 3,
                 "negative case one did not refresh the automatic destination cache") &&
           check(!state.scheduled, "negative case one commit did not clear scheduling");
}

bool test_commit_preserves_cache_when_case_one_is_positive() {
    const std::vector<EventCases> table = make_table();
    EventFlowState state = make_state();
    state.current_id = 1;
    state.requested_id = 3;
    state.cached_destination_id = 88;
    state.scheduled = true;
    const EventFlowState before = state;

    commit_scheduled_event_state(table, state);

    return check(
               state.previous_id == 1 && state.current_id == 3 && state.requested_id == -1,
               "positive case one commit did not clear the request") &&
           check(state.cached_destination_id == before.cached_destination_id,
                 "positive case one commit refreshed the cache") &&
           check(!state.scheduled, "positive case one commit did not clear scheduling") &&
           check(
               state.selected_case == before.selected_case &&
                   state.argument_size == before.argument_size &&
                   state.argument_bytes == before.argument_bytes,
               "positive case one commit changed selection or arguments");
}

bool test_commit_is_a_noop_when_unscheduled() {
    const std::vector<EventCases> empty;
    EventFlowState state = make_state();
    state.current_id = 40000;
    state.requested_id = 40000;
    state.scheduled = false;
    const EventFlowState before = state;

    commit_scheduled_event_state(empty, state);

    return check(same_state(before, state), "unscheduled commit validated or changed state");
}

bool test_commit_rejects_invalid_pending_request_or_current_without_mutation() {
    const std::vector<EventCases> table = make_table();
    const std::vector<EventCases> empty;
    const std::vector<EventCases> too_large(
        32769u,
        make_cases(0, 0, 0, 0, 0, 0, 0, 0));
    EventFlowState negative_request = make_state();
    negative_request.current_id = 1;
    negative_request.requested_id = -1;
    EventFlowState out_of_range_request = make_state();
    out_of_range_request.current_id = 1;
    out_of_range_request.requested_id = 4;
    EventFlowState invalid_current = make_state();
    invalid_current.current_id = 4;
    invalid_current.requested_id = 1;
    EventFlowState empty_state = make_state();
    empty_state.current_id = 0;
    empty_state.requested_id = 0;
    EventFlowState too_large_state = make_state();
    too_large_state.current_id = 0;
    too_large_state.requested_id = 0;

    return expects_invalid_without_mutation(
               negative_request,
               [&]() { commit_scheduled_event_state(table, negative_request); },
               "negative scheduled request was accepted") &&
           expects_invalid_without_mutation(
               out_of_range_request,
               [&]() { commit_scheduled_event_state(table, out_of_range_request); },
               "out-of-range scheduled request was accepted") &&
           expects_invalid_without_mutation(
               invalid_current,
               [&]() { commit_scheduled_event_state(table, invalid_current); },
               "invalid current id was accepted by commit") &&
           expects_invalid_without_mutation(
               empty_state,
               [&]() { commit_scheduled_event_state(empty, empty_state); },
               "empty event table was accepted by commit") &&
           expects_invalid_without_mutation(
               too_large_state,
               [&]() { commit_scheduled_event_state(too_large, too_large_state); },
               "oversized event table was accepted by commit");
}

bool test_rejects_out_of_signed16_values_without_mutation() {
    std::vector<EventCases> selection_table = make_table();
    selection_table[0].destination_ids[2] = 32768;
    EventFlowState selected_destination = make_state();

    std::vector<EventCases> schedule_table = make_table();
    schedule_table[0].destination_ids[0] = -32769;
    EventFlowState high_request = make_state();
    high_request.scheduled = false;
    high_request.requested_id = 32768;
    EventFlowState low_case_zero = make_state();
    low_case_zero.scheduled = false;
    low_case_zero.requested_id = -1;

    std::vector<EventCases> commit_case_zero_table = make_table();
    commit_case_zero_table[2].destination_ids[0] = 32768;
    EventFlowState high_automatic_request = make_state();
    high_automatic_request.current_id = 1;
    high_automatic_request.requested_id = 2;

    std::vector<EventCases> commit_case_one_table = make_table();
    commit_case_one_table[2].destination_ids[1] = 32768;
    EventFlowState high_case_one = make_state();
    high_case_one.current_id = 1;
    high_case_one.requested_id = 2;

    return expects_invalid_without_mutation(
               selected_destination,
               [&]() { select_event_case(selection_table, selected_destination, 2); },
               "selection accepted an out-of-signed16 destination") &&
           expects_invalid_without_mutation(
               high_request,
               [&]() { schedule_requested_event(schedule_table, high_request); },
               "scheduling accepted an out-of-signed16 request") &&
           expects_invalid_without_mutation(
               low_case_zero,
               [&]() { schedule_requested_event(schedule_table, low_case_zero); },
               "scheduling accepted an out-of-signed16 case zero") &&
           expects_invalid_without_mutation(
               high_automatic_request,
               [&]() { commit_scheduled_event_state(commit_case_zero_table, high_automatic_request); },
               "commit accepted an out-of-signed16 automatic request") &&
           expects_invalid_without_mutation(
               high_case_one,
               [&]() { commit_scheduled_event_state(commit_case_one_table, high_case_one); },
               "commit accepted an out-of-signed16 case one");
}

}

int main() {
    if (!test_selection_sets_valid_destination_without_touching_other_state() ||
        !test_selection_uses_case_zero_for_zero_destination() ||
        !test_selection_supports_ninth_case_zero_fallback() ||
        !test_selection_uses_nonzero_ninth_case() ||
        !test_selection_preserves_request_and_cache_for_invalid_destinations() ||
        !test_selection_rejects_invalid_table_current_or_case_without_mutation() ||
        !test_last_signed16_event_id_is_a_valid_table_index() ||
        !test_schedule_uses_case_zero_for_negative_request_and_clears_arguments() ||
        !test_schedule_keeps_nonnegative_request_and_stale_cache() ||
        !test_schedule_rejects_invalid_table_or_current_without_mutation() ||
        !test_commit_sets_automatic_next_for_nonpositive_case_one() ||
        !test_commit_treats_negative_case_one_as_automatic_next() ||
        !test_commit_preserves_cache_when_case_one_is_positive() ||
        !test_commit_is_a_noop_when_unscheduled() ||
        !test_commit_rejects_invalid_pending_request_or_current_without_mutation() ||
        !test_rejects_out_of_signed16_values_without_mutation()) {
        return 1;
    }

    std::printf("event flow tests: selection, scheduling, bounded commit\n");
    return 0;
}
