#include "ame_draw.h"

#include <cmath>
#include <cstdint>
#include <utility>

std::vector<std::size_t> ame_primitive_sort_order(
    const std::vector<float>& depths,
    CameraPrecision precision) {
    if ((precision != CameraPrecision::Single && precision != CameraPrecision::Double) || depths.size() > 512u) {
        throw AmeDrawError("AME primitive sort context is unsupported");
    }
    std::vector<std::int32_t> keys;
    std::vector<std::size_t> order;
    keys.reserve(depths.size());
    order.reserve(depths.size());
    for (float depth : depths) {
        if (!std::isfinite(depth) || std::fabs(depth) > 16777216.0f) {
            throw AmeDrawError("AME primitive sort depth is invalid");
        }
        double key = static_cast<double>(depth) * 100.0;
        if (precision == CameraPrecision::Single) {
            key = static_cast<float>(key);
        }
        keys.push_back(static_cast<std::int32_t>(key));
        order.push_back(order.size());
    }
    for (std::size_t first = 0u; first + 1u < order.size(); ++first) {
        std::size_t selected = first;
        for (std::size_t next = first + 1u; next < order.size(); ++next) {
            if (keys[order[next]] > keys[order[selected]]) {
                selected = next;
            }
        }
        std::swap(order[first], order[selected]);
    }
    return order;
}
