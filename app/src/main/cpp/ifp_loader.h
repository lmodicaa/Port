#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

struct AnimationKeyframe {
    float time;
    float qx, qy, qz, qw; // Quaternion
    float tx, ty, tz;     // Translation (only if root or translated)
};

struct AnimationTrack {
    std::string bone_name;
    int bone_id; // -1 if not mapped yet
    std::vector<AnimationKeyframe> keyframes;
};

struct Animation {
    std::string name;
    float duration;
    std::vector<AnimationTrack> tracks;
};

// Carga todas las animaciones de un archivo IFP
std::map<std::string, Animation> load_ifp(const uint8_t* data, size_t size);
