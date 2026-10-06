#include "dff_loader.h"
#include <cstring>
#include <android/log.h>

#define TAG "Manhunt/DFF"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static constexpr uint32_t RW_STRUCT        = 0x0001;
static constexpr uint32_t RW_GEOMETRY      = 0x000F;
static constexpr uint32_t RW_CLUMP         = 0x0010;
static constexpr uint32_t RW_GEOMETRYLIST  = 0x001A;

struct ChunkHeader {
    uint32_t type;
    uint32_t size;
    uint32_t version;
};

struct Reader {
    const uint8_t* base;
    size_t         total;
    size_t         pos = 0;

    bool can_read(size_t n) const { return pos + n <= total; }

    template<typename T>
    T read() {
        T v{};
        if (can_read(sizeof(T))) {
            memcpy(&v, base + pos, sizeof(T));
            pos += sizeof(T);
        }
        return v;
    }

    void skip(size_t n) { pos += n; }
    ChunkHeader read_chunk() { return read<ChunkHeader>(); }
};

DFFModel dff_load(const uint8_t* data, size_t size) {
    Reader r{data, size};
    DFFModel model;
    
    // Escaneamos recursivamente buscando GEOMETRY chunks (0x0F)
    while (r.pos + sizeof(ChunkHeader) <= r.total) {
        ChunkHeader hdr = r.read_chunk();
        
        // Si es un contenedor, leemos sus hijos (no skipeamos su payload)
        if (hdr.type == RW_CLUMP || hdr.type == RW_GEOMETRYLIST) {
            continue; 
        }
        
        if (hdr.type == 0x000E) { // RW_FRAMELIST
            size_t frame_end = r.pos + hdr.size;
            ChunkHeader fstruct = r.read_chunk();
            uint32_t frameCount = r.read<uint32_t>();
            
            model.bones.resize(frameCount);
            for (uint32_t i = 0; i < frameCount; i++) {
                // matrix (9 floats) - right, up, at
                for (int j = 0; j < 9; j++) {
                    model.bones[i].rot_mat[j] = r.read<float>();
                }
                
                // pos (3 floats)
                model.bones[i].pos_x = r.read<float>();
                model.bones[i].pos_y = r.read<float>();
                model.bones[i].pos_z = r.read<float>();
                
                model.bones[i].parent = r.read<uint32_t>();
                r.read<uint32_t>(); // flags
            }
            
            // Extensions
            for (uint32_t i = 0; i < frameCount; i++) {
                ChunkHeader ext = r.read_chunk();
                size_t ext_end = r.pos + ext.size;
                while (r.pos + sizeof(ChunkHeader) <= ext_end) {
                    ChunkHeader plug = r.read_chunk();
                    size_t plug_payload_start = r.pos;
                    size_t plug_payload_end = plug_payload_start + plug.size;
                    if (plug.type == 0x011E) { // HAnim
                        uint32_t ver = r.read<uint32_t>();
                        uint32_t hanim_id = r.read<uint32_t>();
                        uint32_t nodeCount = r.read<uint32_t>();
                        if (nodeCount > 0) {
                            r.read<uint32_t>(); // flags
                            r.read<uint32_t>(); // keyFrameSize
                            for (uint32_t n = 0; n < nodeCount; n++) {
                                uint32_t nodeId = r.read<uint32_t>();
                                uint32_t nodeIdx = r.read<uint32_t>();
                                r.read<uint32_t>(); // flags
                                model.bone_id_to_index[nodeId] = nodeIdx;
                                if (nodeCount < 80) {
                                    LOGI("HAnim frame %u node %u: id=%u idx=%u", i, n, nodeId, nodeIdx);
                                }
                            }
                            if (nodeCount >= 80) {
                                LOGI("HAnim frame %u: %u nodes (truncado)", i, nodeCount);
                            }
                        }
                    }
                    r.pos = plug_payload_end; // avanzar al fin del payload (evita doble-avance)
                }
                r.pos = ext_end; // fix any bad size reading
            }

            // Mapear bone_id a cada frame invirtiendo bone_id_to_index
            // (HAnim nodeIdx suele ser el índice del frame en el FrameList).
            {
                std::map<uint32_t, uint32_t> frame_to_boneid;
                for (auto& kv : model.bone_id_to_index) {
                    // kv.first = boneId, kv.second = frameIdx (supuesto)
                    if (kv.second < frameCount) {
                        frame_to_boneid[kv.second] = kv.first;
                    }
                }
                for (uint32_t i = 0; i < frameCount; i++) {
                    auto it = frame_to_boneid.find(i);
                    if (it != frame_to_boneid.end()) {
                        model.bones[i].bone_id = it->second;
                    } else {
                        model.bones[i].bone_id = 0xFFFFFFFF;
                    }
                    // matrix_index se asigna al final (cuando se conozca el Skin boneCount).
                    model.bones[i].matrix_index = i;
                }
                LOGI("FrameList: %u frames, %zu nodos HAnim mapeados", frameCount, frame_to_boneid.size());
            }
            
            r.pos = frame_end;
            continue;
        }

        if (hdr.type != RW_GEOMETRY) {
            r.skip(hdr.size);
            continue;
        }
        
        size_t geom_end = r.pos + hdr.size;
        
        // El primer chunk de Geometry es el Struct con la info principal
        ChunkHeader sh = r.read_chunk();
        if (sh.type != RW_STRUCT) {
            LOGE("Geometry sin Struct interno, skipeando");
            r.pos = geom_end;
            continue;
        }
        
        uint32_t format = r.read<uint32_t>();
        uint32_t numTriangles = r.read<uint32_t>();
        uint32_t numVertices = r.read<uint32_t>();
        uint32_t numMorphTargets = r.read<uint32_t>();
        
        LOGI("Geometry: %d tris, %d verts, format 0x%X", numTriangles, numVertices, format);
        
        // Banderas (flags) del formato
        bool has_colors = (format & 0x08) != 0;
        bool has_tex    = (format & 0x04) != 0;
        bool has_norms  = (format & 0x10) != 0;
        
        // Omitimos los pre-lit colors si existen (RGBA)
        std::vector<uint8_t> colors(numVertices * 4, 255);
        if (has_colors) {
            if (r.can_read(numVertices * 4)) memcpy(colors.data(), r.base + r.pos, numVertices * 4);
            r.skip(numVertices * 4);
        }
        
        // Leemos los UVs (TexCoords)
        int num_uv_sets = (format & 0x00FF0000) >> 16;
        if (num_uv_sets == 0) num_uv_sets = has_tex ? 1 : 0;
        
        std::vector<float> uvs(numVertices * 2, 0.0f);
        if (num_uv_sets > 0 && r.can_read(numVertices * 8)) {
            memcpy(uvs.data(), r.base + r.pos, numVertices * 8);
            r.skip(numVertices * 8 * num_uv_sets);
        }
        
        // Leemos los triángulos (4 uint16_t por cara: v2, v1, materialId, v3)
        uint16_t vertex_offset = static_cast<uint16_t>(model.vertices.size());
        for (uint32_t i = 0; i < numTriangles; i++) {
            uint16_t v2  = r.read<uint16_t>();
            uint16_t v1  = r.read<uint16_t>();
            uint16_t mat = r.read<uint16_t>();
            uint16_t v3  = r.read<uint16_t>();
            
            if (mat >= model.indices_by_mat.size()) {
                model.indices_by_mat.resize(mat + 1);
            }
            
            // RenderWare usa este orden peculiar
            model.indices_by_mat[mat].push_back(vertex_offset + v1);
            model.indices_by_mat[mat].push_back(vertex_offset + v2);
            model.indices_by_mat[mat].push_back(vertex_offset + v3);
        }
        
        // Finalmente leemos los Morph Targets (generalmente 1, que contiene las posiciones XYZ)
        for (uint32_t m = 0; m < numMorphTargets; m++) {
            r.skip(16); // Bounding Sphere (x, y, z, r = 4 floats)
            uint32_t has_pos = r.read<uint32_t>();
            uint32_t has_nrm = r.read<uint32_t>();

            // Guardamos las posiciones
            for (uint32_t i = 0; i < numVertices; i++) {
                float x = r.read<float>();
                float y = r.read<float>();
                float z = r.read<float>();

                if (m == 0) {
                    DFFVertex v;
                    v.x  = x; v.y = y; v.z = z;
                    v.u  = uvs[i * 2 + 0];
                    v.v  = uvs[i * 2 + 1];
                    v.nx = 0.f; v.ny = 1.f; v.nz = 0.f;
                    v.r = colors[i*4+0]; v.g = colors[i*4+1]; v.b = colors[i*4+2]; v.a = colors[i*4+3];
                    model.vertices.push_back(v);
                }
            }

            // Leer normales si existen
            if (has_nrm && m == 0) {
                uint16_t voff = static_cast<uint16_t>(model.vertices.size() - numVertices);
                for (uint32_t i = 0; i < numVertices; i++) {
                    float nx = r.read<float>();
                    float ny = r.read<float>();
                    float nz = r.read<float>();
                    model.vertices[voff + i].nx = nx;
                    model.vertices[voff + i].ny = ny;
                    model.vertices[voff + i].nz = nz;
                }
            } else if (has_nrm) {
                r.skip(numVertices * 12);
            }
        }
        
        // El siguiente chunk DEBE ser MaterialList (0x08)
        ChunkHeader ml_hdr;
        if (r.pos + sizeof(ChunkHeader) <= geom_end) {
            ml_hdr = r.read_chunk();
            if (ml_hdr.type == 0x0008) { // RW_MATERIAL_LIST
                size_t ml_end = r.pos + ml_hdr.size;
                ChunkHeader ml_struct = r.read_chunk();
                uint32_t numMaterials = r.read<uint32_t>();
                r.pos += (ml_struct.size - 4);
                
                model.material_textures.resize(numMaterials);
                for (uint32_t i = 0; i < numMaterials && r.pos < ml_end; i++) {
                    ChunkHeader mat_hdr = r.read_chunk();
                    if (mat_hdr.type != 0x0007) { r.skip(mat_hdr.size); continue; }
                    size_t mat_end = r.pos + mat_hdr.size;
                    
                    ChunkHeader mat_struct = r.read_chunk();
                    r.skip(mat_struct.size);
                    
                    while (r.pos < mat_end) {
                        ChunkHeader th = r.read_chunk();
                        if (th.type == 0x0006) {
                            size_t tex_end = r.pos + th.size;
                            ChunkHeader ts = r.read_chunk();
                            r.skip(ts.size);
                            
                            ChunkHeader str_h = r.read_chunk();
                            if (str_h.type == 0x0002) {
                                std::string tex_name(reinterpret_cast<const char*>(r.base + r.pos), str_h.size);
                                while(!tex_name.empty() && tex_name.back() == '\0') tex_name.pop_back();
                                model.material_textures[i] = tex_name;
                                LOGI("Material %d usa textura: '%s'", i, tex_name.c_str());
                            }
                            r.pos = tex_end;
                            break;
                        } else {
                            r.skip(th.size);
                        }
                    }
                    r.pos = mat_end;
                }
            }
        }

        // Buscar Plugin de Skin dentro de las extensiones del Geometry
        while (r.pos + sizeof(ChunkHeader) <= geom_end) {
            ChunkHeader ch = r.read_chunk();
            if (ch.type == 0x0003) { // Extension
                size_t ext_end = r.pos + ch.size;
                while (r.pos + sizeof(ChunkHeader) <= ext_end) {
                    ChunkHeader plugin = r.read_chunk();
                    if (plugin.type == 0x0116) { // Skin Plugin
                        uint8_t boneCount = r.read<uint8_t>();
                        uint8_t usedBoneCount = r.read<uint8_t>();
                        uint8_t maxWeights = r.read<uint8_t>();
                        uint8_t pad = r.read<uint8_t>();
                        
                        std::vector<uint8_t> usedBoneIndices;
                        if (usedBoneCount > 0) {
                            usedBoneIndices.resize(usedBoneCount);
                            for (uint8_t i = 0; i < usedBoneCount; i++) {
                                usedBoneIndices[i] = r.read<uint8_t>();
                            }
                        }
                        
                        for (uint32_t i = 0; i < numVertices; i++) {
                            for (int j = 0; j < 4; j++) {
                                uint8_t idx = r.read<uint8_t>();
                                if (usedBoneCount > 0 && idx < usedBoneCount) {
                                    model.vertices[i].bone_indices[j] = usedBoneIndices[idx];
                                } else {
                                    model.vertices[i].bone_indices[j] = idx;
                                }
                            }
                        }
                        for (uint32_t i = 0; i < numVertices; i++) {
                            model.vertices[i].bone_weights[0] = r.read<float>();
                            model.vertices[i].bone_weights[1] = r.read<float>();
                            model.vertices[i].bone_weights[2] = r.read<float>();
                            model.vertices[i].bone_weights[3] = r.read<float>();
                        }
                        
                        // Inverse Bind Matrices (16 floats / 64 bytes per bone)
                        // NOTA: plugin.size incluye los 4 bytes de cabecera + usedBoneCount bytes ya leídos
                        uint32_t consumed_vertices = numVertices * 20;
                        uint32_t header_used = 4 + (uint32_t)usedBoneIndices.size();
                        uint32_t remaining = (plugin.size > header_used + consumed_vertices)
                            ? (plugin.size - header_used - consumed_vertices) : 0;
                        if (remaining >= (uint32_t)boneCount * 64) {
                            model.inverse_bind_matrices.resize((size_t)boneCount * 16);
                            // RW Inverse Bind Matrices son column-major-compatible en memoria si omitimos el transpose erroneo.
                            // right (x,y,z,0), up (x,y,z,0), at (x,y,z,0), pos (x,y,z,1)
                            for (uint32_t b = 0; b < boneCount; b++) {
                                float* m = &model.inverse_bind_matrices[(size_t)b * 16];
                                for (int k = 0; k < 16; k++) m[k] = r.read<float>();
                                
                                // Asegurar formato affine
                                m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
                            }
                            remaining -= (uint32_t)boneCount * 64;
                            if (boneCount == 65 || boneCount > 60) {
                                float* m0 = &model.inverse_bind_matrices[0];
                                LOGI("Skin inv_bind[0] transpuesta: [%.3f,%.3f,%.3f,%.3f / %.3f,%.3f,%.3f,%.3f / %.3f,%.3f,%.3f,%.3f / %.3f,%.3f,%.3f,%.3f]",
                                     m0[0],m0[1],m0[2],m0[3],m0[4],m0[5],m0[6],m0[7],
                                     m0[8],m0[9],m0[10],m0[11],m0[12],m0[13],m0[14],m0[15]);
                            }
                        }
                        if (remaining > 0) r.skip(remaining);
                        
                        LOGI("Skin Plugin cargado: %d huesos (used=%d), matrices guardadas.", boneCount, usedBoneCount);
                    } else {
                        r.skip(plugin.size);
                    }
                }
                r.pos = ext_end;
            } else {
                r.skip(ch.size);
            }
        }

        model.valid = true;
        r.pos = geom_end; // Avanzar al final del bloque Geometry completo
    }

    // Ajustar matrix_index según el Skin boneCount:
    // Típico en Manhunt: frames = skins + 1 (frame 0 = raíz del clump, sin skin).
    // Entonces skin_idx = frame_idx - 1. Si coinciden, skin_idx = frame_idx.
    if (!model.bones.empty() && !model.inverse_bind_matrices.empty()) {
        size_t skinCount = model.inverse_bind_matrices.size() / 16;
        size_t frameCount = model.bones.size();
        int offset = 0;
        if (frameCount == skinCount + 1) {
            offset = 1;
        } else if (frameCount == skinCount) {
            offset = 0;
        } else {
            // Heurística: si hay más frames que skins, asumir raíz extra.
            offset = (frameCount > skinCount) ? 1 : 0;
        }
        for (size_t i = 0; i < frameCount; i++) {
            int skin_idx = (int)i - offset;
            if (skin_idx >= 0 && (size_t)skin_idx < skinCount) {
                model.bones[i].matrix_index = (uint32_t)skin_idx;
            } else {
                model.bones[i].matrix_index = 0xFFFFFFFF; // sin skin (raíz)
            }
        }
        LOGI("Mapeo frames->skin: frames=%zu skins=%zu offset=%d", frameCount, skinCount, offset);
    }
    
    return model;
}


std::map<std::string, DFFModel> dff_load_archive(const uint8_t* data, size_t size) {
    std::map<std::string, DFFModel> archive;
    Reader r{data, size};
    
    while (r.pos + sizeof(ChunkHeader) <= r.total) {
        ChunkHeader hdr = r.read_chunk();
        if (hdr.type == RW_CLUMP) {
            size_t clump_end = r.pos + hdr.size;
            
            // 1. Encontrar el Node Name dentro de este Clump
            std::string clump_name = "unknown";
            // Plugin ID: 0x0253F2FE (little endian: FE F2 53 02)
            for (size_t i = 0; i < hdr.size - 4; ++i) {
                if (data[r.pos + i] == 0xFE && 
                    data[r.pos + i + 1] == 0xF2 && 
                    data[r.pos + i + 2] == 0x53 && 
                    data[r.pos + i + 3] == 0x02) {
                    
                    uint32_t str_size = *(uint32_t*)(data + r.pos + i + 4);
                    if (i + 12 + str_size <= hdr.size && str_size > 0 && str_size < 100) {
                        const char* str_ptr = (const char*)(data + r.pos + i + 12);
                        clump_name = std::string(str_ptr, str_size);
                        while(!clump_name.empty() && clump_name.back() == '\0') clump_name.pop_back();
                        break;
                    }
                }
            }
            
            // 2. Extraer el Geometry de este Clump
            DFFModel model = dff_load(r.base + r.pos, hdr.size);
            if (model.valid) {
                if (clump_name == "unknown") {
                    static int unk_idx = 0;
                    clump_name = "unknown_" + std::to_string(unk_idx++);
                }
                archive[clump_name] = model;
                LOGI("Cargado clump: %s", clump_name.c_str());
            }
            
            r.pos = clump_end;
        } else {
            r.skip(hdr.size);
        }
    }
    
    return archive;
}

