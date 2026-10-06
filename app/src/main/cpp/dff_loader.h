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

// Material de RenderWare: color RGBA + coeficientes de iluminación.
// Estos valores vienen directamente del struct del material (.bsp/.dff) —
// son los que usa el motor original, no constantes inventadas.
struct MaterialData {
    float color[4]      = {1.f, 1.f, 1.f, 1.f}; // diffuse/color RGBA (0..1)
    float ambient       = 0.f;                  // coeficiente ambiente
    float diffuse       = 1.f;                  // coeficiente difuso
    float specular      = 0.f;                  // coeficiente especular
    std::string texture;                        // nombre de textura (vacío = sin textura)
};

// Iluminación del mundo embebida en el RW_WORLD (.bsp).
// Manhunt usa estos valores para iluminar toda la escena.
struct WorldLighting {
    float ambient[4]     = {0.5f, 0.5f, 0.5f, 1.f};   // ambientColor del mundo
    float dir_ambient[4] = {0.5f, 0.5f, 0.5f, 1.f};   // directionalAmbientColor
    float light_dir[3]   = {0.f, -1.f, 0.f};          // lightDirection
    bool  valid          = false;
};

// Geometría completa extraída del DFF
struct DFFModel {
    std::vector<DFFVertex> vertices;
    std::vector<std::vector<uint32_t>> indices_by_mat;
    std::vector<std::string> material_textures;
    std::vector<MaterialData> materials;      // datos completos de material
    WorldLighting world;                      // sólo poblado por bsp_load()
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
