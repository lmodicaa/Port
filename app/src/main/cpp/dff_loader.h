#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <string>

// Un vértice 3D con coordenadas de textura (UV) y normal
struct DFFVertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
    uint8_t r, g, b, a;
    
    // Skinning
    uint8_t bone_indices[4];
    float bone_weights[4];
};

struct DFFBone {
    uint32_t parent = 0xFFFFFFFF;
    uint32_t bone_id = 0xFFFFFFFF;
    uint32_t matrix_index = 0;
    float rot_mat[9] = {1,0,0, 0,1,0, 0,0,1};
    float pos_x = 0.f;
    float pos_y = 0.f;
    float pos_z = 0.f;
};

// Geometría completa extraída del DFF
struct DFFModel {
    std::vector<DFFVertex> vertices;
    std::vector<std::vector<uint16_t>> indices_by_mat;
    std::vector<std::string> material_textures;
    std::vector<float> inverse_bind_matrices; // float[16] per bone
    std::map<uint32_t, uint32_t> bone_id_to_index; // HAnim map
    std::vector<DFFBone> bones; // FrameList hierarchy
    bool valid = false;
    unsigned int vao = 0;
    unsigned int vbo = 0;
// We can't put RenderGroup here because it's defined in native-lib.cpp.
    // Instead we can use a basic struct or just leave it out and have native-lib keep a map.
};

// Parsea un archivo .dff de RenderWare en memoria
DFFModel dff_load(const uint8_t* data, size_t size);

// Parsea un archivo .dff que contiene mltiples Clumps y devuelve un mapa de nombre -> DFFModel
std::map<std::string, DFFModel> dff_load_archive(const uint8_t* data, size_t size);

// Parsea un archivo .bsp de RenderWare (World)
DFFModel bsp_load(const uint8_t* data, size_t size);
