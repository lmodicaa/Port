#pragma once

#include "dff_loader.h"
#include "ifp_loader.h"
#include "math3d.h"

bool sample_animation_bone(const Animation* anim, float animation_time,
                           const DFFBone& bone, float* out_pos, float* out_quat);
float animation_root_motion_speed(const Animation* anim);
bool animation_root_motion_delta(const Animation* anim, float time0, float dt, Vec3* out_delta);
bool is_looping_locomotion_animation(const Animation* anim);
const char* locomotion_direction_from_animation(const Animation* anim);
float locomotion_phase(const Animation* anim, float time);
float root_motion_distance_for_animation(const Animation* anim, float time, float dt);
