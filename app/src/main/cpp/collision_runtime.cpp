#include "collision_runtime.h"

#include <algorithm>
#include <cmath>

void CollisionGrid::add(const Tri& t) {
    float min_x = std::min({t.a.x, t.b.x, t.c.x});
    float max_x = std::max({t.a.x, t.b.x, t.c.x});
    float min_z = std::min({t.a.z, t.b.z, t.c.z});
    float max_z = std::max({t.a.z, t.b.z, t.c.z});

    int cx1 = (int)std::floor(min_x / cell_size);
    int cx2 = (int)std::floor(max_x / cell_size);
    int cz1 = (int)std::floor(min_z / cell_size);
    int cz2 = (int)std::floor(max_z / cell_size);

    for (int cx = cx1; cx <= cx2; ++cx) {
        for (int cz = cz1; cz <= cz2; ++cz) {
            cells[{cx, cz}].push_back(t);
        }
    }
}

const std::vector<Tri>* CollisionGrid::get(float x, float z) const {
    int cx = (int)std::floor(x / cell_size);
    int cz = (int)std::floor(z / cell_size);
    auto it = cells.find({cx, cz});
    if (it != cells.end()) return &it->second;
    return nullptr;
}
