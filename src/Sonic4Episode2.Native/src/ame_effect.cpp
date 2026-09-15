#include "ame_effect.h"

#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <utility>

namespace {

constexpr std::size_t kHeaderSize = 0x40u;
constexpr std::size_t kNodeAlignment = 0x10u;
constexpr std::size_t kCommonNodeSize = 0x20u;
constexpr std::size_t kOmniNodeSize = 0x64u;
constexpr std::size_t kCircleNodeSize = 0x74u;
constexpr std::size_t kSpriteNodeSize = 0xa4u;
constexpr std::size_t kLineNodeSize = 0xa0u;
constexpr std::size_t kRadialNodeSize = 0x38u;
constexpr std::size_t kSpriteKeySize = 0x14u;
constexpr std::uint32_t kAmeVersion = 0x00020000u;
constexpr std::uint16_t kOmniNodeType = 0x0100u;
constexpr std::uint16_t kCircleNodeType = 0x0103u;
constexpr std::uint16_t kSimpleSpriteNodeType = 0x0200u;
constexpr std::uint16_t kSpriteNodeType = 0x0201u;
constexpr std::uint16_t kLineNodeType = 0x0202u;
constexpr std::uint16_t kRadialNodeType = 0x0302u;

struct RawLinks {
    std::uint32_t child_offset;
    std::uint32_t sibling_offset;
    std::uint32_t parent_offset;
    std::optional<std::size_t> expected_parent_index;
};

struct ParsedNode {
    AmeEffectNode node;
    std::size_t end_offset;
    RawLinks links;
};

struct PendingNode {
    std::size_t offset;
    std::optional<std::size_t> expected_parent_index;
};

struct ScheduledNode {
    std::optional<std::size_t> index;
};

[[noreturn]] void fail(const char* message) {
    throw AmeEffectError(message);
}

std::uint16_t read_le16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
}

std::int16_t read_le_i16(const std::uint8_t* data) {
    const std::uint16_t value = read_le16(data);
    if ((value & 0x8000u) == 0u) {
        return static_cast<std::int16_t>(value);
    }
    return static_cast<std::int16_t>(
        static_cast<std::int32_t>(value) - static_cast<std::int32_t>(0x10000u));
}

std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u));
}

std::int32_t read_le_i32(const std::uint8_t* data) {
    const std::uint32_t value = read_le32(data);
    if ((value & 0x80000000u) == 0u) {
        return static_cast<std::int32_t>(value);
    }
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(value) - static_cast<std::int64_t>(0x100000000ull));
}

float read_le_float(const std::uint8_t* data) {
    const std::uint32_t bits = read_le32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::size_t checked_sum(std::size_t left, std::size_t right, const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        fail(message);
    }
    return left + right;
}

std::size_t checked_product(std::size_t left, std::size_t right, const char* message) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        fail(message);
    }
    return left * right;
}

bool fits_range(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

void require_range(std::size_t offset, std::size_t length, std::size_t size, const char* message) {
    if (!fits_range(offset, length, size)) {
        fail(message);
    }
}

std::size_t nonzero_node_offset(std::uint32_t raw_offset, std::size_t size, const char* message) {
    if ((raw_offset & 0x80000000u) != 0u) {
        fail(message);
    }
    const std::size_t offset = checked_size(raw_offset, message);
    if (offset < kHeaderSize || (offset & (kNodeAlignment - 1u)) != 0u) {
        fail(message);
    }
    require_range(offset, kCommonNodeSize, size, message);
    return offset;
}

std::optional<std::size_t> optional_node_offset(
    std::uint32_t raw_offset,
    std::size_t size,
    const char* message) {
    if (raw_offset == 0u) {
        return std::nullopt;
    }
    return nonzero_node_offset(raw_offset, size, message);
}

AmeEffectVector4 read_vector4(const std::uint8_t* data) {
    return {
        read_le_float(data),
        read_le_float(data + 4u),
        read_le_float(data + 8u),
        read_le_float(data + 12u),
    };
}

AmeEffectQuaternion read_quaternion(const std::uint8_t* data) {
    return {
        read_le_float(data),
        read_le_float(data + 4u),
        read_le_float(data + 8u),
        read_le_float(data + 12u),
    };
}

void require_nonoverlapping_node_span(
    std::map<std::size_t, std::size_t>& spans,
    std::size_t start,
    std::size_t end) {
    const auto following = spans.lower_bound(start);
    if (following != spans.end() && following->first < end) {
        fail("AME node spans overlap");
    }
    if (following != spans.begin()) {
        const auto previous = std::prev(following);
        if (previous->second > start) {
            fail("AME node spans overlap");
        }
    }
    spans.emplace(start, end);
}

ParsedNode parse_node(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t offset,
    std::optional<std::size_t> expected_parent_index) {
    require_range(offset, kCommonNodeSize, size, "AME common node is truncated");

    AmeEffectNode node{
        read_le_i16(data + offset),
        read_le16(data + offset + 2u),
        read_le32(data + offset + 4u),
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        AmeEffectOmniNode{},
    };
    for (std::size_t index = 0u; index < node.name_bytes.size(); ++index) {
        node.name_bytes[index] = data[offset + 8u + index];
    }

    const std::uint32_t child_offset = read_le32(data + offset + 0x14u);
    const std::uint32_t sibling_offset = read_le32(data + offset + 0x18u);
    const std::uint32_t parent_offset = read_le32(data + offset + 0x1cu);
    (void)optional_node_offset(child_offset, size, "AME child offset is invalid");
    (void)optional_node_offset(sibling_offset, size, "AME sibling offset is invalid");
    (void)optional_node_offset(parent_offset, size, "AME parent offset is invalid");

    std::size_t end_offset = 0u;
    if (node.type == kOmniNodeType || node.type == kCircleNodeType) {
        const std::size_t node_size = node.type == kCircleNodeType ? kCircleNodeSize : kOmniNodeSize;
        require_range(offset, node_size, size, "AME emitter node is truncated");
        const AmeEffectOmniNode emitter{
            read_vector4(data + offset + 0x20u),
            read_quaternion(data + offset + 0x30u),
            read_le_float(data + offset + 0x40u),
            read_le_float(data + offset + 0x44u),
            read_le_float(data + offset + 0x48u),
            read_le_float(data + offset + 0x4cu),
            read_le_float(data + offset + 0x50u),
            read_le_float(data + offset + 0x54u),
            read_le_float(data + offset + 0x58u),
            read_le_float(data + offset + 0x5cu),
            read_le_float(data + offset + 0x60u),
        };
        if (node.type == kCircleNodeType) {
            node.payload = AmeEffectCircleNode{
                emitter,
                read_le_float(data + offset + 0x64u),
                read_le_float(data + offset + 0x68u),
                read_le_float(data + offset + 0x6cu),
                read_le_float(data + offset + 0x70u),
            };
        } else {
            node.payload = emitter;
        }
        end_offset = checked_sum(offset, node_size, "AME emitter node span is too large");
    } else if (node.type == kSimpleSpriteNodeType || node.type == kSpriteNodeType) {
        require_range(offset, kSpriteNodeSize, size, "AME sprite node is truncated");
        const std::int32_t texture_key_count = read_le_i32(data + offset + 0xa0u);
        if (texture_key_count < 0) {
            fail("AME sprite texture key count is negative");
        }
        const std::size_t key_count = static_cast<std::size_t>(texture_key_count);
        const std::size_t key_bytes = checked_product(
            key_count,
            kSpriteKeySize,
            "AME sprite texture key span is too large");
        const std::size_t total_bytes = checked_sum(
            kSpriteNodeSize,
            key_bytes,
            "AME sprite node span is too large");
        require_range(offset, total_bytes, size, "AME sprite texture keys are truncated");

        std::vector<AmeEffectTextureAnimationKey> texture_keys;
        if (key_count > texture_keys.max_size()) {
            fail("AME sprite texture key count is too large");
        }
        texture_keys.reserve(key_count);
        const std::size_t key_data = checked_sum(offset, kSpriteNodeSize, "AME sprite key offset is too large");
        for (std::size_t index = 0u; index < key_count; ++index) {
            const std::size_t key_offset = checked_sum(
                key_data,
                checked_product(index, kSpriteKeySize, "AME sprite key offset is too large"),
                "AME sprite key offset is too large");
            texture_keys.push_back({
                read_le_float(data + key_offset),
                read_le_float(data + key_offset + 4u),
                read_le_float(data + key_offset + 8u),
                read_le_float(data + key_offset + 12u),
                read_le_float(data + key_offset + 16u),
            });
        }

        node.payload = AmeEffectSpriteNode{
            read_vector4(data + offset + 0x20u),
            read_quaternion(data + offset + 0x30u),
            read_le_float(data + offset + 0x40u),
            read_le_float(data + offset + 0x44u),
            read_le_float(data + offset + 0x48u),
            read_le_float(data + offset + 0x4cu),
            read_le_float(data + offset + 0x50u),
            read_le_float(data + offset + 0x54u),
            read_le_float(data + offset + 0x58u),
            read_le_float(data + offset + 0x5cu),
            read_le_float(data + offset + 0x60u),
            read_le_float(data + offset + 0x64u),
            read_le_float(data + offset + 0x68u),
            read_le_float(data + offset + 0x6cu),
            read_le_float(data + offset + 0x70u),
            read_le32(data + offset + 0x74u),
            read_le32(data + offset + 0x78u),
            read_le_i32(data + offset + 0x7cu),
            read_le_i16(data + offset + 0x80u),
            read_le_i16(data + offset + 0x82u),
            read_le_float(data + offset + 0x84u),
            read_le_float(data + offset + 0x88u),
            read_le_float(data + offset + 0x8cu),
            read_le_float(data + offset + 0x90u),
            read_le_float(data + offset + 0x94u),
            read_le_float(data + offset + 0x98u),
            read_le_float(data + offset + 0x9cu),
            texture_key_count,
            std::move(texture_keys),
        };
        end_offset = checked_sum(offset, total_bytes, "AME sprite node span is too large");
    } else if (node.type == kLineNodeType) {
        require_range(offset, kLineNodeSize, size, "AME line node is truncated");
        const std::int32_t texture_key_count = read_le_i32(data + offset + 0x9cu);
        if (texture_key_count < 0) {
            fail("AME line texture key count is negative");
        }
        const std::size_t key_count = static_cast<std::size_t>(texture_key_count);
        const std::size_t key_bytes = checked_product(
            key_count, kSpriteKeySize, "AME line texture key span is too large");
        const std::size_t total_bytes = checked_sum(
            kLineNodeSize, key_bytes, "AME line node span is too large");
        require_range(offset, total_bytes, size, "AME line texture keys are truncated");
        std::vector<AmeEffectTextureAnimationKey> texture_keys;
        if (key_count > texture_keys.max_size()) {
            fail("AME line texture key count is too large");
        }
        texture_keys.reserve(key_count);
        const std::size_t key_data = checked_sum(
            offset, kLineNodeSize, "AME line key offset is too large");
        for (std::size_t index = 0u; index < key_count; ++index) {
            const std::size_t key_offset = checked_sum(
                key_data,
                checked_product(index, kSpriteKeySize, "AME line key offset is too large"),
                "AME line key offset is too large");
            texture_keys.push_back({
                read_le_float(data + key_offset),
                read_le_float(data + key_offset + 4u),
                read_le_float(data + key_offset + 8u),
                read_le_float(data + key_offset + 12u),
                read_le_float(data + key_offset + 16u),
            });
        }
        node.payload = AmeEffectLineNode{
            read_vector4(data + offset + 0x20u),
            read_quaternion(data + offset + 0x30u),
            read_le_float(data + offset + 0x40u),
            read_le_float(data + offset + 0x44u),
            read_le_float(data + offset + 0x48u),
            read_le_float(data + offset + 0x4cu),
            read_le_float(data + offset + 0x50u),
            read_le_float(data + offset + 0x54u),
            read_le_float(data + offset + 0x58u),
            read_le_float(data + offset + 0x5cu),
            read_le_float(data + offset + 0x60u),
            read_le_float(data + offset + 0x64u),
            read_le32(data + offset + 0x68u),
            read_le32(data + offset + 0x6cu),
            read_le32(data + offset + 0x70u),
            read_le32(data + offset + 0x74u),
            read_le_i32(data + offset + 0x78u),
            read_le_i16(data + offset + 0x7cu),
            read_le_i16(data + offset + 0x7eu),
            read_le_float(data + offset + 0x80u),
            read_le_float(data + offset + 0x84u),
            read_le_float(data + offset + 0x88u),
            read_le_float(data + offset + 0x8cu),
            read_le_float(data + offset + 0x90u),
            read_le_float(data + offset + 0x94u),
            read_le_float(data + offset + 0x98u),
            texture_key_count,
            std::move(texture_keys),
        };
        end_offset = checked_sum(offset, total_bytes, "AME line node span is too large");
    } else if (node.type == kRadialNodeType) {
        require_range(offset, kRadialNodeSize, size, "AME radial node is truncated");
        node.payload = AmeEffectRadialNode{
            read_vector4(data + offset + 0x20u),
            read_le_float(data + offset + 0x30u),
            read_le_float(data + offset + 0x34u),
        };
        end_offset = checked_sum(offset, kRadialNodeSize, "AME radial node span is too large");
    } else {
        fail("AME node type is unsupported");
    }

    return {
        std::move(node),
        end_offset,
        {child_offset, sibling_offset, parent_offset, expected_parent_index},
    };
}

std::optional<std::size_t> resolve_link(
    std::uint32_t raw_offset,
    std::size_t size,
    const std::map<std::size_t, ScheduledNode>& scheduled) {
    const std::optional<std::size_t> offset = optional_node_offset(
        raw_offset,
        size,
        "AME link offset is invalid");
    if (!offset.has_value()) {
        return std::nullopt;
    }
    const auto found = scheduled.find(*offset);
    if (found == scheduled.end() || !found->second.index.has_value()) {
        fail("AME link target is absent from the linked graph");
    }
    return found->second.index;
}

}

AmeEffectData parse_pc_ame_effect(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        fail("AME data is null");
    }
    if (size < kHeaderSize) {
        fail("AME header is truncated");
    }
    if (data[0] != static_cast<std::uint8_t>('#') ||
        data[1] != static_cast<std::uint8_t>('A') ||
        data[2] != static_cast<std::uint8_t>('M') ||
        data[3] != static_cast<std::uint8_t>('E')) {
        fail("AME magic is unsupported");
    }
    if (read_le32(data + 4u) != kAmeVersion) {
        fail("AME version is unsupported");
    }

    AmeEffectData result{{
        {data[0], data[1], data[2], data[3]},
        read_le32(data + 4u),
        read_le_i32(data + 8u),
        read_vector4(data + 0x10u),
        read_le_float(data + 0x20u),
        read_le_float(data + 0x24u),
        {read_le_float(data + 0x28u), read_le_float(data + 0x2cu)},
        {
            read_le32(data + 0x30u),
            read_le32(data + 0x34u),
            read_le32(data + 0x38u),
            read_le32(data + 0x3cu),
        },
        std::nullopt,
    }, {}};

    const std::size_t maximum_node_count = (size - kHeaderSize) / kCommonNodeSize;
    std::map<std::size_t, ScheduledNode> scheduled;
    std::map<std::size_t, std::size_t> node_spans;
    std::vector<PendingNode> pending;
    std::vector<RawLinks> raw_links;

    const auto schedule = [&scheduled, &pending, maximum_node_count](
                              std::size_t offset,
                              std::optional<std::size_t> expected_parent_index) {
        if (scheduled.size() >= maximum_node_count) {
            fail("AME linked node count exceeds its input span");
        }
        const auto insertion = scheduled.emplace(
            offset,
            ScheduledNode{std::nullopt});
        if (!insertion.second) {
            fail("AME linked node is referenced more than once");
        }
        pending.push_back({offset, expected_parent_index});
    };

    const std::uint32_t raw_root_offset = read_le32(data + 0x0cu);
    const std::optional<std::size_t> root_offset = optional_node_offset(
        raw_root_offset,
        size,
        "AME root offset is invalid");
    if (root_offset.has_value()) {
        schedule(*root_offset, std::nullopt);
    }

    while (!pending.empty()) {
        const PendingNode pending_node = pending.back();
        pending.pop_back();
        const auto scheduled_node = scheduled.find(pending_node.offset);
        if (scheduled_node == scheduled.end() || scheduled_node->second.index.has_value()) {
            fail("AME traversal state is invalid");
        }
        if (result.nodes.size() >= maximum_node_count || raw_links.size() >= raw_links.max_size()) {
            fail("AME linked node count exceeds its input span");
        }

        ParsedNode parsed = parse_node(
            data,
            size,
            pending_node.offset,
            pending_node.expected_parent_index);
        require_nonoverlapping_node_span(node_spans, pending_node.offset, parsed.end_offset);
        const std::size_t node_index = result.nodes.size();
        scheduled_node->second.index = node_index;
        result.nodes.push_back(std::move(parsed.node));
        raw_links.push_back(parsed.links);

        const std::optional<std::size_t> child_offset = optional_node_offset(
            parsed.links.child_offset,
            size,
            "AME child offset is invalid");
        const std::optional<std::size_t> sibling_offset = optional_node_offset(
            parsed.links.sibling_offset,
            size,
            "AME sibling offset is invalid");
        if (sibling_offset.has_value()) {
            schedule(*sibling_offset, pending_node.expected_parent_index);
        }
        if (child_offset.has_value()) {
            schedule(*child_offset, node_index);
        }
    }

    result.header.root_node_index = resolve_link(raw_root_offset, size, scheduled);
    for (std::size_t index = 0u; index < result.nodes.size(); ++index) {
        AmeEffectNode& node = result.nodes[index];
        const RawLinks& links = raw_links[index];
        node.child_index = resolve_link(links.child_offset, size, scheduled);
        node.sibling_index = resolve_link(links.sibling_offset, size, scheduled);
        node.parent_index = resolve_link(links.parent_offset, size, scheduled);
        if (node.parent_index != links.expected_parent_index) {
            fail("AME parent link is inconsistent with the linked graph");
        }
    }

    return result;
}
