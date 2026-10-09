#include "player_runtime.h"

#include <algorithm>
#include <cmath>

void player_update_stamina(float dt, bool moving, const PlayerControlConfig& config, float& stamina_remaining, float& recovery_delay, bool& sprint_active) {
    if (dt <= 0.0f) return;

    const float total =
        std::max(0.001f, config.stamina_total_sprint_time);

    if (sprint_active && moving &&
        stamina_remaining > 0.0f) {
        stamina_remaining =
            std::max(0.0f, stamina_remaining - dt);
        recovery_delay =
            std::max(0.0f, config.stamina_recovery_pause);

        if (stamina_remaining <= 0.0f) {
            sprint_active = false;
        }
        return;
    }

    if (recovery_delay > 0.0f) {
        recovery_delay =
            std::max(0.0f, recovery_delay - dt);
        return;
    }

    if (stamina_remaining >= total) {
        stamina_remaining = total;
        return;
    }

    const float recovery_seconds = moving
        ? config.stamina_recovery_moving
        : config.stamina_recovery_still;

    const float recovery =
        total / std::max(0.001f, recovery_seconds);

    stamina_remaining =
        std::min(total, stamina_remaining + recovery * dt);
}

float player_aim_zone_speed(float stick_distance, bool vertical, const PlayerControlConfig& config) {
    const float dead = std::max(0.001f, config.stick_dead_zone);
    float d = (stick_distance - dead) / std::max(0.001f, 1.0f - dead);
    d = std::max(0.0f, std::min(1.0f, d));

    float zone_pos = d * 10.0f;

    if (vertical) {
        zone_pos = std::min(zone_pos, config.vertical_aim_limit);
    }
    if (zone_pos <= 0.0f) return 0.0f;
    if (zone_pos >= 10.0f) return config.aim_zones[9];

    const float exact = zone_pos - 1.0f;
    const int lo = std::max(0, std::min(8, static_cast<int>(std::floor(exact))));
    const float t = exact - static_cast<float>(lo);
    return config.aim_zones[lo] * (1.0f - t) +
           config.aim_zones[lo + 1] * t;
}

float player_snap_aim_angle(float angle, const PlayerControlConfig& config) {
    const float width = config.aim_axis_width * 3.14159265359f / 180.0f;
    const float step = 3.14159265359f / 4.0f;
    const float nearest = std::round(angle / step) * step;
    const float delta = atan2f(sinf(angle - nearest), cosf(angle - nearest));
    return fabsf(delta) <= width ? nearest : angle;
}
