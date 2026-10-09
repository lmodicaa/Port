#pragma once

struct PlayerControlConfig {
    float stick_dead_zone = 0.35f;
    float move_walk_threshold = 0.53f;
    float move_run_threshold = 0.96f;
    float move_transition_speed = 0.20f;
    float sneak_walk_speed = 1.20f;
    float sneak_run_speed = 1.00f;
    float walk_speed = 1.00f;
    float run_speed = 1.00f;
    float sprint_speed = 1.00f;
    float crouch_forward_speed = 1.30f;
    float crouch_backward_speed = 0.90f;
    float crouch_sideways_speed = 1.20f;
    float aim_axis_width = 10.0f;
    float move_axis_width = 10.0f;
    float cam_position[3] = {0.1f, 0.0f, -0.1f};
    float cam_recentre_speed = 1700.0f;
    float cam_stair_speed = 500.0f;
    float aim_zones[10] = {3.0f, 8.0f, 12.0f, 18.0f, 25.0f, 35.0f, 45.0f, 57.0f, 76.0f, 125.0f};
    float vertical_aim_limit = 9.30f;
    float turn_pause = 0.27f;
    float turn_acceleration = 4.0f;
    float extra_turn_speed = 50.0f;
    float max_quick_turn_speed = 60.0f;
    float run_threshold = 0.95f;
    float stamina_total_sprint_time = 20.0f;
    float stamina_no_sprint_time = 3.0f;
    float stamina_recovery_moving = 54.0f;
    float stamina_recovery_running = 80.0f;
    float stamina_recovery_still = 22.0f;
    float stamina_recovery_pause = 0.0f;
};

void player_update_stamina(float dt, bool moving, const PlayerControlConfig& config,
                           float& stamina_remaining, float& recovery_delay,
                           bool& sprint_active);
float player_aim_zone_speed(float stick_distance, bool vertical, const PlayerControlConfig& config);
float player_snap_aim_angle(float angle, const PlayerControlConfig& config);
