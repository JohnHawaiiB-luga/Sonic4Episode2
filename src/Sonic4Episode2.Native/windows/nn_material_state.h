#pragma once

#include "d3d9_renderer.h"
#include "nn_material_profile.h"

namespace sonic4ep2::d3d9 {

struct NnUnlitMaterialState {
    RenderStateDescription render_state;
    bool fog_enabled;
};

struct NnLitMaterialState {
    RenderStateDescription render_state;
    bool fog_enabled;
};

NnUnlitMaterialState nn_unlit_material_state(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context,
    const RenderStateDescription& prior,
    bool fog_requested);

NnLitMaterialState nn_lit_material_state(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context,
    const RenderStateDescription& prior,
    bool fog_requested);

}
