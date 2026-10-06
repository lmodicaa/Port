#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ColVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ColSurface {
    int8_t material = 0;
    int8_t flag = 0;
    int8_t brightness = 0;
    int8_t light = 0;
};

struct ColSphere {
    ColVec3 center;
    float radius = 0.0f;
    ColSurface surface;
};

struct ColLine {
    ColVec3 a;
    ColVec3 b;
};

struct ColBox {
    ColVec3 min;
    ColVec3 max;
    ColSurface surface;
};

struct ColFace {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
};

struct ColModel {
    std::string name;

    ColVec3 center;
    float radius = 0.0f;
    ColVec3 min;
    ColVec3 max;

    std::vector<ColSphere> spheres;
    std::vector<ColLine> lines;
    std::vector<ColBox> boxes;
    std::vector<ColVec3> vertices;
    std::vector<ColFace> faces;
};

std::vector<ColModel> col_load_all(const uint8_t* data, size_t size);
