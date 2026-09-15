cmake_minimum_required(VERSION 3.20)

project(Sonic4Episode2Native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SONIC4EP2_NATIVE_BUILD_TOOLS "Build standalone native probe executables" ON)
option(SONIC4EP2_NATIVE_BUILD_GAME "Build the native Windows stage inspection application" OFF)
set(SONIC4EP2_NATIVE_GAME_ICON "" CACHE FILEPATH "Icon extracted from the user's original PC executable")

function(sonic4ep2_native_configure_target target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(sonic4ep2_native STATIC
    src/adx_audio.cpp
    src/ame_effect.cpp
    src/ame_runtime.cpp
    src/ame_line.cpp
    src/ame_draw.cpp
    src/ame_sprite.cpp
    src/cri_audio_bank.cpp
    src/damage_rings.cpp
    src/death_wait.cpp
    src/event_flow.cpp
    src/object_speed.cpp
    src/object_movement.cpp
    src/player_ground_walk.cpp
    src/player_air.cpp
    src/player_jump.cpp
    src/player_homing.cpp
    src/player_jump_dash_effect.cpp
    src/player_rectangles.cpp
    src/player_ring_counters.cpp
    src/player_spindash.cpp
    src/player_ground_motion.cpp
    src/sonic_ground_animation.cpp
    src/camera_math.cpp
    src/normal_camera_follow.cpp
    src/normal_camera_view.cpp
    src/camera_view.cpp
    src/camera_matrix.cpp
    src/mt_math_rand.cpp
    src/nn_trig.cpp
    src/nn_motion_a16.cpp
    src/nn_motion_frame.cpp
    src/nn_motion_data.cpp
    src/nn_motion_scalar.cpp
    src/nn_quaternion.cpp
    src/nn_motion_pose.cpp
    src/nn_motion_palette.cpp
    src/nn_shader_name.cpp
    src/nn_shader_profile.cpp
    src/nn_material_profile.cpp
    src/nn_shader_constants.cpp
    src/nn_normal_matrix.cpp
    src/nn_weighted_matrix_subset.cpp
    src/nn_lit_material_constants.cpp
    src/nn_parallel_lighting.cpp
    src/dds_texture.cpp
    src/txb_texture.cpp
    src/nn_fog.cpp
    src/nn_static_palette.cpp
    src/nn_node_clip.cpp
    src/view_bounds.cpp
    src/damage_ring_collision.cpp
    src/damage_ring_checks.cpp
    src/damage_ring_pickup.cpp
    src/stage_data.cpp
    src/stage_lighting.cpp
    src/ambient_field.cpp
    src/terrain_data.cpp
    src/terrain_collision.cpp
    src/object_contact.cpp
    src/nn_model_data.cpp
    src/render_scene.cpp
    src/stage_placement.cpp
    src/stage_transform.cpp
    src/stage_scene.cpp
)
if(MSVC)
    set_source_files_properties(src/adx_audio.cpp PROPERTIES COMPILE_OPTIONS /fp:precise)
    set_source_files_properties(src/ame_runtime.cpp src/ame_sprite.cpp src/ame_line.cpp src/ame_draw.cpp src/stage_transform.cpp src/camera_math.cpp src/normal_camera_follow.cpp src/normal_camera_view.cpp src/camera_matrix.cpp src/camera_matrix_d3dx.cpp src/nn_fog.cpp src/nn_static_palette.cpp src/nn_node_clip.cpp src/stage_lighting.cpp src/ambient_field.cpp src/nn_normal_matrix.cpp src/nn_lit_material_constants.cpp src/nn_parallel_lighting.cpp src/nn_motion_a16.cpp src/nn_motion_frame.cpp src/nn_motion_scalar.cpp src/nn_trig.cpp src/nn_quaternion.cpp src/nn_motion_pose.cpp src/nn_motion_palette.cpp src/object_speed.cpp src/object_movement.cpp src/player_ground_walk.cpp src/player_air.cpp src/player_jump.cpp src/player_homing.cpp src/player_jump_dash_effect.cpp src/player_spindash.cpp src/sonic_ground_animation.cpp windows/native_player_scene.cpp src/object_contact.cpp src/terrain_collision.cpp PROPERTIES COMPILE_OPTIONS /fp:precise)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/adx_audio.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
    set_source_files_properties(src/ame_runtime.cpp src/ame_sprite.cpp src/ame_line.cpp src/ame_draw.cpp src/stage_transform.cpp src/camera_math.cpp src/normal_camera_follow.cpp src/normal_camera_view.cpp src/camera_matrix.cpp src/camera_matrix_d3dx.cpp src/nn_fog.cpp src/nn_static_palette.cpp src/nn_node_clip.cpp src/stage_lighting.cpp src/ambient_field.cpp src/nn_normal_matrix.cpp src/nn_lit_material_constants.cpp src/nn_parallel_lighting.cpp src/nn_motion_a16.cpp src/nn_motion_frame.cpp src/nn_motion_scalar.cpp src/nn_trig.cpp src/nn_quaternion.cpp src/nn_motion_pose.cpp src/nn_motion_palette.cpp src/object_speed.cpp src/object_movement.cpp src/player_ground_walk.cpp src/player_air.cpp src/player_jump.cpp src/player_homing.cpp src/player_jump_dash_effect.cpp src/player_spindash.cpp src/sonic_ground_animation.cpp windows/native_player_scene.cpp src/object_contact.cpp src/terrain_collision.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
target_include_directories(sonic4ep2_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
sonic4ep2_native_configure_target(sonic4ep2_native)

if(WIN32)
    add_library(sonic4ep2_native_audio STATIC
        windows/native_audio_output.cpp
        windows/native_sonic_audio.cpp
    )
    target_include_directories(sonic4ep2_native_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/windows")
    target_link_libraries(sonic4ep2_native_audio PUBLIC sonic4ep2_native PRIVATE xaudio2 ole32)
    sonic4ep2_native_configure_target(sonic4ep2_native_audio)

    add_library(sonic4ep2_native_d3dx STATIC src/camera_matrix_d3dx.cpp)
    target_link_libraries(sonic4ep2_native_d3dx PUBLIC sonic4ep2_native)
    sonic4ep2_native_configure_target(sonic4ep2_native_d3dx)

    add_library(sonic4ep2_native_d3d9 STATIC
        windows/d3d9_device.cpp
        windows/d3d9_renderer.cpp
        windows/nn_material_state.cpp
    )
    target_include_directories(sonic4ep2_native_d3d9 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/windows")
    target_link_libraries(sonic4ep2_native_d3d9 PUBLIC sonic4ep2_native PRIVATE d3d9 user32 gdi32)
    sonic4ep2_native_configure_target(sonic4ep2_native_d3d9)

    if(SONIC4EP2_NATIVE_BUILD_GAME)
        if(NOT EXISTS "${SONIC4EP2_NATIVE_GAME_ICON}")
            message(FATAL_ERROR "SONIC4EP2_NATIVE_GAME_ICON must identify the privately extracted original icon")
        endif()
        enable_language(RC)
        file(TO_CMAKE_PATH "${SONIC4EP2_NATIVE_GAME_ICON}" SONIC4EP2_NATIVE_GAME_ICON_RC)
        configure_file(windows/native_game.rc.in "${CMAKE_CURRENT_BINARY_DIR}/native_game.rc" @ONLY)
        add_executable(sonic4ep2_game WIN32
            windows/native_game_main.cpp
            windows/native_player_scene.cpp
            windows/native_stage_renderer.cpp
            "${CMAKE_CURRENT_BINARY_DIR}/native_game.rc"
        )
        set_target_properties(sonic4ep2_game PROPERTIES OUTPUT_NAME "Sonic-Decomp")
        target_compile_definitions(sonic4ep2_game PRIVATE UNICODE _UNICODE)
        target_link_libraries(sonic4ep2_game PRIVATE sonic4ep2_native_d3d9 sonic4ep2_native_d3dx sonic4ep2_native_audio shell32)
        sonic4ep2_native_configure_target(sonic4ep2_game)
    endif()
endif()

if(SONIC4EP2_NATIVE_BUILD_TOOLS)
    add_executable(mt_math_rand_cli tools/mt_math_rand_cli.cpp)
    target_link_libraries(mt_math_rand_cli PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(mt_math_rand_cli)

    add_executable(damage_rings_cli tools/damage_rings_cli.cpp)
    target_link_libraries(damage_rings_cli PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(damage_rings_cli)

    add_executable(stage_inspect_cli tools/stage_inspect_cli.cpp)
    target_link_libraries(stage_inspect_cli PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(stage_inspect_cli)

    add_executable(resource_inspect_cli tools/resource_inspect_cli.cpp)
    target_link_libraries(resource_inspect_cli PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(resource_inspect_cli)

    add_executable(stage_scene_cli tools/stage_scene_cli.cpp)
    target_link_libraries(stage_scene_cli PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(stage_scene_cli)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(test_nn_motion_palette tests/test_nn_motion_palette.cpp)
    target_link_libraries(test_nn_motion_palette PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_motion_palette)
    add_test(NAME test_nn_motion_palette COMMAND test_nn_motion_palette)

    add_executable(test_nn_weighted_matrix_subset tests/test_nn_weighted_matrix_subset.cpp)
    target_link_libraries(test_nn_weighted_matrix_subset PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_weighted_matrix_subset)
    add_test(NAME test_nn_weighted_matrix_subset COMMAND test_nn_weighted_matrix_subset)

    add_executable(test_nn_quaternion tests/test_nn_quaternion.cpp)
    target_link_libraries(test_nn_quaternion PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_quaternion)
    add_test(NAME test_nn_quaternion COMMAND test_nn_quaternion)

    add_executable(test_nn_motion_pose tests/test_nn_motion_pose.cpp)
    target_link_libraries(test_nn_motion_pose PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_motion_pose)
    add_test(NAME test_nn_motion_pose COMMAND test_nn_motion_pose)

    add_executable(test_nn_motion_scalar tests/test_nn_motion_scalar.cpp)
    target_link_libraries(test_nn_motion_scalar PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_motion_scalar)
    add_test(NAME test_nn_motion_scalar COMMAND test_nn_motion_scalar)

    add_executable(test_nn_motion_data tests/test_nn_motion_data.cpp)
    target_link_libraries(test_nn_motion_data PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_motion_data)
    add_test(NAME test_nn_motion_data COMMAND test_nn_motion_data)

    add_executable(test_nn_motion_frame tests/test_nn_motion_frame.cpp)
    target_link_libraries(test_nn_motion_frame PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_motion_frame)
    add_test(NAME test_nn_motion_frame COMMAND test_nn_motion_frame)

    add_executable(test_nn_motion_a16 tests/test_nn_motion_a16.cpp)
    target_link_libraries(test_nn_motion_a16 PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_motion_a16)
    add_test(NAME test_nn_motion_a16 COMMAND test_nn_motion_a16)

    add_executable(test_mt_math_rand tests/test_mt_math_rand.cpp)
    target_link_libraries(test_mt_math_rand PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_mt_math_rand)
    add_test(NAME test_mt_math_rand COMMAND test_mt_math_rand)

    add_executable(test_damage_rings tests/test_damage_rings.cpp)
    target_link_libraries(test_damage_rings PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_rings)
    add_test(NAME test_damage_rings COMMAND test_damage_rings)

    add_executable(test_death_wait tests/test_death_wait.cpp)
    target_link_libraries(test_death_wait PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_death_wait)
    add_test(NAME test_death_wait COMMAND test_death_wait)

    add_executable(test_event_flow tests/test_event_flow.cpp)
    target_link_libraries(test_event_flow PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_event_flow)
    add_test(NAME test_event_flow COMMAND test_event_flow)

    add_executable(test_object_speed tests/test_object_speed.cpp)
    target_link_libraries(test_object_speed PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_object_speed)
    add_test(NAME test_object_speed COMMAND test_object_speed)

    add_executable(test_camera_math tests/test_camera_math.cpp)
    target_link_libraries(test_camera_math PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_camera_math)
    add_test(NAME test_camera_math COMMAND test_camera_math)

    add_executable(test_camera_view tests/test_camera_view.cpp)
    target_link_libraries(test_camera_view PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_camera_view)
    add_test(NAME test_camera_view COMMAND test_camera_view)

    add_executable(test_camera_matrix tests/test_camera_matrix.cpp)
    target_link_libraries(test_camera_matrix PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_camera_matrix)
    add_test(NAME test_camera_matrix COMMAND test_camera_matrix)

    add_executable(test_damage_ring_lifetime tests/test_damage_ring_lifetime.cpp)
    target_link_libraries(test_damage_ring_lifetime PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_ring_lifetime)
    add_test(NAME test_damage_ring_lifetime COMMAND test_damage_ring_lifetime)

    add_executable(test_damage_ring_motion tests/test_damage_ring_motion.cpp)
    target_link_libraries(test_damage_ring_motion PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_ring_motion)
    add_test(NAME test_damage_ring_motion COMMAND test_damage_ring_motion)

    add_executable(test_nn_trig tests/test_nn_trig.cpp)
    target_link_libraries(test_nn_trig PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_trig)
    add_test(NAME test_nn_trig COMMAND test_nn_trig)

    add_executable(test_nn_shader_name tests/test_nn_shader_name.cpp)
    target_link_libraries(test_nn_shader_name PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_shader_name)
    add_test(NAME test_nn_shader_name COMMAND test_nn_shader_name)

    add_executable(test_nn_shader_profile tests/test_nn_shader_profile.cpp)
    target_link_libraries(test_nn_shader_profile PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_shader_profile)
    add_test(NAME test_nn_shader_profile COMMAND test_nn_shader_profile)

    add_executable(test_nn_material_profile tests/test_nn_material_profile.cpp)
    target_link_libraries(test_nn_material_profile PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_material_profile)
    add_test(NAME test_nn_material_profile COMMAND test_nn_material_profile)

    add_executable(test_nn_shader_constants tests/test_nn_shader_constants.cpp)
    target_link_libraries(test_nn_shader_constants PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_shader_constants)
    add_test(NAME test_nn_shader_constants COMMAND test_nn_shader_constants)

    add_executable(test_nn_normal_matrix tests/test_nn_normal_matrix.cpp)
    target_link_libraries(test_nn_normal_matrix PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_normal_matrix)
    add_test(NAME test_nn_normal_matrix COMMAND test_nn_normal_matrix)

    add_executable(test_nn_lit_material_constants tests/test_nn_lit_material_constants.cpp)
    target_link_libraries(test_nn_lit_material_constants PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_lit_material_constants)
    add_test(NAME test_nn_lit_material_constants COMMAND test_nn_lit_material_constants)

    add_executable(test_nn_parallel_lighting tests/test_nn_parallel_lighting.cpp)
    target_link_libraries(test_nn_parallel_lighting PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_parallel_lighting)
    add_test(NAME test_nn_parallel_lighting COMMAND test_nn_parallel_lighting)

    add_executable(test_dds_texture tests/test_dds_texture.cpp)
    target_link_libraries(test_dds_texture PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_dds_texture)
    add_test(NAME test_dds_texture COMMAND test_dds_texture)

    add_executable(test_txb_texture tests/test_txb_texture.cpp)
    target_link_libraries(test_txb_texture PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_txb_texture)
    add_test(NAME test_txb_texture COMMAND test_txb_texture)

    add_executable(test_nn_fog tests/test_nn_fog.cpp)
    target_link_libraries(test_nn_fog PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_fog)
    add_test(NAME test_nn_fog COMMAND test_nn_fog)

    add_executable(test_nn_static_palette tests/test_nn_static_palette.cpp)
    target_link_libraries(test_nn_static_palette PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_static_palette)
    add_test(NAME test_nn_static_palette COMMAND test_nn_static_palette)

    add_executable(test_nn_node_clip tests/test_nn_node_clip.cpp)
    target_link_libraries(test_nn_node_clip PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_node_clip)
    add_test(NAME test_nn_node_clip COMMAND test_nn_node_clip)

    add_executable(test_damage_ring_acceleration tests/test_damage_ring_acceleration.cpp)
    target_link_libraries(test_damage_ring_acceleration PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_ring_acceleration)
    add_test(NAME test_damage_ring_acceleration COMMAND test_damage_ring_acceleration)

    add_executable(test_view_bounds tests/test_view_bounds.cpp)
    target_link_libraries(test_view_bounds PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_view_bounds)
    add_test(NAME test_view_bounds COMMAND test_view_bounds)

    add_executable(test_damage_ring_collision tests/test_damage_ring_collision.cpp)
    target_link_libraries(test_damage_ring_collision PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_ring_collision)
    add_test(NAME test_damage_ring_collision COMMAND test_damage_ring_collision)

    add_executable(test_damage_ring_checks tests/test_damage_ring_checks.cpp)
    target_link_libraries(test_damage_ring_checks PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_ring_checks)
    add_test(NAME test_damage_ring_checks COMMAND test_damage_ring_checks)

    add_executable(test_damage_ring_pickup tests/test_damage_ring_pickup.cpp)
    target_link_libraries(test_damage_ring_pickup PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_damage_ring_pickup)
    add_test(NAME test_damage_ring_pickup COMMAND test_damage_ring_pickup)

    add_executable(test_stage_data tests/test_stage_data.cpp)
    target_link_libraries(test_stage_data PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_stage_data)
    add_test(NAME test_stage_data COMMAND test_stage_data)

    add_executable(test_stage_lighting tests/test_stage_lighting.cpp)
    target_link_libraries(test_stage_lighting PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_stage_lighting)
    add_test(NAME test_stage_lighting COMMAND test_stage_lighting)

    add_executable(test_terrain_data tests/test_terrain_data.cpp)
    target_link_libraries(test_terrain_data PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_terrain_data)
    add_test(NAME test_terrain_data COMMAND test_terrain_data)

    add_executable(test_terrain_collision tests/test_terrain_collision.cpp)
    target_link_libraries(test_terrain_collision PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_terrain_collision)
    add_test(NAME test_terrain_collision COMMAND test_terrain_collision)

    add_executable(test_nn_model_data tests/test_nn_model_data.cpp)
    target_link_libraries(test_nn_model_data PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_nn_model_data)
    add_test(NAME test_nn_model_data COMMAND test_nn_model_data)

    add_executable(test_render_scene tests/test_render_scene.cpp)
    target_link_libraries(test_render_scene PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_render_scene)
    add_test(NAME test_render_scene COMMAND test_render_scene)

    if(WIN32)
        add_executable(test_nn_material_state tests/test_nn_material_state.cpp)
        target_link_libraries(test_nn_material_state PRIVATE sonic4ep2_native_d3d9)
        sonic4ep2_native_configure_target(test_nn_material_state)
        add_test(NAME test_nn_material_state COMMAND test_nn_material_state)

        add_executable(test_d3d9_renderer tests/test_d3d9_renderer.cpp)
        target_link_libraries(test_d3d9_renderer PRIVATE sonic4ep2_native_d3d9 d3dcompiler)
        sonic4ep2_native_configure_target(test_d3d9_renderer)
        add_test(NAME test_d3d9_renderer COMMAND test_d3d9_renderer)
        set_tests_properties(test_d3d9_renderer PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    add_executable(test_stage_placement tests/test_stage_placement.cpp)
    target_link_libraries(test_stage_placement PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_stage_placement)
    add_test(NAME test_stage_placement COMMAND test_stage_placement)

    add_executable(test_stage_transform tests/test_stage_transform.cpp)
    target_link_libraries(test_stage_transform PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_stage_transform)
    add_test(NAME test_stage_transform COMMAND test_stage_transform)

    add_executable(test_stage_scene tests/test_stage_scene.cpp)
    target_link_libraries(test_stage_scene PRIVATE sonic4ep2_native)
    sonic4ep2_native_configure_target(test_stage_scene)
    add_test(NAME test_stage_scene COMMAND test_stage_scene)

    if(WIN32 AND SONIC4EP2_NATIVE_BUILD_TOOLS)
        find_program(SONIC4EP2_NATIVE_POWERSHELL NAMES pwsh powershell)
        if(SONIC4EP2_NATIVE_POWERSHELL)
            add_test(
                NAME test_stage_scene_cli
                COMMAND "${SONIC4EP2_NATIVE_POWERSHELL}" -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stage_scene_cli.ps1"
                    -Executable "$<TARGET_FILE:stage_scene_cli>"
            )
            add_test(
                NAME test_resource_inspect_cli
                COMMAND "${SONIC4EP2_NATIVE_POWERSHELL}" -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_inspect_cli.ps1"
                    -Executable "$<TARGET_FILE:resource_inspect_cli>"
            )
            add_test(
                NAME test_stage_inspect_cli
                COMMAND "${SONIC4EP2_NATIVE_POWERSHELL}" -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stage_inspect_cli.ps1"
                    -Executable "$<TARGET_FILE:stage_inspect_cli>"
            )
            add_test(
                NAME test_mt_math_rand_cli
                COMMAND "${SONIC4EP2_NATIVE_POWERSHELL}" -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.ps1"
                    -Executable "$<TARGET_FILE:mt_math_rand_cli>"
            )
            add_test(
                NAME test_damage_rings_cli
                COMMAND "${SONIC4EP2_NATIVE_POWERSHELL}" -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_damage_rings_cli.ps1"
                    -Executable "$<TARGET_FILE:damage_rings_cli>"
            )
        endif()
    endif()
endif()
