#include "dff_loader.h"
#include <string>
#include <cmath>
#include <cstring>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ManhuntBSP", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ManhuntBSP", __VA_ARGS__)

struct ChunkHeader {
    uint32_t type;
    uint32_t size;
    uint32_t version;
};

class BSPReader {
public:
    const uint8_t* base;
    size_t pos;
    size_t max_size;
    bool failed = false;

    BSPReader(const uint8_t* d, size_t s) : base(d), pos(0), max_size(s) {}

    bool can_read(size_t bytes) const {
        return base != nullptr && pos <= max_size && bytes <= max_size - pos;
    }

    template<typename T>
    T read() {
        T val{};
        if (!can_read(sizeof(T))) {
            failed = true;
            return val;
        }
        std::memcpy(&val, base + pos, sizeof(T));
        pos += sizeof(T);
        return val;
    }

    ChunkHeader read_chunk() {
        if (!can_read(sizeof(ChunkHeader))) {
            failed = true;
            return ChunkHeader{};
        }
        ChunkHeader h{};
        h.type = read<uint32_t>();
        h.size = read<uint32_t>();
        h.version = read<uint32_t>();
        return h;
    }

    bool skip(size_t bytes) {
        if (!can_read(bytes)) {
            failed = true;
            pos = max_size;
            return false;
        }
        pos += bytes;
        return true;
    }
};

DFFModel bsp_load(const uint8_t* data, size_t size) {
    DFFModel model;
    if (data == nullptr || size < sizeof(ChunkHeader)) {
        LOGE("BSP: buffer nulo o demasiado pequeño (%zu bytes)", size);
        return model;
    }
    BSPReader r(data, size);

    ChunkHeader root = r.read_chunk();
    if (r.failed || !r.can_read(root.size)) {
        LOGE("BSP: cabecera raíz truncada o tamaño fuera del archivo");
        return model;
    }
    if (root.type != 0x000B) { // RW_WORLD
        LOGE("No es un archivo BSP/World (tipo=0x%04X)", root.type);
        return model;
    }

    size_t root_end = r.pos + root.size;
    
    // El primer hijo de RW_WORLD es RW_STRUCT
    ChunkHeader ws_hdr = r.read_chunk();
    if (r.failed || !r.can_read(ws_hdr.size) ||
        r.pos > root_end || ws_hdr.size > root_end - r.pos) {
        LOGE("BSP: struct del mundo truncado");
        return model;
    }
    if (ws_hdr.type != 0x0001) return model;

    // ── Struct del mundo (84 bytes). Layout RW 3.6 (ver parse_bsp.py):
    //   +0  rootIsWorldSector (int)
    //   +4  invWorldOrigin[3] (float)
    //   +16 ambientColor[4]   (float)  <- iluminación ambiente del nivel
    //   +32 dirAmbientColor[4](float)  <- luz direccional (color)
    //   +48 lightDirection[3] (float)  <- dirección de la luz
    //   +60 numTriangles ... +80 format
    {
        size_t ws_start = r.pos;
        r.skip(4);   // rootIsWorldSector
        r.skip(12);  // invWorldOrigin[3]
        for (int i = 0; i < 4; i++) model.world.ambient[i]     = r.read<float>();
        for (int i = 0; i < 4; i++) model.world.dir_ambient[i] = r.read<float>();
        for (int i = 0; i < 3; i++) model.world.light_dir[i]   = r.read<float>();
        model.world.valid = true;
        LOGI("World ambient=(%.3f %.3f %.3f) dirAmbient=(%.3f %.3f %.3f) lightDir=(%.3f %.3f %.3f)",
             model.world.ambient[0], model.world.ambient[1], model.world.ambient[2],
             model.world.dir_ambient[0], model.world.dir_ambient[1], model.world.dir_ambient[2],
             model.world.light_dir[0], model.world.light_dir[1], model.world.light_dir[2]);
        // Saltar el resto del struct hasta su final
        r.pos = ws_start + ws_hdr.size;
    }

    // El segundo hijo de RW_WORLD es RW_MATERIAL_LIST
    ChunkHeader ml_hdr = r.read_chunk();
    if (r.failed || r.pos > root_end || ml_hdr.size > root_end - r.pos ||
        !r.can_read(ml_hdr.size)) {
        LOGE("BSP: material list truncada");
        return DFFModel{};
    }
    if (ml_hdr.type == 0x0008) {
        size_t ml_end = r.pos + ml_hdr.size;
        ChunkHeader ml_struct = r.read_chunk();
        if (r.failed || ml_struct.size < sizeof(uint32_t) ||
            r.pos > ml_end || ml_struct.size > ml_end - r.pos ||
            !r.can_read(ml_struct.size)) {
            LOGE("BSP: struct de material list truncado");
            return DFFModel{};
        }
        uint32_t numMaterials = r.read<uint32_t>();
        r.skip(ml_struct.size - sizeof(uint32_t));
        
        model.material_textures.resize(numMaterials);
        for (uint32_t i = 0; i < numMaterials && r.pos < ml_end; i++) {
            ChunkHeader mat_hdr = r.read_chunk();
            if (r.failed || r.pos > ml_end || mat_hdr.size > ml_end - r.pos ||
                !r.can_read(mat_hdr.size)) {
                LOGE("BSP: material %u truncado", i);
                return DFFModel{};
            }
            if (mat_hdr.type != 0x0007) { r.skip(mat_hdr.size); continue; }
            size_t mat_end = r.pos + mat_hdr.size;
            model.materials.resize(numMaterials);
            ChunkHeader mat_struct = r.read_chunk();
            if (r.failed || r.pos > mat_end || mat_struct.size > mat_end - r.pos ||
                !r.can_read(mat_struct.size)) {
                LOGE("BSP: struct de material %u truncado", i);
                return DFFModel{};
            }
            if (mat_struct.size >= 28) {
                r.read<uint32_t>(); // flags
                uint8_t r_c = r.read<uint8_t>();
                uint8_t g_c = r.read<uint8_t>();
                uint8_t b_c = r.read<uint8_t>();
                uint8_t a_c = r.read<uint8_t>();
                model.materials[i].color[0] = r_c / 255.f;
                model.materials[i].color[1] = g_c / 255.f;
                model.materials[i].color[2] = b_c / 255.f;
                model.materials[i].color[3] = a_c / 255.f;
                r.read<uint32_t>(); // unused
                r.read<uint32_t>(); // textured
                model.materials[i].ambient = r.read<float>();
                model.materials[i].specular = r.read<float>();
                model.materials[i].diffuse = r.read<float>();
                if (mat_struct.size > 28) r.skip(mat_struct.size - 28);
            } else {
                r.skip(mat_struct.size);
            }
            while (r.pos < mat_end) {
                ChunkHeader th = r.read_chunk();
                if (r.failed || r.pos > mat_end || th.size > mat_end - r.pos ||
                    !r.can_read(th.size)) {
                    LOGE("BSP: textura de material truncada");
                    return DFFModel{};
                }
                if (th.type == 0x0006) { // Texture
                    size_t tex_end = r.pos + th.size;
                    ChunkHeader ts = r.read_chunk();
                    r.skip(ts.size);
                    ChunkHeader str_h = r.read_chunk();
                    if (str_h.type == 0x0002) {
                        std::string tex_name(reinterpret_cast<const char*>(r.base + r.pos), str_h.size);
                        while(!tex_name.empty() && tex_name.back() == '\0') tex_name.pop_back();
                        model.material_textures[i] = tex_name;
                    }
                    r.pos = tex_end;
                    break;
                } else {
                    r.skip(th.size);
                }
            }
            r.pos = mat_end;
        }
    } else {
        r.skip(ml_hdr.size);
    }
    
    model.indices_by_mat.resize(model.material_textures.size());

    // Ahora parseamos todo recursivamente buscando ATOMICSECTORs (0x0009)
    while (r.pos < root_end) {
        ChunkHeader ch = r.read_chunk();
        
        if (ch.type == 0x0009) { // ATOMICSECTOR
            size_t atom_end = r.pos + ch.size;
            ChunkHeader st = r.read_chunk();
            uint32_t matListBase = r.read<uint32_t>();
            uint32_t numTri      = r.read<uint32_t>();
            uint32_t numVert     = r.read<uint32_t>();
            // Struct header: matListBase(4)+numTri(4)+numVert(4)+bboxMin(12)+bboxMax(12)+pad(8) = 44 bytes
            // We already read 12, skip the remaining bbox+padding
            r.skip(32); // bboxMin(12) + bboxMax(12) + 2x uint32 unused(8)

            if (numVert > 0 && numTri > 0) {
                uint32_t vertex_offset = (uint32_t)model.vertices.size();

                // st.size = 44 (header) + numVert*bpv + numTri*8
                // Solve for bytes_per_vert (includes position)
                size_t tri_bytes = (size_t)numTri * 8;
                size_t total_vert_bytes = (st.size > 44 + tri_bytes) ? (st.size - 44 - tri_bytes) : 0;
                size_t bpv = (numVert > 0 && total_vert_bytes > 0) ? total_vert_bytes / numVert : 12;

                // Manhunt PC: 28 bpv = pos(12)+normal(4)+color(4)+uv(8)
                bool has_normals = bpv >= 16; // pos + normals
                bool has_colors  = bpv >= 20; // + colors
                bool has_uv      = bpv >= 28; // + UVs

                LOGI("ATOMICSECTOR: %u verts %u tris bpv=%zu nrm=%d col=%d uv=%d",
                     numVert, numTri, bpv, has_normals, has_colors, has_uv);

                // 1. Posiciones
                for (uint32_t i = 0; i < numVert; i++) {
                    DFFVertex v{};
                    v.x = r.read<float>();
                    v.y = r.read<float>();
                    v.z = r.read<float>();
                    v.u  = 0.f; v.v  = 0.f;
                    v.nx = 0.f; v.ny = 1.f; v.nz = 0.f;
                    v.r = 255; v.g = 255; v.b = 255; v.a = 255;
                    v.bone_indices[0] = 0; v.bone_indices[1] = 0; v.bone_indices[2] = 0; v.bone_indices[3] = 0;
                    v.bone_weights[0] = 1.0f; v.bone_weights[1] = 0.0f; v.bone_weights[2] = 0.0f; v.bone_weights[3] = 0.0f;
                    model.vertices.push_back(v);
                }

                // 2. Normales empaquetadas (int8 x4)
                if (has_normals) {
                    for (uint32_t i = 0; i < numVert; i++) {
                        int8_t nx8 = (int8_t)r.read<uint8_t>();
                        int8_t ny8 = (int8_t)r.read<uint8_t>();
                        int8_t nz8 = (int8_t)r.read<uint8_t>();
                        r.read<uint8_t>(); // pad
                        model.vertices[vertex_offset + i].nx = nx8 / 127.f;
                        model.vertices[vertex_offset + i].ny = ny8 / 127.f;
                        model.vertices[vertex_offset + i].nz = nz8 / 127.f;
                    }
                }

                // 3. Vertex colors (RGBA)
                if (has_colors) {
                    for (uint32_t i = 0; i < numVert; i++) {
                        model.vertices[vertex_offset + i].r = r.read<uint8_t>();
                        model.vertices[vertex_offset + i].g = r.read<uint8_t>();
                        model.vertices[vertex_offset + i].b = r.read<uint8_t>();
                        model.vertices[vertex_offset + i].a = r.read<uint8_t>();
                    }
                }

                // 4. UVs
                if (has_uv) {
                    for (uint32_t i = 0; i < numVert; i++) {
                        model.vertices[vertex_offset + i].u = r.read<float>();
                        model.vertices[vertex_offset + i].v = r.read<float>();
                    }
                }

                // 5. Triángulos (v1, v2, v3, matId) × 8 bytes
                for (uint32_t i = 0; i < numTri; i++) {
                    uint16_t v1     = r.read<uint16_t>();
                    uint16_t v2     = r.read<uint16_t>();
                    uint16_t v3     = r.read<uint16_t>();
                    uint16_t mat_id = r.read<uint16_t>();
                    if (mat_id < model.indices_by_mat.size()) {
                        model.indices_by_mat[mat_id].push_back(vertex_offset + v1);
                        model.indices_by_mat[mat_id].push_back(vertex_offset + v2);
                        model.indices_by_mat[mat_id].push_back(vertex_offset + v3);
                    }
                }
            }
            r.pos = atom_end;
        } 
        else if (ch.type == 0x000A) { // PLANESECTOR
            // PLANESECTOR wraps children — skip its own Struct, then fall into children
            ChunkHeader st = r.read_chunk();
            r.skip(st.size);
        }
        else {
            r.skip(ch.size);
        }
    }

    if (r.failed) {
        LOGE("BSP: lectura fuera de límites; se descarta el modelo");
        return DFFModel{};
    }
    model.valid = true;
    return model;
}
