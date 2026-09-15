#include "camera_view.h"

#include <cstddef>
#include <cstring>

namespace {

void copy_float_bits(float& destination, const float& source) noexcept {
    std::memcpy(&destination, &source, sizeof(destination));
}

void copy_vector_bits(std::array<float, 3u>& destination, const std::array<float, 3u>& source) noexcept {
    for (std::size_t index = 0u; index < destination.size(); ++index) {
        copy_float_bits(destination[index], source[index]);
    }
}

}

CameraViewParameters make_camera_view_parameters(const CameraViewInput& input) noexcept {
    CameraViewParameters output{};
    output.leading_word = 0u;
    output.fov_angle = input.fov_angle;
    copy_float_bits(output.aspect, input.aspect);
    copy_float_bits(output.near_plane, input.near_plane);
    copy_float_bits(output.far_plane, input.far_plane);
    copy_vector_bits(output.eye, input.eye);
    copy_vector_bits(output.target, input.linked_target ? *input.linked_target : input.inline_target);
    output.roll_angle = input.roll_angle;
    return output;
}
