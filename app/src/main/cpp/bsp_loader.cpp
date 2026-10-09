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
    
    // El primer hijo de RW_WORLD es RW_STRUCT.
    ChunkHeader ws_hdr = r.read_chunk();
    constexpr uint32_t RW_WORLD_STRUCT_SIZE = 64;
    if (r.failed || ws_hdr.type != 0x0001 ||
        ws_hdr.size != RW_WORLD_STRUCT_SIZE ||
        !r.can_read(ws_hdr.size) ||
        r.pos > root_end || ws_hdr.size > root_end - r.pos) {
        LOGE("BSP: struct RW_WORLD inválido (type=0x%04X size=%u; esperado 64 bytes)",
             ws_hdr.type, ws_hdr.size);
        return model;
    }

    {
        const size_t ws_start = r.pos;
        model.world.root_is_world_sector = r.read<uint32_t>(); // +0
        for (int i = 0; i < 3; ++i) {
            model.world.inv_world_origin[i] = r.read<float>(); // +4
        }
        model.world.num_triangles = r.read<uint32_t>();       // +16
        model.world.num_vertices = r.read<uint32_t>();        // +20
        model.world.num_plane_sectors = r.read<uint32_t>();   // +24
        model.world.num_atomic_sectors = r.read<uint32_t>();  // +28
        model.world.col_sector_size = r.read<uint32_t>();     // +32
        model.world.format = r.read<uint32_t>();              // +36
        for (int i = 0; i < 3; ++i) {
            model.world.bbox_sup[i] = r.read<float>();        // +40
        }
        for (int i = 0; i < 3; ++i) {
            model.world.bbox_inf[i] = r.read<float>();        // +52
        }
        // RW_WORLD no aporta datos de iluminación en este layout.
        model.world.valid = false;
        if (r.failed || r.pos != ws_start + RW_WORLD_STRUCT_SIZE) {
            LOGE("BSP: lectura incompleta del struct RW_WORLD");
            return DFFModel{};
        }
        LOGI("RW_WORLD: bytes=64 triangles=%u vertices=%u planeSectors=%u atomicSectors=%u format=%u; lighting=unavailable",
             model.world.num_triangles, model.world.num_vertices,
             model.world.num_plane_sectors, model.world.num_atomic_sectors,
             model.world.format);
    }

    // El segundo hijo de RW_WORLD es RW_MATERIAL_LIST.
    if (r.pos > root_end || sizeof(ChunkHeader) > root_end - r.pos) {
        LOGE("BSP: falta cabecera de MaterialList dentro de RW_WORLD");
        return DFFModel{};
    }
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
        if (r.failed || numMaterials > ml_hdr.size / sizeof(ChunkHeader)) {
            LOGE("BSP: cantidad de materiales inválida (%u)", numMaterials);
            return DFFModel{};
        }
        if (!r.skip(ml_struct.size - sizeof(uint32_t))) {
            LOGE("BSP: struct de material list truncado al avanzar");
            return DFFModel{};
        }

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
                if (r.pos > mat_end || sizeof(ChunkHeader) > mat_end - r.pos) {
                    LOGE("BSP: cabecera de subchunk de material truncada");
                    return DFFModel{};
                }
                ChunkHeader th = r.read_chunk();
                if (r.failed || r.pos > mat_end || th.size > mat_end - r.pos ||
                    !r.can_read(th.size)) {
                    LOGE("BSP: textura de material truncada");
                    return DFFModel{};
                }
                if (th.type == 0x0006) { // Texture
                    size_t tex_end = r.pos + th.size;
                    ChunkHeader ts = r.read_chunk();
                    if (r.failed || r.pos > tex_end || ts.size > tex_end - r.pos ||
                        !r.can_read(ts.size) || !r.skip(ts.size) ||
                        r.pos > tex_end || sizeof(ChunkHeader) > tex_end - r.pos) {
                        LOGE("BSP: struct de textura truncado");
                        return DFFModel{};
                    }
                    ChunkHeader str_h = r.read_chunk();
                    if (r.failed || r.pos > tex_end || str_h.size > tex_end - r.pos ||
                        !r.can_read(str_h.size)) {
                        LOGE("BSP: nombre de textura truncado");
                        return DFFModel{};
                    }
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

    // Contraste de los conteos declarados en RW_WORLD con la geometría de sectores.
    uint64_t parsed_sector_triangles = 0;
    uint64_t parsed_sector_vertices = 0;

    // Ahora parseamos todo recursivamente buscando ATOMICSECTORs (0x0009)
    while (r.pos < root_end) {
        if (r.pos > root_end || sizeof(ChunkHeader) > root_end - r.pos) {
            LOGE("BSP: cabecera de chunk raíz truncada en %zu/%zu", r.pos, root_end);
            return DFFModel{};
        }
        ChunkHeader ch = r.read_chunk();
        
        if (ch.type == 0x0009) { // ATOMICSECTOR
            size_t atom_end = r.pos + ch.size;
            ChunkHeader st = r.read_chunk();
            if (r.failed || r.pos > atom_end || st.size > atom_end - r.pos ||
                !r.can_read(st.size)) {
                LOGE("BSP: struct de atomic/sector truncado");
                return DFFModel{};
            }
            uint32_t matListBase = r.read<uint32_t>();
            uint32_t numTri      = r.read<uint32_t>();
            uint32_t numVert     = r.read<uint32_t>();
            // Struct header: matListBase(4)+numTri(4)+numVert(4)+bboxMin(12)+bboxMax(12)+pad(8) = 44 bytes
            // We already read 12, skip the remaining bbox+padding.
            if (r.failed || st.size < 44 || !r.skip(32)) {
                LOGE("BSP: struct de atomic inválido/truncado");
                return DFFModel{};
            }
            const size_t tri_bytes = static_cast<size_t>(numTri) * 8u;
            if (tri_bytes > st.size - 44 ||
                static_cast<size_t>(numVert) > (st.size - 44 - tri_bytes) / 12u) {
                LOGE("BSP: conteos fuera de límites (verts=%u tris=%u struct=%u)",
                     numVert, numTri, st.size);
                return DFFModel{};
            }

            parsed_sector_triangles += numTri;
            parsed_sector_vertices += numVert;

            if (numVert > 0 && numTri > 0) {
                uint32_t vertex_offset = (uint32_t)model.vertices.size();

                // st.size = 44 (header) + numVert*bpv + numTri*8
                // Solve for bytes_per_vert (includes position)
                size_t total_vert_bytes = st.size - 44 - tri_bytes;
                size_t bpv = (numVert > 0 && total_vert_bytes > 0) ? total_vert_bytes / numVert : 12;

                // Manhunt PC: 28 bpv = pos(12)+normal(4)+color(4)+uv(8)
                bool has_normals = bpv >= 16; // pos + normals
                bool has_colors  = bpv >= 20; // + colors
                bool has_uv      = bpv >= 28; // + UVs


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
            if (r.failed || r.pos > root_end || st.size > root_end - r.pos ||
                !r.can_read(st.size)) {
                LOGE("BSP: struct de planesector truncado");
                return DFFModel{};
            }
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

    uint64_t parsed_index_count = 0;
    for (const auto& indices : model.indices_by_mat) {
        parsed_index_count += indices.size();
    }
    const uint64_t parsed_index_triangles = parsed_index_count / 3u;
    const bool sector_counts_match =
        parsed_sector_triangles == model.world.num_triangles &&
        parsed_sector_vertices == model.world.num_vertices;
    const bool geometry_counts_match =
        model.vertices.size() == model.world.num_vertices &&
        parsed_index_triangles == model.world.num_triangles;

    LOGI("RW_WORLD count check: header=(%u tris,%u verts) sectors=(%llu tris,%llu verts) geometry=(%zu verts,%llu indexed tris) match=%s",
         model.world.num_triangles, model.world.num_vertices,
         static_cast<unsigned long long>(parsed_sector_triangles),
         static_cast<unsigned long long>(parsed_sector_vertices),
         model.vertices.size(),
         static_cast<unsigned long long>(parsed_index_triangles),
         (sector_counts_match && geometry_counts_match) ? "YES" : "NO");
    if (!sector_counts_match || !geometry_counts_match) {
        LOGE("BSP: conteos RW_WORLD no coinciden con geometría; modelo rechazado");
        return DFFModel{};
    }

    model.valid = true;
    return model;
}
