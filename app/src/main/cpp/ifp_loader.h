#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstddef>

struct AnimationKeyframe {
    float time = 0.0f;

    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;

    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
};

struct AnimationTrack {
    int bone_id = -1;

    std::string bone_name;

    std::vector<AnimationKeyframe> keyframes;
};

struct Animation {
    std::string name;

    float duration = 0.0f;

    std::vector<AnimationTrack> tracks;
};

std::map<std::string, Animation> load_ifp(
        const uint8_t* data,
        size_t size
);