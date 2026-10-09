#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct EntityInst {
    std::string name;
    std::string model;
    float pos[3];
    float rot[4];
    std::string entity_class;
    // Raw unnamed int32 values trailing the class string in Manhunt 1 INST.
    // Kept in file order; meanings are intentionally not inferred.
    std::vector<int32_t> parameters;
};

std::vector<EntityInst> parse_inst(const std::vector<uint8_t>& data);
