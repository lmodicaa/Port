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
};

std::vector<EntityInst> parse_inst(const std::vector<uint8_t>& data);
