#include "dff_loader.h"
#include <cstring>
#include <android/log.h>

#define TAG "Manhunt/DFF"
#if defined(MANHUNT_VERBOSE_ASSET_LOGS)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#else
#define LOGI(...) do { } while (0)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static constexpr uint32_t RW_STRUCT        = 0x0001;
static constexpr uint32_t RW_GEOMETRY      = 0x000F;
static constexpr uint32_t RW_CLUMP         = 0x0010;
static constexpr uint32_t RW_GEOMETRYLIST  = 0x001A;
static constexpr uint32_t RW_ATOMIC       = 0x0014;

struct ChunkHeader {
    uint32_t type;
    uint32_t size;
    uint32_t version;
};

struct Reader {
    const uint8_t* base;
    size_t         total;
    size_t         pos = 0;
    bool           failed = false;

    bool can_read(size_t n) const {
        return base != nullptr && pos <= total && n <= total - pos;
    }

    template<typename T>
    T read() {
        T v{};
        if (!can_read(sizeof(T))) {
            failed = true;
            return v;
        }
        memcpy(&v, base + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }

    bool skip(size_t n) {
        if (!can_read(n)) {
            failed = true;
            pos = total;
            return false;
        }
        pos += n;
        return true;
    }

    ChunkHeader read_chunk() {
        if (!can_read(sizeof(ChunkHeader))) {
            failed = true;
            return ChunkHeader{};
        }
        return read<ChunkHeader>();
    }
};

DFFModel dff_load(const uint8_t* data, size_t size) {
    DFFModel model;
    if (data == nullptr || size < sizeof(ChunkHeader)) {
        LOGE("DFF: buffer nulo o demasiado pequeño (%zu bytes)", size);
        return model;
    }
    Reader r{data, size};
    
    // Escaneamos recursivamente buscando GEOMETRY chunks (0x0F)
    while (r.can_read(sizeof(ChunkHeader)) && !r.failed) {
        ChunkHeader hdr = r.read_chunk();
        if (r.failed || !r.can_read(hdr.size)) {
            LOGE("DFF: chunk truncado en offset %zu (tipo=0x%08X, size=%u)", r.pos, hdr.type, hdr.size);
            return DFFModel{};
        }
        
        // Si es un contenedor, leemos sus hijos (no skipeamos su payload)
        if (hdr.type == RW_CLUMP || hdr.type == RW_GEOMETRYLIST) {
            continue; 
        }
        
        if (hdr.type == 0x000E) { // RW_FRAMELIST
            size_t frame_end = r.pos + hdr.size;
            ChunkHeader fstruct = r.read_chunk();
            if (r.failed || fstruct.type != RW_STRUCT || fstruct.size < sizeof(uint32_t) ||
                r.pos > frame_end || fstruct.size > frame_end - r.pos ||
                !r.can_read(fstruct.size)) {
                LOGE("DFF: FrameList struct truncado");
                return DFFModel{};
            }
            uint32_t frameCount = r.read<uint32_t>();
            constexpr size_t kFrameRecordBytes = 56; // 9 rotation floats + 3 position floats + parent + flags
            if (r.failed || frameCount > (fstruct.size - sizeof(uint32_t)) / kFrameRecordBytes) {
                LOGE("DFF: frameCount inválido (%u)", frameCount);
                return DFFModel{};
            }

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
            
            // HAnim no usa el nodeIndex escrito en el stream para
            // asociar el hueso con el Frame. La implementación de RenderWare
            // adjunta cada HAnim node a su Frame buscando el mismo nodeID.
            // Por eso primero guardamos el orden de nodos de la jerarquía y
            // después resolvemos nodeID -> FrameList index.
            std::vector<uint32_t> hanim_node_ids;
            std::vector<uint32_t> hanim_node_flags;

            // Extensions
            for (uint32_t i = 0; i < frameCount; i++) {
                ChunkHeader ext = r.read_chunk();
                if (r.failed || r.pos > frame_end || ext.size > frame_end - r.pos ||
                    !r.can_read(ext.size)) {
                    LOGE("DFF: extension de frame truncada");
                    return DFFModel{};
                }
                size_t ext_end = r.pos + ext.size;

                while (r.pos <= ext_end && sizeof(ChunkHeader) <= ext_end - r.pos && r.can_read(sizeof(ChunkHeader)) && !r.failed) {
                    ChunkHeader plug = r.read_chunk();
                    if (r.failed || r.pos > ext_end || plug.size > ext_end - r.pos ||
                        !r.can_read(plug.size)) {
                        LOGE("DFF: plugin de frame truncado");
                        return DFFModel{};
                    }
                    size_t plug_payload_start = r.pos;
                    size_t plug_payload_end =
                        plug_payload_start + plug.size;

                    if (plug.type == 0x011E) { // HAnim
                        uint32_t ver = r.read<uint32_t>();
                        uint32_t hanim_id = r.read<uint32_t>();
                        uint32_t nodeCount = r.read<uint32_t>();

                        if (nodeCount > 0) {
                            r.read<uint32_t>(); // flags
                            r.read<uint32_t>(); // keyFrameSize

                            hanim_node_ids.reserve(nodeCount);

                            for (uint32_t n = 0;
                                 n < nodeCount;
                                 ++n) {
                                const uint32_t nodeId =
                                    r.read<uint32_t>();
                                r.read<uint32_t>(); // nodeIndex: RW lo resuelve por ID
                                const uint32_t flags =
                                    r.read<uint32_t>();

                                hanim_node_ids.push_back(nodeId);
                                hanim_node_flags.push_back(flags);
                            }
                        } else {
                            // HAnim Node individual del Frame.
                            // Este es el ID que identifica el Frame.
                            if (i < model.bones.size()) {
                                model.bones[i].bone_id = hanim_id;
                            }
                        }
                    }

                    r.pos = plug_payload_end;
                }

                r.pos = ext_end;
            }

            // HAnim hierarchy order -> FrameList index.
            // Skin inverseMatrices[i] y vertexBoneIndices[] usan el
            // mismo índice i; ese i pertenece al orden de HAnim.
            model.skin_bone_to_frame.clear();
            model.skin_bone_to_frame.reserve(hanim_node_ids.size());

            model.hanim_node_flags.clear();
            model.hanim_node_flags.reserve(hanim_node_ids.size());

            size_t mapped_hanim_nodes = 0;

            for (size_t h = 0;
                 h < hanim_node_ids.size();
                 ++h) {
                const uint32_t nodeId =
                    hanim_node_ids[h];

                size_t frame_index = SIZE_MAX;

                for (size_t frame = 0;
                     frame < model.bones.size();
                     ++frame) {
                    if (model.bones[frame].bone_id == nodeId) {
                        frame_index = frame;
                        break;
                    }
                }

                if (frame_index != SIZE_MAX &&
                    frame_index <= 0xFE) {
                    model.skin_bone_to_frame.push_back(
                        static_cast<uint8_t>(frame_index)
                    );
                    model.bone_id_to_index[nodeId] =
                        static_cast<uint32_t>(frame_index);
                    model.hanim_node_flags.push_back(
                        h < hanim_node_flags.size()
                            ? hanim_node_flags[h]
                            : 0u
                    );
                    ++mapped_hanim_nodes;
                } else {
                    // Mantener el índice HAnim, pero marcar el Frame como
                    // inválido. Conservamos también sus flags.
                    model.skin_bone_to_frame.push_back(0xFF);
                    model.hanim_node_flags.push_back(
                        h < hanim_node_flags.size()
                            ? hanim_node_flags[h]
                            : 0u
                    );
                }
            }

            // matrix_index representa el índice LOCAL del Skin/HAnim,
            // no el FrameList index.
            for (uint32_t i = 0; i < frameCount; i++) {
                model.bones[i].matrix_index = i;
            }

            LOGI(
                "FrameList: %u frames, HAnim nodes=%zu mapped=%zu flags=%zu",
                frameCount,
                hanim_node_ids.size(),
                mapped_hanim_nodes,
                model.hanim_node_flags.size()
            );
            
            r.pos = frame_end;
            continue;
        }

        if (hdr.type == RW_ATOMIC) {
            const size_t atomic_end =
                r.pos + hdr.size;

            if (r.pos <= atomic_end && sizeof(ChunkHeader) <= atomic_end - r.pos && r.can_read(sizeof(ChunkHeader))) {
                const ChunkHeader astruct =
                    r.read_chunk();

                if (astruct.type == RW_STRUCT &&
                    r.pos + 16 <= atomic_end) {
                    const uint32_t frame_index =
                        r.read<uint32_t>();

                    // geometryIndex + flags + unused
                    r.read<uint32_t>();
                    r.read<uint32_t>();
                    r.read<uint32_t>();

                    if (model.atomic_frame_index ==
                        0xFFFFFFFF) {
                        model.atomic_frame_index =
                            frame_index;
                    }

                    // Evitar inundar Logcat al cargar muchos Atomics.
                    static uint32_t atomic_log_count = 0;
                    if (atomic_log_count < 8) {
                        LOGI(
                            "Atomic: frameIndex=%u",
                            frame_index
                        );
                        ++atomic_log_count;
                    }
                }
            }

            r.pos = atomic_end;
            continue;
        }

        if (hdr.type != RW_GEOMETRY) {
            r.skip(hdr.size);
            continue;
        }
        
        size_t geom_end = r.pos + hdr.size;
        
        // El primer chunk de Geometry es el Struct con la info principal
        ChunkHeader sh = r.read_chunk();
        if (r.failed || r.pos > geom_end || sh.size > geom_end - r.pos ||
            !r.can_read(sh.size)) {
            LOGE("DFF: struct de Geometry truncado");
            return DFFModel{};
        }
        if (sh.type != RW_STRUCT) {
            LOGE("Geometry sin Struct interno, skipeando");
            r.pos = geom_end;
            continue;
        }
        if (sh.size < 4u * sizeof(uint32_t)) {
            LOGE("DFF: struct de Geometry demasiado pequeño (%u bytes)", sh.size);
            return DFFModel{};
        }

        uint32_t format = r.read<uint32_t>();
        uint32_t numTriangles = r.read<uint32_t>();
        uint32_t numVertices = r.read<uint32_t>();
        uint32_t numMorphTargets = r.read<uint32_t>();
        if (r.failed || r.pos > geom_end) {
            LOGE("DFF: cabecera de Geometry truncada");
            return DFFModel{};
        }
        const size_t geometry_remaining = geom_end - r.pos;
        if (numTriangles > geometry_remaining / 8u ||
            numVertices > geometry_remaining / 12u ||
            numMorphTargets > geometry_remaining / 24u) {
            LOGE("DFF: conteos fuera de límites (tris=%u verts=%u morph=%u, restantes=%zu)",
                 numTriangles, numVertices, numMorphTargets, geometry_remaining);
            return DFFModel{};
        }

        LOGI("Geometry: %d tris, %d verts, format 0x%X", numTriangles, numVertices, format);
        
        // Banderas (flags) del formato
        bool has_colors = (format & 0x08) != 0;
        bool has_tex    = (format & 0x04) != 0;
        bool has_norms  = (format & 0x10) != 0;
        
        // Omitimos los pre-lit colors si existen (RGBA)
        const size_t color_bytes = static_cast<size_t>(numVertices) * 4u;
        if (color_bytes > geom_end - r.pos) {
            LOGE("DFF: colores fuera de límites del Geometry");
            return DFFModel{};
        }
        std::vector<uint8_t> colors(color_bytes, 255);
        if (has_colors) {
            if (!r.can_read(color_bytes)) {
                LOGE("DFF: colores truncados");
                return DFFModel{};
            }
            memcpy(colors.data(), r.base + r.pos, color_bytes);
            if (!r.skip(color_bytes)) return DFFModel{};
        }
        
        // Leemos los UVs (TexCoords)
        int num_uv_sets = (format & 0x00FF0000) >> 16;
        if (num_uv_sets == 0) num_uv_sets = has_tex ? 1 : 0;
        
        const size_t uv_set_bytes = static_cast<size_t>(numVertices) * 8u;
        if (num_uv_sets > 0 &&
            (uv_set_bytes > static_cast<size_t>(-1) / static_cast<size_t>(num_uv_sets) ||
             uv_set_bytes * static_cast<size_t>(num_uv_sets) > geom_end - r.pos)) {
            LOGE("DFF: UVs fuera de límites del Geometry");
            return DFFModel{};
        }
        std::vector<float> uvs(static_cast<size_t>(numVertices) * 2u, 0.0f);
        if (num_uv_sets > 0) {
            if (!r.can_read(uv_set_bytes)) {
                LOGE("DFF: primer set UV truncado");
                return DFFModel{};
            }
            memcpy(uvs.data(), r.base + r.pos, uv_set_bytes);
            if (!r.skip(uv_set_bytes * static_cast<size_t>(num_uv_sets))) {
                LOGE("DFF: sets UV truncados");
                return DFFModel{};
            }
        }
        
        // Leemos los triángulos (4 uint16_t por cara: v2, v1, materialId, v3)
        uint32_t vertex_offset = static_cast<uint32_t>(model.vertices.size());
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
                    DFFVertex v{};
                    v.x  = x; v.y = y; v.z = z;
                    v.u  = uvs[i * 2 + 0];
                    v.v  = uvs[i * 2 + 1];
                    v.nx = 0.f; v.ny = 1.f; v.nz = 0.f;
                    v.r = colors[i*4+0]; v.g = colors[i*4+1]; v.b = colors[i*4+2]; v.a = colors[i*4+3];
                    v.bone_indices[0] = 0; v.bone_indices[1] = 0; v.bone_indices[2] = 0; v.bone_indices[3] = 0;
                    v.bone_weights[0] = 1.0f; v.bone_weights[1] = 0.0f; v.bone_weights[2] = 0.0f; v.bone_weights[3] = 0.0f;
                    model.vertices.push_back(v);
                }
            }

            // Leer normales si existen
            if (has_nrm && m == 0) {
                uint32_t voff = static_cast<uint32_t>(model.vertices.size() - numVertices);
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
        if (r.pos <= geom_end && sizeof(ChunkHeader) <= geom_end - r.pos) {
            ml_hdr = r.read_chunk();
            if (r.failed || r.pos > geom_end || ml_hdr.size > geom_end - r.pos ||
                !r.can_read(ml_hdr.size)) {
                LOGE("DFF: MaterialList truncado");
                return DFFModel{};
            }
            if (ml_hdr.type == 0x0008) { // RW_MATERIAL_LIST
                size_t ml_end = r.pos + ml_hdr.size;
                ChunkHeader ml_struct = r.read_chunk();
                if (r.failed || ml_struct.size < sizeof(uint32_t) ||
                    r.pos > ml_end || ml_struct.size > ml_end - r.pos ||
                    !r.can_read(ml_struct.size)) {
                    LOGE("DFF: struct de MaterialList truncado");
                    return DFFModel{};
                }
                uint32_t numMaterials = r.read<uint32_t>();
                if (r.failed || numMaterials > (ml_struct.size - sizeof(uint32_t)) / sizeof(uint32_t)) {
                    LOGE("DFF: numMaterials inválido (%u)", numMaterials);
                    return DFFModel{};
                }
                r.skip(ml_struct.size - sizeof(uint32_t));
                
                model.material_textures.resize(numMaterials);
                for (uint32_t i = 0; i < numMaterials && r.pos < ml_end; i++) {
                    ChunkHeader mat_hdr = r.read_chunk();
                    if (r.failed || r.pos > ml_end || mat_hdr.size > ml_end - r.pos ||
                        !r.can_read(mat_hdr.size)) {
                        LOGE("DFF: material %u truncado", i);
                        return DFFModel{};
                    }
                    if (mat_hdr.type != 0x0007) { r.skip(mat_hdr.size); continue; }
                    size_t mat_end = r.pos + mat_hdr.size;
                    model.materials.resize(numMaterials);
                    ChunkHeader mat_struct = r.read_chunk();
                    if (r.failed || r.pos > mat_end || mat_struct.size > mat_end - r.pos ||
                        !r.can_read(mat_struct.size)) {
                        LOGE("DFF: struct de material %u truncado", i);
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
                            LOGE("DFF: texture chunk truncado");
                            return DFFModel{};
                        }
                        if (th.type == 0x0006) {
                            size_t tex_end = r.pos + th.size;
                            ChunkHeader ts = r.read_chunk();
                            if (r.failed || r.pos > tex_end || ts.size > tex_end - r.pos ||
                                !r.can_read(ts.size)) {
                                LOGE("DFF: texture struct truncado");
                                return DFFModel{};
                            }
                            r.skip(ts.size);

                            ChunkHeader str_h = r.read_chunk();
                            if (r.failed || r.pos > tex_end || str_h.size > tex_end - r.pos ||
                                !r.can_read(str_h.size)) {
                                LOGE("DFF: nombre de textura truncado");
                                return DFFModel{};
                            }
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
        while (r.pos <= geom_end && sizeof(ChunkHeader) <= geom_end - r.pos && r.can_read(sizeof(ChunkHeader)) && !r.failed) {
            ChunkHeader ch = r.read_chunk();
            if (ch.type == 0x0003) { // Extension
                size_t ext_end = r.pos + ch.size;
                while (r.pos <= ext_end && sizeof(ChunkHeader) <= ext_end - r.pos && r.can_read(sizeof(ChunkHeader)) && !r.failed) {
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

                        // El índice almacenado por vértice es LOCAL al
                        // Skin plugin. No lo sustituimos por el frame index:
                        // esa conversión se aplica al construir la palette
                        // de skin, donde también se relaciona con la
                        // inverse-bind matrix del mismo Skin bone.
                        size_t vertex_offset = model.vertices.size() - numVertices;
                        for (uint32_t i = 0; i < numVertices; i++) {
                            for (int j = 0; j < 4; j++) {
                                const uint8_t idx = r.read<uint8_t>();
                                model.vertices[vertex_offset + i].bone_indices[j] = idx;
                            }
                        }

                        // usedBoneIndices es la lista de huesos usados
                        // por esta geometría; NO es el mapeo HAnim -> Frame.
                        for (uint32_t i = 0; i < numVertices; i++) {
                            model.vertices[vertex_offset + i].bone_weights[0] = r.read<float>();
                            model.vertices[vertex_offset + i].bone_weights[1] = r.read<float>();
                            model.vertices[vertex_offset + i].bone_weights[2] = r.read<float>();
                            model.vertices[vertex_offset + i].bone_weights[3] = r.read<float>();
                        }
                        
                        // Inverse Bind Matrices (16 floats / 64 bytes per bone)
                        // NOTA: plugin.size incluye los 4 bytes de cabecera + usedBoneCount bytes ya leídos
                        uint32_t consumed_vertices = numVertices * 20;
                        uint32_t header_used = 4 + (uint32_t)usedBoneIndices.size();
                        uint32_t remaining = (plugin.size > header_used + consumed_vertices)
                            ? (plugin.size - header_used - consumed_vertices) : 0;
                        if (remaining >= (uint32_t)boneCount * 64) {
                            model.inverse_bind_matrices.resize((size_t)boneCount * 16);

                            // RenderWare guarda RwMatrix como cuatro
                            // vectores consecutivos: right, up, at, pos.
                            // Nuestra Mat4 también usa esas cuatro columnas,
                            // por lo que NO hay que transponer.
                            for (uint32_t b = 0; b < boneCount; b++) {
                                float* m =
                                    &model.inverse_bind_matrices[
                                        static_cast<size_t>(b) * 16
                                    ];

                                for (int k = 0; k < 16; ++k) {
                                    m[k] = r.read<float>();
                                }

                                // El último componente de right/up/at debe
                                // ser 0 y el de pos debe ser 1.
                                m[3]  = 0.0f;
                                m[7]  = 0.0f;
                                m[11] = 0.0f;
                                m[15] = 1.0f;
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
                        
                        LOGI(
                            "Skin Plugin cargado: bones=%d used=%d matrices=%zu remap=%zu",
                            boneCount,
                            usedBoneCount,
                            model.inverse_bind_matrices.size() / 16,
                            model.skin_bone_to_frame.size()
                        );
                    } else {
                        r.skip(plugin.size);
                    }
                }
                r.pos = ext_end;
            } else {
                r.skip(ch.size);
            }
        }

        if (r.failed) {
            LOGE("DFF: lectura fuera de límites; se descarta la geometría");
            return DFFModel{};
        }
        model.valid = true;
        r.pos = geom_end; // Avanzar al final del bloque Geometry completo
    }

    // matrix_index ya fue asignado usando el HAnim Hierarchy plugin
    // Validar con el boneCount del Skin Plugin:
    if (!model.inverse_bind_matrices.empty()) {
        size_t skinCount = model.inverse_bind_matrices.size() / 16;
        size_t framesWithSkin = 0;
        for (const auto& b : model.bones) {
            if (b.matrix_index != 0xFFFFFFFF) framesWithSkin++;
        }
        LOGI("Validacion Skin: %zu matrices de skin. %zu frames mapeados a skin.", skinCount, framesWithSkin);
    }
    // Normalizar pesos y eliminar referencias a matrices inexistentes.
    const size_t skinCount = model.inverse_bind_matrices.size() / 16;
    for (auto& v : model.vertices) {
        for (int j = 0; j < 4; ++j) {
            if (skinCount == 0 ||
                static_cast<size_t>(v.bone_indices[j]) >= skinCount) {
                v.bone_indices[j] = 0;
                v.bone_weights[j] = 0.0f;
            }
        }

        float sum = v.bone_weights[0] + v.bone_weights[1] + v.bone_weights[2] + v.bone_weights[3];
        if (sum > 0.0f) {
            if (std::abs(sum - 1.0f) > 0.001f) {
                v.bone_weights[0] /= sum;
                v.bone_weights[1] /= sum;
                v.bone_weights[2] /= sum;
                v.bone_weights[3] /= sum;
            }
        } else {
            v.bone_weights[0] = 1.0f;
            v.bone_indices[0] = 0;
            v.bone_weights[1] = 0.0f;
            v.bone_weights[2] = 0.0f;
            v.bone_weights[3] = 0.0f;
        }
    }
    
    return model;
}


std::map<std::string, DFFModel> dff_load_archive(const uint8_t* data, size_t size) {
    std::map<std::string, DFFModel> archive;
    if (data == nullptr || size < sizeof(ChunkHeader)) {
        LOGE("DFF archive: buffer nulo o demasiado pequeño (%zu bytes)", size);
        return archive;
    }
    Reader r{data, size};
    
    while (r.can_read(sizeof(ChunkHeader)) && !r.failed) {
        ChunkHeader hdr = r.read_chunk();
        if (r.failed || !r.can_read(hdr.size)) {
            LOGE("DFF archive: chunk truncado en offset %zu (tipo=0x%08X, size=%u)", r.pos, hdr.type, hdr.size);
            return {};
        }
        if (hdr.type == RW_CLUMP) {
            size_t clump_end = r.pos + hdr.size;
            
            // 1. Encontrar el Node Name dentro de este Clump
            std::string clump_name = "unknown";
            // Plugin ID: 0x0253F2FE (little endian: FE F2 53 02)
            for (size_t i = 0; hdr.size >= 12 && i <= hdr.size - 12; ++i) {
                if (data[r.pos + i] == 0xFE &&
                    data[r.pos + i + 1] == 0xF2 &&
                    data[r.pos + i + 2] == 0x53 &&
                    data[r.pos + i + 3] == 0x02) {

                    uint32_t str_size = 0;
                    std::memcpy(&str_size, data + r.pos + i + 4, sizeof(str_size));
                    if (str_size > 0 && str_size < 100 && str_size <= hdr.size - (i + 12)) {
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

