#pragma once

#include "math3d.h"

#include <map>
#include <utility>
#include <vector>

struct Tri { Vec3 a, b, c; };

struct CollisionGrid {
    float cell_size = 5.0f;
    std::map<std::pair<int, int>, std::vector<Tri>> cells;

    void add(const Tri& tri);
    const std::vector<Tri>* get(float x, float z) const;
};
