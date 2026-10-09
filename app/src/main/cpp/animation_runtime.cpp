#include "animation_runtime.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

bool sample_animation_bone(
    const Animation* anim,
    float animation_time,
    const DFFBone& bone,
    float* out_pos,
    float* out_quat
) {
    out_pos[0] = bone.pos_x;
    out_pos[1] = bone.pos_y;
    out_pos[2] = bone.pos_z;

    out_quat[0] = 0.0f;
    out_quat[1] = 0.0f;
    out_quat[2] = 0.0f;
    out_quat[3] = 1.0f;

    if (!anim || bone.bone_id == 0xFFFFFFFF) {
        return false;
    }

    for (const auto& track : anim->tracks) {
        if (track.bone_id != static_cast<int>(bone.bone_id) ||
            track.keyframes.empty()) {
            continue;
        }

        const AnimationKeyframe* k0 = &track.keyframes.front();
        const AnimationKeyframe* k1 = &track.keyframes.front();

        if (animation_time <= track.keyframes.front().time) {
            k0 = &track.keyframes.front();
            k1 = &track.keyframes.front();
        } else {
            bool found = false;
            for (size_t k = 0; k + 1 < track.keyframes.size(); ++k) {
                const auto& a = track.keyframes[k];
                const auto& b = track.keyframes[k + 1];

                if (animation_time >= a.time &&
                    animation_time <= b.time) {
                    k0 = &a;
                    k1 = &b;
                    found = true;
                    break;
                }
            }

            if (!found) {
                k0 = &track.keyframes.back();
                k1 = &track.keyframes.back();
            }
        }

        float t = 0.0f;
        if (k1 != k0 && k1->time > k0->time) {
            t = (animation_time - k0->time) /
                (k1->time - k0->time);
            t = std::max(0.0f, std::min(1.0f, t));
        }

        const float q0[4] = {k0->qx, k0->qy, k0->qz, k0->qw};
        const float q1[4] = {k1->qx, k1->qy, k1->qz, k1->qw};

        quat_slerp(q0, q1, t, out_quat);

        const float anim_tx = k0->tx + t * (k1->tx - k0->tx);
        const float anim_ty = k0->ty + t * (k1->ty - k0->ty);
        const float anim_tz = k0->tz + t * (k1->tz - k0->tz);

        const bool root_motion_track = bone.bone_id == 1000;

        if (track.frame_type == 1) {
            out_pos[0] = bone.pos_x;
            out_pos[1] = bone.pos_y;
            out_pos[2] = bone.pos_z;
        } else {
            out_pos[0] = root_motion_track ? bone.pos_x : anim_tx;
            out_pos[1] = anim_ty;
            out_pos[2] = root_motion_track ? bone.pos_z : anim_tz;
        }

        return true;
    }

    return false;
}

float animation_root_motion_speed(const Animation* anim) {
    if (!anim || anim->duration <= 0.0001f) return 0.0f;

    // HAnim del Player usa el nodo raíz 1000. Su traslación se mantiene
    // fuera del esqueleto visual y representa el desplazamiento del actor.
    for (const auto& track : anim->tracks) {
        if (track.bone_id != 1000 ||
            track.keyframes.size() < 2 ||
            (track.frame_type != 2 && track.frame_type != 3)) {
            continue;
        }

        const auto& first = track.keyframes.front();
        const auto& last  = track.keyframes.back();
        const float dx = last.tx - first.tx;
        const float dz = last.tz - first.tz;
        const float distance = sqrtf(dx * dx + dz * dz);

        if (distance > 0.0001f) {
            return distance / anim->duration;
        }
    }

    return 0.0f;
}

bool animation_root_motion_delta(
    const Animation* anim,
    float time0,
    float dt,
    Vec3* out_delta
) {
    *out_delta = {0.0f, 0.0f, 0.0f};
    if (!anim || anim->duration <= 0.0001f || dt <= 0.0f) return false;

    const AnimationTrack* root_track = nullptr;
    for (const auto& track : anim->tracks) {
        if (track.bone_id == 1000 &&
            track.keyframes.size() >= 2 &&
            (track.frame_type == 2 || track.frame_type == 3)) {
            root_track = &track;
            break;
        }
    }
    if (!root_track) return false;

    const auto sample_root = [&](float t) -> Vec3 {
        if (t <= root_track->keyframes.front().time) {
            const auto& k = root_track->keyframes.front();
            return {k.tx, k.ty, k.tz};
        }

        if (t >= root_track->keyframes.back().time) {
            const auto& k = root_track->keyframes.back();
            return {k.tx, k.ty, k.tz};
        }

        for (size_t i = 0; i + 1 < root_track->keyframes.size(); ++i) {
            const auto& a = root_track->keyframes[i];
            const auto& b = root_track->keyframes[i + 1];
            if (t >= a.time && t <= b.time) {
                const float span = b.time - a.time;
                const float u = span > 0.000001f
                    ? std::max(0.0f, std::min(1.0f, (t - a.time) / span))
                    : 0.0f;
                return {
                    a.tx + (b.tx - a.tx) * u,
                    a.ty + (b.ty - a.ty) * u,
                    a.tz + (b.tz - a.tz) * u
                };
            }
        }

        const auto& k = root_track->keyframes.back();
        return {k.tx, k.ty, k.tz};
    };

    float start = fmodf(time0, anim->duration);
    if (start < 0.0f) start += anim->duration;

    float end = start + dt;
    Vec3 p0 = sample_root(start);
    Vec3 p1;

    if (end <= anim->duration) {
        p1 = sample_root(end);
        *out_delta = {
            p1.x - p0.x,
            p1.y - p0.y,
            p1.z - p0.z
        };
        return true;
    }

    // El ciclo cruza el final de la animación: conservar el tramo final
    // y sumar el tramo desde el primer frame del siguiente ciclo.
    const Vec3 pend = sample_root(anim->duration);
    const Vec3 pstart = sample_root(0.0f);
    const Vec3 pnext = sample_root(end - anim->duration);

    *out_delta = {
        (pend.x - p0.x) + (pnext.x - pstart.x),
        (pend.y - p0.y) + (pnext.y - pstart.y),
        (pend.z - p0.z) + (pnext.z - pstart.z)
    };
    return true;
}

bool is_looping_locomotion_animation(const Animation* anim) {
    if (!anim) return false;
    const std::string& n = anim->name;
    return n == "Stand_Idle" ||
           n == "Walk_Fwd" || n == "Walk_Bkw" ||
           n == "Walk_Left" || n == "Walk_Right" ||
           n == "Run_Fwd" || n == "Run_Bkw" ||
           n == "Run_Left" || n == "Run_Right" ||
           n == "Sneak_Walk_Fwd" || n == "Sneak_Walk_Bkw" ||
           n == "Sneak_Walk_Left" || n == "Sneak_Walk_Right" ||
           n == "Sprint_Fwd" || n == "Sprint_Bkw" ||
           n == "Sprint_Left" || n == "Sprint_Right";
}

const char* locomotion_direction_from_animation(const Animation* anim) {
    if (!anim) return "Fwd";
    std::string n = anim->name;
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n.find("_bkw") != std::string::npos) return "Bkw";
    if (n.find("_left") != std::string::npos) return "Left";
    if (n.find("_right") != std::string::npos) return "Right";
    return "Fwd";
}

float locomotion_phase(const Animation* anim, float time) {
    if (!anim || anim->duration <= 0.0001f) return 0.0f;
    float phase = fmodf(time, anim->duration) / anim->duration;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

float root_motion_distance_for_animation(
    const Animation* anim,
    float time,
    float dt
) {
    if (!anim || dt <= 0.0f) return 0.0f;

    Vec3 delta{};
    if (!animation_root_motion_delta(
            anim,
            time,
            dt,
            &delta
        )) {
        return 0.0f;
    }

    return sqrtf(
        delta.x * delta.x +
        delta.z * delta.z
    );
}
