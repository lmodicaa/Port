// native-lib.cpp — Manhunt Android port
// target: Android NDK r25+, OpenGL ES 3.0, C++17

#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include <ctime>
#include "txd_loader.h"
#include "dff_loader.h"
#include "inst_loader.h"
#include "ifp_loader.h"

#define LOG_TAG "Manhunt"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Matemáticas ───────────────────────────────────────────────────────────────
struct Vec3 { float x, y, z; };

static Vec3 vec3_add(Vec3 a, Vec3 b)   { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static Vec3 vec3_scale(Vec3 a, float s){ return {a.x*s, a.y*s, a.z*s}; }
static float vec3_dot(Vec3 a, Vec3 b)  { return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
static Vec3 vec3_norm(Vec3 a) {
    float l = sqrtf(vec3_dot(a, a));
    if (l < 1e-6f) return {0,1,0};
    return {a.x/l, a.y/l, a.z/l};
}

struct Mat4 { float m[16]; };

static Mat4 mat4_identity() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

static Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 r = {0};
    for(int c=0;c<4;++c)
        for(int row=0;row<4;++row)
            for(int k=0;k<4;++k)
                r.m[c*4+row] += a.m[k*4+row] * b.m[c*4+k];
    return r;
}

static Mat4 mat4_perspective(float fovY, float aspect, float nearZ, float farZ) {
    float f = 1.0f / tanf(fovY * 0.5f);
    Mat4 r = {0};
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (farZ + nearZ) / (nearZ - farZ);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    return r;
}

// Construye view matrix FPS a partir de posicion + yaw + pitch
[[maybe_unused]] static Mat4 mat4_fps_view(Vec3 pos, float yaw, float pitch) {
    float cy = cosf(yaw),   sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);

    // forward = dir que mira la camara
    Vec3 fwd = { -sy*cp, sp, -cy*cp };
    Vec3 right = vec3_norm(vec3_cross(fwd, {0,1,0}));
    Vec3 up    = vec3_cross(right, fwd);

    Mat4 r = {0};
    r.m[0]  = right.x; r.m[4]  = right.y; r.m[8]  = right.z;
    r.m[1]  = up.x;    r.m[5]  = up.y;    r.m[9]  = up.z;
    r.m[2]  = -fwd.x;  r.m[6]  = -fwd.y;  r.m[10] = -fwd.z;
    r.m[15] = 1.0f;
    // Translation = -R * pos
    r.m[12] = -(right.x*pos.x + right.y*pos.y + right.z*pos.z);
    r.m[13] = -(up.x*pos.x    + up.y*pos.y    + up.z*pos.z);
    r.m[14] =  (fwd.x*pos.x   + fwd.y*pos.y   + fwd.z*pos.z);
    return r;
}

static Mat4 mat4_from_pos_quat(const float* pos, const float* rot) {
    float x = rot[0], y = rot[1], z = rot[2], w = rot[3];
    Mat4 r = {0};
    r.m[0] = 1.0f - 2.0f*(y*y + z*z); r.m[1] = 2.0f*(x*y + z*w);      r.m[2] = 2.0f*(x*z - y*w);      r.m[3] = 0;
    r.m[4] = 2.0f*(x*y - z*w);      r.m[5] = 1.0f - 2.0f*(x*x + z*z); r.m[6] = 2.0f*(y*z + x*w);      r.m[7] = 0;
    r.m[8] = 2.0f*(x*z + y*w);      r.m[9] = 2.0f*(y*z - x*w);      r.m[10]= 1.0f - 2.0f*(x*x + y*y); r.m[11]= 0;
    r.m[12]= pos[0];                r.m[13]= pos[1];                r.m[14]= pos[2];                r.m[15]= 1.0f;
    return r;
}

static Mat4 mat4_from_pos_yaw(Vec3 pos, float yaw) {
    Mat4 r = {0};
    float c = cosf(yaw), s = sinf(yaw);
    r.m[0] = c;  r.m[1] = 0; r.m[2] = -s; r.m[3] = 0;
    r.m[4] = 0;  r.m[5] = 1; r.m[6] = 0;  r.m[7] = 0;
    r.m[8] = s;  r.m[9] = 0; r.m[10]= c;  r.m[11]= 0;
    r.m[12]= pos.x; r.m[13]= pos.y; r.m[14]= pos.z; r.m[15]= 1.0f;
    return r;
}


static Vec3 mat4_transform_point(const Mat4& m, Vec3 p) {
    return {
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    };
}

// Altura mínima del modelo Cash después de aplicar su transformación de ejes.
// Se usa para que el punto más bajo del mesh quede justo sobre el piso.
static float cash_model_min_y(const Mat4& rotation_m) {
    if (g_cash_model.vertices.empty()) return 0.0f;

    float min_y = 1e30f;
    for (const auto& v : g_cash_model.vertices) {
        const Vec3 p = mat4_transform_point(
            rotation_m,
            {v.x, v.y, v.z}
        );
        min_y = std::min(min_y, p.y);
    }

    return min_y;
}

static Mat4 mat4_from_pos_cash(Vec3 pos, float yaw) {
    // Baseline established during visual calibration:
    // Cash needs a Z-up -> Y-up axis conversion plus the runtime yaw.
    const float cy = cosf(yaw);
    const float sy = sinf(yaw);

    Mat4 r = {0};
    r.m[0] = cy;
    r.m[1] = 0.0f;
    r.m[2] = -sy;
    r.m[3] = 0.0f;

    r.m[4] = -sy;
    r.m[5] = 0.0f;
    r.m[6] = -cy;
    r.m[7] = 0.0f;

    r.m[8] = 0.0f;
    r.m[9] = 1.0f;
    r.m[10] = 0.0f;
    r.m[11] = 0.0f;

    r.m[12] = pos.x;
    r.m[13] = pos.y;
    r.m[14] = pos.z;
    r.m[15] = 1.0f;
    return r;
}

static Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = {center.x - eye.x, center.y - eye.y, center.z - eye.z};
    float flen = sqrtf(f.x*f.x + f.y*f.y + f.z*f.z);
    if(flen>0.0f){ f.x/=flen; f.y/=flen; f.z/=flen; }
    
    Vec3 s = vec3_cross(f, up);
    float slen = sqrtf(s.x*s.x + s.y*s.y + s.z*s.z);
    if(slen>0.0f){ s.x/=slen; s.y/=slen; s.z/=slen; }
    
    Vec3 u = vec3_cross(s, f);
    
    Mat4 r = {0};
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] =-f.x; r.m[6] =-f.y; r.m[10]=-f.z;
    r.m[12] = -(s.x*eye.x + s.y*eye.y + s.z*eye.z);
    r.m[13] = -(u.x*eye.x + u.y*eye.y + u.z*eye.z);
    r.m[14] =  (f.x*eye.x + f.y*eye.y + f.z*eye.z);
    r.m[15] = 1.0f;
    return r;
}

// ── Estado global ─────────────────────────────────────────────────────────────
struct RenderGroup {
    GLuint ebo;
    int num_indices;
    GLuint texture_id;
    MaterialData material;
};
static WorldLighting g_world_lighting;

static AAssetManager* g_assets   = nullptr;
static std::string    g_base_path = "";
static GLuint g_program           = 0;
static GLuint g_vao               = 0;
static std::map<std::string, GLuint> g_tex_map;
static std::map<std::string, Animation> g_anims;
// Debug de animaciones:
// -2 = modo automático según movimiento
// -1 = bind pose (sin IFP)
// >=0 = índice dentro de g_debug_anim_list
static int g_debug_anim_idx = -1;
static std::vector<const Animation*> g_debug_anim_list;
static bool g_cash_skinning_enabled = false;

// Forward declaration: setup_model() may use this diagnostic helper.
static void dump_cash_debug(const DFFModel& model);

static std::string to_lower(std::string s) {
    for (char& c : s) c = tolower((unsigned char)c);
    return s;
}

static GLuint get_texture(const std::string& name) {
    if (name.empty()) return 0;
    auto it = g_tex_map.find(to_lower(name));
    return (it != g_tex_map.end()) ? it->second : 0;
}

static void rebuild_debug_animation_list() {
    g_debug_anim_list.clear();
    g_debug_anim_list.reserve(g_anims.size());

    for (const auto& pair : g_anims) {
        if (!pair.second.tracks.empty()) {
            g_debug_anim_list.push_back(&pair.second);
        }
    }

    LOGI("IFP DEBUG: %zu animaciones con tracks", g_debug_anim_list.size());
    for (size_t i = 0; i < g_debug_anim_list.size(); ++i) {
        const Animation* anim = g_debug_anim_list[i];
        LOGI("IFP DEBUG [%zu] %s duration=%.3f tracks=%zu",
             i,
             anim->name.c_str(),
             anim->duration,
             anim->tracks.size());
    }
}

static void dump_cash_debug(const DFFModel& model) {
    size_t weighted_vertices = 0;
    size_t zero_weight_vertices = 0;
    size_t normalized_vertices = 0;
    uint32_t max_bone_index = 0;
    float min_weight_sum = 1e30f;
    float max_weight_sum = -1e30f;

    for (const auto& v : model.vertices) {
        const float sum =
            v.bone_weights[0] +
            v.bone_weights[1] +
            v.bone_weights[2] +
            v.bone_weights[3];

        if (sum > 0.000001f) {
            ++weighted_vertices;
            min_weight_sum = std::min(min_weight_sum, sum);
            max_weight_sum = std::max(max_weight_sum, sum);

            if (std::fabs(sum - 1.0f) <= 0.001f) {
                ++normalized_vertices;
            }
        } else {
            ++zero_weight_vertices;
        }

        for (int j = 0; j < 4; ++j) {
            max_bone_index = std::max(
                max_bone_index,
                static_cast<uint32_t>(v.bone_indices[j])
            );
        }
    }

    if (weighted_vertices == 0) {
        min_weight_sum = 0.0f;
        max_weight_sum = 0.0f;
    }

    size_t unmapped_bones = 0;
    for (const auto& bone : model.bones) {
        if (bone.bone_id == 0xFFFFFFFF ||
            bone.matrix_index == 0xFFFFFFFF) {
            ++unmapped_bones;
        }
    }

    const size_t skin_matrices =
        model.inverse_bind_matrices.size() / 16;

    LOGI(
        "CASH DEBUG: vertices=%zu bones=%zu skinMatrices=%zu "
        "weighted=%zu zeroWeight=%zu normalized=%zu "
        "unmappedBones=%zu maxBoneIndex=%u weightSumMin=%.5f "
        "weightSumMax=%.5f",
        model.vertices.size(),
        model.bones.size(),
        skin_matrices,
        weighted_vertices,
        zero_weight_vertices,
        normalized_vertices,
        unmapped_bones,
        max_bone_index,
        min_weight_sum,
        max_weight_sum
    );
}
static std::vector<RenderGroup>  g_groups;
static int    g_width             = 0;
static int    g_height            = 0;

// Estado 3ra Persona
static Vec3  g_player_pos = {0.f, -5.f, 0.f};
static float g_cash_y_offset = 1.0f;

// El modelo Cash usa un eje frontal distinto al del runtime:
// el frente del DFF debe girarse 90 grados para alinearlo con
// el forward del jugador (-Z en yaw=0).
static constexpr float CASH_MODEL_YAW_OFFSET = -1.57079632679f;
// Calibración final de orientación obtenida con el menú de ajuste.
// La altura vertical NO se fija aquí: se calcula automáticamente contra el suelo.
static Vec3 g_cash_pos_adjust = {0.0f, 0.0f, 0.0f};
static Vec3 g_cash_rot_adjust_deg = {0.0f, -91.0f, 180.0f};
static constexpr float CASH_FOOT_CLEARANCE = 0.02f;

static Mat4 mat4_rotate_x(float angle) {
    const float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

static Mat4 mat4_rotate_y(float angle) {
    const float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

static Mat4 mat4_rotate_z(float angle) {
    const float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}
static DFFModel g_cash_model;
static float g_player_yaw = 0.0f;
static float g_anim_time = 0.f;
static float g_cam_yaw    = 0.0f;
static float g_cam_pitch  = -0.2f;
static float g_cam_dist   = 3.0f;

// Movimiento joystick (actualizados desde Kotlin)
static float g_move_fwd   = 0.f;  // -1..1  (adelante/atrás)
static float g_move_right = 0.f;  // -1..1  (izquierda/derecha)

// Física / colisión
static float  g_vel_y    = 0.f;
static bool   g_on_ground= false;
static const float GRAVITY        = -20.0f;
static const float PLAYER_HEIGHT  =  1.8f;
static const float MOVE_SPEED     = 10.0f;

// Triángulos del BSP para colisión de suelo
struct Tri { Vec3 a, b, c; };

struct CollisionGrid {
    float cell_size = 5.0f;
    std::map<std::pair<int, int>, std::vector<Tri>> cells;
    
    void add(const Tri& t) {
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
    
    const std::vector<Tri>* get(float x, float z) const {
        int cx = (int)std::floor(x / cell_size);
        int cz = (int)std::floor(z / cell_size);
        auto it = cells.find({cx, cz});
        if (it != cells.end()) return &it->second;
        return nullptr;
    }
};

static CollisionGrid g_col_grid;
static std::vector<EntityInst> g_insts;
#include <map>
static std::map<std::string, DFFModel> g_models;

struct ModelRenderData {
    GLuint vao = 0;
    GLuint vbo = 0;
    std::vector<RenderGroup> groups;
};
static std::map<std::string, ModelRenderData> g_model_render;

static void upload_dff_to_gpu(const DFFModel& model, ModelRenderData& rd) {
    glGenVertexArrays(1, &rd.vao);
    glBindVertexArray(rd.vao);

    glGenBuffers(1, &rd.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
    glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(DFFVertex), model.vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, u));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, r));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, bone_indices));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, bone_weights));
    glEnableVertexAttribArray(5);

    for (size_t i = 0; i < model.indices_by_mat.size(); i++) {
        if (model.indices_by_mat[i].empty()) continue;
        RenderGroup g;
        glGenBuffers(1, &g.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices_by_mat[i].size() * sizeof(uint32_t), model.indices_by_mat[i].data(), GL_STATIC_DRAW);
        g.num_indices = model.indices_by_mat[i].size();
        
        std::string tex_name = "";
        if (i < model.material_textures.size()) tex_name = model.material_textures[i];
        g.texture_id = get_texture(tex_name);
        if (i < model.materials.size()) g.material = model.materials[i];
        
        rd.groups.push_back(g);
    }
    glBindVertexArray(0);
}

// ── Shaders ───────────────────────────────────────────────────────────────────
static const char* VERT_SRC = R"(#version 300 es
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec3 a_normal;
layout(location=3) in vec4 a_color;
layout(location=4) in uvec4 a_bone_idx;
layout(location=5) in vec4 a_bone_weight;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat4 u_bone_matrices[96];
uniform int u_skinned;

out vec2  v_uv;
out vec4  v_color;
out float v_dist;
out vec3  v_normal;

void main() {
    vec4 local_pos = vec4(a_pos, 1.0);
    vec3 local_normal = a_normal;
    
    if (u_skinned == 1) {
        mat4 bone_transform =
            u_bone_matrices[clamp(int(a_bone_idx.x), 0, 95)] * a_bone_weight.x +
            u_bone_matrices[clamp(int(a_bone_idx.y), 0, 95)] * a_bone_weight.y +
            u_bone_matrices[clamp(int(a_bone_idx.z), 0, 95)] * a_bone_weight.z +
            u_bone_matrices[clamp(int(a_bone_idx.w), 0, 95)] * a_bone_weight.w;
        local_pos = bone_transform * local_pos;
        local_normal = mat3(bone_transform) * local_normal;
    }
    
    vec4 pos = u_mvp * local_pos;
    gl_Position = pos;
    v_uv = a_uv;
    v_color = a_color;
    v_dist = pos.w;
    v_normal = normalize(mat3(u_model) * local_normal);
})";

// Fragment: texturas + colores de vértices + niebla negra (Manhunt style)
static const char* FRAG_SRC = R"(#version 300 es
precision mediump float;
in vec2  v_uv;
in vec4  v_color;
in float v_dist;
in vec3  v_normal;

uniform sampler2D u_tex;
uniform int u_has_tex;

uniform vec4 u_world_ambient;
uniform vec4 u_dir_ambient;
uniform vec3 u_light_dir;

uniform vec4 u_mat_color;
uniform float u_mat_ambient;
uniform float u_mat_diffuse;

out vec4 frag_color;

void main() {
    vec4 tex_color = vec4(1.0);
    if (u_has_tex == 1) {
        tex_color = texture(u_tex, v_uv);
    }
    // Render estable: no usar todavía los datos de iluminación del BSP,
    // porque el parser del RW_WORLD aún no está verificado.
    vec4 base = tex_color * v_color;

    if (base.a < 0.1) discard;

    float fog_start = 10.0;
    float fog_end = 45.0;
    float fog_factor = clamp(
        (fog_end - v_dist) / (fog_end - fog_start),
        0.0,
        1.0
    );

    vec3 final_color = base.rgb;
    vec3 fog_color = vec3(0.0);

    frag_color = vec4(
        mix(fog_color, final_color, fog_factor),
        base.a
    );
})";

// ── Helpers ───────────────────────────────────────────────────────────────────
static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        LOGE("Shader error: %s", log);
        glDeleteShader(s); return 0;
    }
    return s;
}

static std::vector<uint8_t> read_asset(const char* path) {
    if (!g_base_path.empty()) {
        std::string full_path = g_base_path + "/" + path;
        std::ifstream file(full_path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(size);
            if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
                LOGI("Archivo cargado externamente: %s (%zu bytes)", full_path.c_str(), size);
                return buffer;
            }
        }
    }
    if (!g_assets) return {};
    AAsset* a = AAssetManager_open(g_assets, path, AASSET_MODE_BUFFER);
    if (!a) { LOGE("Asset no encontrado: %s", path); return {}; }
    size_t sz = AAsset_getLength(a);
    std::vector<uint8_t> buf(sz);
    AAsset_read(a, buf.data(), sz);
    AAsset_close(a);
    return buf;
}

// ── Raycast contra suelo ──────────────────────────────────────────────────────
// Retorna la mayor Y de suelo bajo el punto (px, search_y, pz), dentro de max_dist
// Retorna -1e9 si no hay suelo.
static float find_floor(float px, float search_y, float pz) {
    float best = -1e9f;
    Vec3 ray_o  = {px, search_y, pz};
    Vec3 ray_d  = {0, -1, 0};  // raycast hacia abajo

    const std::vector<Tri>* tris = g_col_grid.get(px, pz);
    if (!tris) return best;

    for (const auto& tri : *tris) {
        Vec3 e1 = {tri.b.x-tri.a.x, tri.b.y-tri.a.y, tri.b.z-tri.a.z};
        Vec3 e2 = {tri.c.x-tri.a.x, tri.c.y-tri.a.y, tri.c.z-tri.a.z};
        
        // Calcular normal
        Vec3 n = vec3_cross(e1, e2);
        float nlen = sqrtf(n.x*n.x + n.y*n.y + n.z*n.z);
        if (nlen > 0.0f) { n.x/=nlen; n.y/=nlen; n.z/=nlen; }
        
        // Si no mira hacia arriba (ej. pared o techo), ignorarlo como suelo
        if (n.y < 0.3f) continue;
        
        Vec3 h  = vec3_cross(ray_d, e2);
        float det = vec3_dot(e1, h);
        if (fabsf(det) < 1e-6f) continue;
        float inv_det = 1.0f / det;
        Vec3 s   = {ray_o.x-tri.a.x, ray_o.y-tri.a.y, ray_o.z-tri.a.z};
        float u  = vec3_dot(s, h) * inv_det;
        if (u < 0.0f || u > 1.0f) continue;
        Vec3 q   = vec3_cross(s, e1);
        float v  = vec3_dot(ray_d, q) * inv_det;
        if (v < 0.0f || u + v > 1.0f) continue;
        float t  = vec3_dot(e2, q) * inv_det;
        if (t < 0.0f || t > 200.f) continue;  // Solo detectar suelo HACIA ABAJO
        float hit_y = search_y - t;
        if (hit_y > best) best = hit_y;
    }
    return best;
}

static bool hit_wall(float x1, float y1, float z1, float x2, float y2, float z2) {
    Vec3 ray_o = {x1, y1, z1};
    Vec3 ray_d = {x2 - x1, y2 - y1, z2 - z1};
    const float dist = sqrtf(
        ray_d.x*ray_d.x +
        ray_d.y*ray_d.y +
        ray_d.z*ray_d.z
    );
    if (dist < 0.001f) return false;

    ray_d.x /= dist;
    ray_d.y /= dist;
    ray_d.z /= dist;

    const float inv_cell = 1.0f / g_col_grid.cell_size;
    const int cx0 = static_cast<int>(std::floor(x1 * inv_cell));
    const int cz0 = static_cast<int>(std::floor(z1 * inv_cell));
    const int cx1 = static_cast<int>(std::floor(x2 * inv_cell));
    const int cz1 = static_cast<int>(std::floor(z2 * inv_cell));

    const int min_cx = std::min(cx0, cx1) - 1;
    const int max_cx = std::max(cx0, cx1) + 1;
    const int min_cz = std::min(cz0, cz1) - 1;
    const int max_cz = std::max(cz0, cz1) + 1;

    for (int cx = min_cx; cx <= max_cx; ++cx) {
        for (int cz = min_cz; cz <= max_cz; ++cz) {
            const auto it = g_col_grid.cells.find({cx, cz});
            if (it == g_col_grid.cells.end()) continue;

            for (const auto& tri : it->second) {
                Vec3 e1 = {tri.b.x-tri.a.x, tri.b.y-tri.a.y, tri.b.z-tri.a.z};
                Vec3 e2 = {tri.c.x-tri.a.x, tri.c.y-tri.a.y, tri.c.z-tri.a.z};

                Vec3 n = vec3_cross(e1, e2);
                const float nlen = sqrtf(n.x*n.x + n.y*n.y + n.z*n.z);
                if (nlen > 0.0f) {
                    n.x/=nlen;
                    n.y/=nlen;
                    n.z/=nlen;
                }

                if (fabsf(n.y) > 0.7f) continue;

                Vec3 h = vec3_cross(ray_d, e2);
                const float det = vec3_dot(e1, h);
                if (fabsf(det) < 1e-6f) continue;

                const float inv_det = 1.0f / det;
                Vec3 s = {
                    ray_o.x-tri.a.x,
                    ray_o.y-tri.a.y,
                    ray_o.z-tri.a.z
                };

                const float u = vec3_dot(s, h) * inv_det;
                if (u < 0.0f || u > 1.0f) continue;

                Vec3 q = vec3_cross(s, e1);
                const float v = vec3_dot(ray_d, q) * inv_det;
                if (v < 0.0f || u + v > 1.0f) continue;

                const float t = vec3_dot(e2, q) * inv_det;
                if (t > 0.0f && t < dist + 0.15f) {
                    return true;
                }
            }
        }
    }

    return false;
}

static void load_txd_to_gpu(const char* path) {
    auto txd_raw = read_asset(path);
    if (txd_raw.empty()) {
        LOGE("TXD no encontrado: %s", path);
        return;
    }
    std::vector<TXDTexture> textures = txd_load_all(txd_raw.data(), txd_raw.size());
    for (const auto& tex : textures) {
        GLuint t_id;
        glGenTextures(1, &t_id);
        glBindTexture(GL_TEXTURE_2D, t_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        g_tex_map[to_lower(tex.name)] = t_id;
        LOGI("Cargada textura GPU: %s (%dx%d)", tex.name.c_str(), tex.width, tex.height);
    }
}

// ── Setup de Modelo ───────────────────────────────────────────────────────────
static void setup_model() {
    auto bsp_raw = read_asset("levels/asylum/scene1.bsp");
    if (bsp_raw.empty()) {
        LOGE("No se encontró scene1.bsp");
        return;
    }

    DFFModel model = bsp_load(bsp_raw.data(), bsp_raw.size());
    if (!model.valid || model.indices_by_mat.empty()) {
        LOGE("Modelo BSP inválido");
        return;
    }

    g_world_lighting = model.world;

    // Construir triángulos de colisión desde todos los vértices del modelo
    g_col_grid.cells.clear();
    int tris_count = 0;
    for (const auto& mat_idx_list : model.indices_by_mat) {
        for (size_t i = 0; i + 2 < mat_idx_list.size(); i += 3) {
            uint32_t ia = mat_idx_list[i];
            uint32_t ib = mat_idx_list[i+1];
            uint32_t ic = mat_idx_list[i+2];
            if (ia >= model.vertices.size() || ib >= model.vertices.size() || ic >= model.vertices.size()) continue;
            Tri t;
            t.a = {model.vertices[ia].x, model.vertices[ia].y, model.vertices[ia].z};
            t.b = {model.vertices[ib].x, model.vertices[ib].y, model.vertices[ib].z};
            t.c = {model.vertices[ic].x, model.vertices[ic].y, model.vertices[ic].z};
            g_col_grid.add(t);
            tris_count++;
        }
    }
    LOGI("Triángulos de colisión (Grid): %d", tris_count);

    // VAO/VBO
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(DFFVertex), model.vertices.data(), GL_STATIC_DRAW);

    // a_pos (location 0): vec3 @ offset 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, x));
    glEnableVertexAttribArray(0);
    // a_uv (location 1): vec2 @ offset 12
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, u));
    glEnableVertexAttribArray(1);
    // a_normal (location 2): vec3 @ offset 20  (después de x,y,z,u,v = 5*4 = 20)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, nx));
    glEnableVertexAttribArray(2);
    // a_color (location 3): vec4 uint8 @ offset 32
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, r));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, bone_indices));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(DFFVertex), (void*)offsetof(DFFVertex, bone_weights));
    glEnableVertexAttribArray(5);

    g_groups.clear();
    for (size_t i = 0; i < model.indices_by_mat.size(); i++) {
        if (model.indices_by_mat[i].empty()) continue;
        GLuint ebo;
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices_by_mat[i].size() * sizeof(uint32_t), model.indices_by_mat[i].data(), GL_STATIC_DRAW);
        RenderGroup group;
        group.ebo = ebo;
        group.num_indices = (int)model.indices_by_mat[i].size();
        group.texture_id = (GLuint)i;
        if (i < model.materials.size()) group.material = model.materials[i];
        g_groups.push_back(group);
    }
    glBindVertexArray(0);

    // TXD
    load_txd_to_gpu("levels/asylum/pak/scene1pc.txd");
    load_txd_to_gpu("levels/asylum/pak/modelspc.txd");
    load_txd_to_gpu("cash_pc.txd");

    // Load Cash DFF
    auto cash_raw = read_asset("cash_pc.dff");
    if (!cash_raw.empty()) {
        auto cash_models = dff_load_archive(cash_raw.data(), cash_raw.size());
        if (!cash_models.empty()) {
            ModelRenderData rd;
            auto& cash_model = cash_models.begin()->second;
            g_cash_model = cash_model;
            upload_dff_to_gpu(cash_model, rd);
            dump_cash_debug(cash_model);
            g_model_render["cash"] = rd;
            
            g_cash_y_offset = 0.0f;
            LOGI("Cash model loaded. Y-Offset manual: %.3f", g_cash_y_offset);
        }
    }

    for (size_t i = 0; i < g_groups.size(); i++) {
        std::string tex_name = "";
        if (g_groups[i].texture_id < model.material_textures.size()) {
            tex_name = model.material_textures[g_groups[i].texture_id];
        }
        g_groups[i].texture_id = get_texture(tex_name);
    }

    // Posición inicial del jugador: centro del bounding box del nivel
    {
        float min_x=1e9f, max_x=-1e9f;
        float min_y=1e9f, max_y=-1e9f;
        float min_z=1e9f, max_z=-1e9f;
        for (const auto& v : model.vertices) {
            if(v.x < min_x) min_x=v.x; if(v.x > max_x) max_x=v.x;
            if(v.y < min_y) min_y=v.y; if(v.y > max_y) max_y=v.y;
            if(v.z < min_z) min_z=v.z; if(v.z > max_z) max_z=v.z;
        }
        float cx = (min_x + max_x) * 0.5f;
        float cy = (min_y + max_y) * 0.5f;
        float cz = (min_z + max_z) * 0.5f;

        float candidates[][2] = {
            {cx, cz},
            {cx + (max_x-min_x)*0.1f, cz},
            {cx - (max_x-min_x)*0.1f, cz},
            {cx, cz + (max_z-min_z)*0.1f},
            {cx, cz - (max_z-min_z)*0.1f},
            {0.f, 0.f}
        };
        bool spawned = false;
        for (auto& c : candidates) {
            float fy = find_floor(c[0], max_y + 5.f, c[1]);
            if (fy > -1e8f) {
                g_player_pos = {c[0], fy, c[1]};
                LOGI("Spawn en (%.1f, %.1f, %.1f)", g_player_pos.x, g_player_pos.y, g_player_pos.z);
                spawned = true;
                break;
            }
        }
        if (!spawned) {
            g_player_pos = {cx, cy, cz};
        }
    }
    g_vel_y = 0.f;

    // Dump entity.inst to see format
    auto inst_raw = read_asset("levels/asylum/entity.inst");
    if (inst_raw.empty()) {
        LOGE("No se encontró levels/asylum/entity.inst");
        inst_raw = read_asset("levels/asylum/scene1.inst");
        if (inst_raw.empty()) {
            LOGE("Tampoco scene1.inst");
        }
    }
    
    if (!inst_raw.empty()) {
        g_insts = parse_inst(inst_raw);
    }

    auto dff_raw = read_asset("levels/asylum/pak/modelspc.dff");
    if (!dff_raw.empty()) {
        g_models = dff_load_archive(dff_raw.data(), dff_raw.size());
        LOGI("Modelos extraidos de modelspc.dff: %zu", g_models.size());
        for (auto& pair : g_models) {
            ModelRenderData rd;
            upload_dff_to_gpu(pair.second, rd);
            g_model_render[pair.first] = rd;
        }
    } else {
        LOGE("No se encontró levels/asylum/pak/modelspc.dff");
    }

    auto ifp_raw = read_asset("levels/asylum/allanims.ifp");
    if (!ifp_raw.empty()) {
        g_anims = load_ifp(ifp_raw.data(), ifp_raw.size());
        LOGI("REAL ANIMATION NAMES IN IFP:");
        for (const auto& pair : g_anims) {
            LOGI(" - %s", pair.first.c_str());
        }
        rebuild_debug_animation_list();
    } else {
        g_anims.clear();
        g_debug_anim_list.clear();
        LOGE("No se encontro allanims.ifp");
    }
}

// ── JNI ───────────────────────────────────────────────────────────────────────
extern "C" {

JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeInit(JNIEnv* env, jobject, jobject asset_mgr, jstring game_path) {
    g_assets = AAssetManager_fromJava(env, asset_mgr);
    if (game_path) {
        const char* p = env->GetStringUTFChars(game_path, nullptr);
        g_base_path = p;
        env->ReleaseStringUTFChars(game_path, p);
    } else {
        g_base_path = "";
    }
    g_player_pos= {0.f, 0.f, 0.f};
    g_player_yaw= 0.f;
    g_cam_yaw   = 0.f;
    g_cam_pitch = 0.f;
    g_vel_y     = 0.f;
    g_on_ground = false;
    g_move_fwd  = 0.f;
    g_move_right= 0.f;
    // Arrancar en bind pose: ninguna animación experimental puede
    // deformar el modelo al iniciar la aplicación.
    g_debug_anim_idx = -1;
    g_cash_skinning_enabled = false;
    g_cash_pos_adjust = {0.0f, 0.0f, 0.0f};
    g_cash_rot_adjust_deg = {0.0f, -91.0f, 180.0f};
}

JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeSurfaceCreated(JNIEnv*, jobject) {
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    g_program = glCreateProgram();
    glAttachShader(g_program, vert);
    glAttachShader(g_program, frag);
    glLinkProgram(g_program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    glClearColor(0.3f, 0.4f, 0.5f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    // GL_CULL_FACE desactivado — BSP tiene winding inconsistente

    setup_model();
}

JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeSurfaceChanged(JNIEnv*, jobject, jint w, jint h) {
    g_width = w; g_height = h;
    glViewport(0, 0, w, h);
}

// dt en segundos, llamado desde Kotlin en cada frame
JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeDrawFrame(JNIEnv*, jobject) {
    static long long last_ns = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now_ns = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    float dt = last_ns ? (float)(now_ns - last_ns) * 1e-9f : 0.016f;
    dt = std::min(dt, 0.05f);  // cap para saltos de frame
    last_ns = now_ns;

    // ── Física y movimiento ────────────────────────────────────────────────
    float cam_cy = cosf(g_cam_yaw), cam_sy = sinf(g_cam_yaw);
    Vec3 fwd_xz   = {-cam_sy, 0, -cam_cy};
    Vec3 right_xz = { cam_cy, 0, -cam_sy};

    Vec3 vel_xz = vec3_add(
        vec3_scale(fwd_xz,   g_move_fwd),
        vec3_scale(right_xz, g_move_right)
    );
    float speed_sq = vel_xz.x*vel_xz.x + vel_xz.z*vel_xz.z;
    if (speed_sq > 0.01f) {
        float inv = 1.0f / sqrtf(speed_sq);
        vel_xz.x *= inv; vel_xz.z *= inv;
        g_player_yaw = atan2f(-vel_xz.x, -vel_xz.z);
    }

    float nx = g_player_pos.x + vel_xz.x * MOVE_SPEED * dt;
    float nz = g_player_pos.z + vel_xz.z * MOVE_SPEED * dt;
    float waist_y = g_player_pos.y + 1.0f;

    if (hit_wall(g_player_pos.x, waist_y, g_player_pos.z, nx, waist_y, nz)) {
        if (!hit_wall(g_player_pos.x, waist_y, g_player_pos.z, nx, waist_y, g_player_pos.z)) {
            g_player_pos.x = nx;
        } else if (!hit_wall(g_player_pos.x, waist_y, g_player_pos.z, g_player_pos.x, waist_y, nz)) {
            g_player_pos.z = nz;
        }
    } else {
        g_player_pos.x = nx;
        g_player_pos.z = nz;
    }

    // Gravedad
    g_vel_y += GRAVITY * dt;
    g_player_pos.y += g_vel_y * dt;

    float floor_y = find_floor(g_player_pos.x, waist_y, g_player_pos.z);
    if (g_player_pos.y < floor_y) {
        g_player_pos.y = floor_y;
        g_vel_y = 0.f;
        g_on_ground = true;
    } else {
        g_on_ground = false;
    }

    // ── Cámara Orbit ───────────────────────────────────────────────────────
    Vec3 cam_pos;
    cam_pos.x = g_player_pos.x + sinf(g_cam_yaw) * cosf(g_cam_pitch) * g_cam_dist;
    cam_pos.y = g_player_pos.y + 1.5f - sinf(g_cam_pitch) * g_cam_dist;
    cam_pos.z = g_player_pos.z + cosf(g_cam_yaw) * cosf(g_cam_pitch) * g_cam_dist;
    Vec3 target = {g_player_pos.x, g_player_pos.y + 1.5f, g_player_pos.z};

    // ── Render ────────────────────────────────────────────────────────────    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!g_program || !g_vao || g_groups.empty()) return;

    float aspect = g_height > 0 ? (float)g_width / (float)g_height : 1.f;
    Mat4 proj = mat4_perspective(1.22f, aspect, 0.1f, 800.f);
    Mat4 view = mat4_look_at(cam_pos, target, {0.f, 1.f, 0.f});
    Mat4 model_m = mat4_identity();
    Mat4 mvp = mat4_mul(mat4_mul(proj, view), model_m);

    glUseProgram(g_program);
    glUniformMatrix4fv(glGetUniformLocation(g_program, "u_mvp"),   1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(g_program, "u_model"), 1, GL_FALSE, model_m.m);
    glUniform1i(glGetUniformLocation(g_program, "u_skinned"), 0);
    glUniform1i(glGetUniformLocation(g_program, "u_tex"), 0);

    GLint loc_has_tex = glGetUniformLocation(g_program, "u_has_tex");
    GLint loc_mat_color = glGetUniformLocation(g_program, "u_mat_color");
    GLint loc_mat_ambient = glGetUniformLocation(g_program, "u_mat_ambient");
    GLint loc_mat_diffuse = glGetUniformLocation(g_program, "u_mat_diffuse");

    glUniform4fv(glGetUniformLocation(g_program, "u_world_ambient"), 1, g_world_lighting.ambient);
    glUniform4fv(glGetUniformLocation(g_program, "u_dir_ambient"), 1, g_world_lighting.dir_ambient);
    glUniform3fv(glGetUniformLocation(g_program, "u_light_dir"), 1, g_world_lighting.light_dir);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(g_vao);

    for (const auto& group : g_groups) {
        if (group.texture_id) {
            glUniform1i(loc_has_tex, 1);
            glBindTexture(GL_TEXTURE_2D, group.texture_id);
        } else {
            glUniform1i(loc_has_tex, 0);
        }
        glUniform4fv(loc_mat_color, 1, group.material.color);
        glUniform1f(loc_mat_ambient, group.material.ambient);
        glUniform1f(loc_mat_diffuse, group.material.diffuse);
        
        if (group.material.color[3] < 0.99f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, group.ebo);
        glDrawElements(GL_TRIANGLES, group.num_indices, GL_UNSIGNED_INT, nullptr);
    }
    glDisable(GL_BLEND);

    glBindVertexArray(0);

    Mat4 vp = mat4_mul(proj, view);
    GLint loc_mvp = glGetUniformLocation(g_program, "u_mvp");
    GLint loc_model = glGetUniformLocation(g_program, "u_model");
    GLint loc_skinned = glGetUniformLocation(g_program, "u_skinned");
    GLint loc_bones = glGetUniformLocation(g_program, "u_bone_matrices");

    for (const auto& inst : g_insts) {
        auto it = g_model_render.find(inst.model);
        if (it != g_model_render.end()) {
            Mat4 inst_model = mat4_from_pos_quat(inst.pos, inst.rot);
            Mat4 inst_mvp = mat4_mul(vp, inst_model);
            glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, inst_mvp.m);
            glUniformMatrix4fv(loc_model, 1, GL_FALSE, inst_model.m);
            glUniform1i(loc_skinned, 0);

            glBindVertexArray(it->second.vao);
            for (const auto& group : it->second.groups) {
                if (group.texture_id) {
                    glUniform1i(loc_has_tex, 1);
                    glBindTexture(GL_TEXTURE_2D, group.texture_id);
                } else {
                    glUniform1i(loc_has_tex, 0);
                }
                glUniform4fv(loc_mat_color, 1, group.material.color);
                glUniform1f(loc_mat_ambient, group.material.ambient);
                glUniform1f(loc_mat_diffuse, group.material.diffuse);
                
                if (group.material.color[3] < 0.99f) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glDisable(GL_BLEND);
                }

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, group.ebo);
                glDrawElements(GL_TRIANGLES, group.num_indices, GL_UNSIGNED_INT, nullptr);
            }
            glDisable(GL_BLEND);
        }
    }
    
    // ── Skeletal Animation Update ─────────────────────────────────────────────
    g_anim_time += dt;
    auto it_cash = g_model_render.find("cash");
    if (it_cash != g_model_render.end()) {
        Mat4 global_bones[96];
        Mat4 skin_matrices[96];
        for (int i = 0; i < 96; ++i) {
            global_bones[i] = mat4_identity();
            skin_matrices[i] = mat4_identity();
        }
        /*
         * --------------------------------------------------------
         * Seleccionar animación
         * --------------------------------------------------------
         */
        const Animation* anim = nullptr;

        // Debug:
        // -2 = selección automática
        // -1 = bind pose
        // >=0 = animación seleccionada manualmente
        if (g_debug_anim_idx >= 0 &&
            g_debug_anim_idx < static_cast<int>(g_debug_anim_list.size())) {
            anim = g_debug_anim_list[
                static_cast<size_t>(g_debug_anim_idx)
            ];
        } else if (g_debug_anim_idx == -1) {
            // Bind pose: no aplicar IFP.
            anim = nullptr;
        } else {
            const float speed = sqrtf(
                g_move_fwd * g_move_fwd +
                g_move_right * g_move_right
            );

            auto find_anim = [&](const char* wanted) -> const Animation* {
                const std::string query = to_lower(wanted);
                for (const auto& pair : g_anims) {
                    if (to_lower(pair.first) == query) {
                        return &pair.second;
                    }
                }
                return nullptr;
            };

            if (speed > 0.6f) {
                anim = find_anim("Run_Fwd");
            } else if (speed > 0.05f) {
                anim = find_anim("Walk_Fwd");
            } else {
                anim = find_anim("Stand_Idle");
            }

            if (!anim) {
                anim = find_anim("Stand_Idle");
            }

            if (!anim && !g_anims.empty()) {
                anim = &g_anims.begin()->second;
            }
        }

        /*
         * --------------------------------------------------------
         * Debug de animación
         * --------------------------------------------------------
         */
        static bool logged_animation = false;
        if (!logged_animation && anim) {
            LOGI(
                "ANIM PLAY: %s duration=%.3f tracks=%zu",
                anim->name.c_str(),
                anim->duration,
                anim->tracks.size()
            );
            logged_animation = true;
        }
        /*
         * --------------------------------------------------------
         * Tiempo de animación
         * --------------------------------------------------------
         */
        float animation_time = 0.0f;
        if (anim && anim->duration > 0.0f) {
            animation_time = fmodf(
                g_anim_time,
                anim->duration
            );
            if (animation_time < 0.0f)
                animation_time += anim->duration;
        }
        /*
         * --------------------------------------------------------
         * Procesar huesos
         * --------------------------------------------------------
         */
        for (
            size_t bone_idx = 0;
            bone_idx < g_cash_model.bones.size() && bone_idx < 96;
            ++bone_idx
        ) {
            const DFFBone& bone = g_cash_model.bones[bone_idx];
            /*
             * ----------------------------------------------------
             * Pose base
             * ----------------------------------------------------
             */
            float pos[3] = { bone.pos_x, bone.pos_y, bone.pos_z };
            float quat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            bool animated = false;
            /*
             * ----------------------------------------------------
             * Buscar track usando SOLAMENTE bone_id
             * ----------------------------------------------------
             *
             * No usamos bone_idx como fallback.
             *
             * El IFP utiliza IDs como:
             *
             * 1000
             * 1001
             * 1003
             * 1045
             * 1095
             *
             * Por lo tanto hay que relacionarlos con el
             * bone_id real del DFF.
             */
            if (
                anim &&
                bone.bone_id != 0xFFFFFFFF
            ) {
                for (const auto& track : anim->tracks) {
                    if (
                        track.bone_id != static_cast<int>(bone.bone_id)
                    ) {
                        continue;
                    }
                    if (track.keyframes.empty()) {
                        continue;
                    }
                    animated = true;
                    /*
                     * ------------------------------------------------
                     * Buscar los dos keyframes
                     * ------------------------------------------------
                     */
                    const AnimationKeyframe* k0 = &track.keyframes.front();
                    const AnimationKeyframe* k1 = &track.keyframes.front();
                    /*
                     * Antes del primer frame.
                     */
                    if (
                        animation_time <= track.keyframes.front().time
                    ) {
                        k0 = &track.keyframes.front();
                        k1 = &track.keyframes.front();
                    } else {
                        bool found = false;
                        for (
                            size_t k = 0;
                            k + 1 < track.keyframes.size();
                            ++k
                        ) {
                            const auto& a = track.keyframes[k];
                            const auto& b = track.keyframes[k + 1];
                            if (
                                animation_time >= a.time &&
                                animation_time <= b.time
                            ) {
                                k0 = &a;
                                k1 = &b;
                                found = true;
                                break;
                            }
                        }
                        /*
                         * Si estamos después del último keyframe,
                         * mantenemos el último.
                         */
                        if (!found) {
                            k0 = &track.keyframes.back();
                            k1 = &track.keyframes.back();
                        }
                    }
                    /*
                     * ------------------------------------------------
                     * Interpolación
                     * ------------------------------------------------
                     */
                    float t = 0.0f;
                    if (
                        k1 != k0 &&
                        k1->time > k0->time
                    ) {
                        t = (animation_time - k0->time) / (k1->time - k0->time);
                        t = std::max(
                            0.0f,
                            std::min(1.0f, t)
                        );
                    }
                    /*
                     * ------------------------------------------------
                     * Quaternion
                     * ------------------------------------------------
                     */
                    float qx1 = k1->qx;
                    float qy1 = k1->qy;
                    float qz1 = k1->qz;
                    float qw1 = k1->qw;
                    /*
                     * Quaternion shortest path.
                     */
                    float dot = k0->qx * qx1 + k0->qy * qy1 + k0->qz * qz1 + k0->qw * qw1;
                    if (dot < 0.0f) {
                        qx1 = -qx1;
                        qy1 = -qy1;
                        qz1 = -qz1;
                        qw1 = -qw1;
                    }
                    quat[0] = k0->qx + t * (qx1 - k0->qx);
                    quat[1] = k0->qy + t * (qy1 - k0->qy);
                    quat[2] = k0->qz + t * (qz1 - k0->qz);
                    quat[3] = k0->qw + t * (qw1 - k0->qw);
                    /*
                     * Normalizar.
                     */
                    float qlen = sqrtf(
                        quat[0] * quat[0] +
                        quat[1] * quat[1] +
                        quat[2] * quat[2] +
                        quat[3] * quat[3]
                    );
                    if (qlen > 0.000001f) {
                        quat[0] /= qlen;
                        quat[1] /= qlen;
                        quat[2] /= qlen;
                        quat[3] /= qlen;
                    }
                    /*
                     * ------------------------------------------------
                     * Translation
                     * ------------------------------------------------
                     */
                    pos[0] = k0->tx + t * (k1->tx - k0->tx);
                    pos[1] = k0->ty + t * (k1->ty - k0->ty);
                    pos[2] = k0->tz + t * (k1->tz - k0->tz);
                    /*
                     * Debug solamente para algunos huesos.
                     */
                    static int debug_anim_counter = 0;
                    if (
                        debug_anim_counter++ % 120 == 0 &&
                        (
                            bone.bone_id == 1000 ||
                            bone.bone_id == 1045 ||
                            bone.bone_id == 1095
                        )
                    ) {
                        LOGI(
                            "ANIM APPLY: anim=%s bone=%u "
                            "time=%.3f "
                            "Q=(%.3f %.3f %.3f %.3f) "
                            "P=(%.3f %.3f %.3f)",
                            anim->name.c_str(),
                            bone.bone_id,
                            animation_time,
                            quat[0], quat[1], quat[2], quat[3],
                            pos[0], pos[1], pos[2]
                        );
                    }
                    break;
                }
            }
            /*
             * --------------------------------------------------------
             * Matriz bind
             * --------------------------------------------------------
             */
            Mat4 bind_mat = mat4_identity();
            bind_mat.m[0] = bone.rot_mat[0];
            bind_mat.m[1] = bone.rot_mat[1];
            bind_mat.m[2] = bone.rot_mat[2];
            bind_mat.m[4] = bone.rot_mat[3];
            bind_mat.m[5] = bone.rot_mat[4];
            bind_mat.m[6] = bone.rot_mat[5];
            bind_mat.m[8] = bone.rot_mat[6];
            bind_mat.m[9] = bone.rot_mat[7];
            bind_mat.m[10] = bone.rot_mat[8];
            bind_mat.m[12] = bone.pos_x;
            bind_mat.m[13] = bone.pos_y;
            bind_mat.m[14] = bone.pos_z;
            /*
             * --------------------------------------------------------
             * Matriz local
             * --------------------------------------------------------
             */
            Mat4 local_mat = bind_mat;
            if (animated) {
                /*
                 * La animación proporciona la rotación.
                 * La posición del hueso se conserva desde el DFF,
                 * excepto cuando el track proporciona traslación.
                 */
                float zero[3] = { 0.0f, 0.0f, 0.0f };
                Mat4 anim_mat = mat4_from_pos_quat(
                    zero,
                    quat
                );
                local_mat = anim_mat;
                /*
                 * Mantener la posición del hueso
                 * proveniente del skeleton.
                 */
                local_mat.m[12] = bind_mat.m[12];
                local_mat.m[13] = bind_mat.m[13];
                local_mat.m[14] = bind_mat.m[14];
                /*
                 * Root.
                 *
                 * Si es el root, sí usamos la traslación
                 * proporcionada por la animación.
                 */
                if (bone.bone_id == 1000) {
                    local_mat.m[12] = pos[0];
                    local_mat.m[13] = pos[1];
                    local_mat.m[14] = pos[2];
                }
            }
            /*
             * --------------------------------------------------------
             * Jerarquía
             * --------------------------------------------------------
             */
            if (
                bone.parent != 0xFFFFFFFF &&
                bone.parent < bone_idx
            ) {
                global_bones[bone_idx] = mat4_mul(
                    global_bones[bone.parent],
                    local_mat
                );
            } else {
                global_bones[bone_idx] = local_mat;
            }
            /*
             * --------------------------------------------------------
             * Skin matrix
             * --------------------------------------------------------
             */
            if (
                bone.matrix_index != 0xFFFFFFFF &&
                bone.matrix_index < 96 &&
                (
                    bone.matrix_index * 16 + 15
                ) < g_cash_model.inverse_bind_matrices.size()
            ) {
                Mat4 inverse_bind;
                for (int j = 0; j < 16; ++j) {
                    inverse_bind.m[j] = g_cash_model
                        .inverse_bind_matrices[
                            static_cast<size_t>(
                                bone.matrix_index
                            ) * 16 + j
                        ];
                }
                skin_matrices[
                    bone.matrix_index
                ] = mat4_mul(
                    global_bones[bone_idx],
                    inverse_bind
                );
            }
        }
        /*
         * --------------------------------------------------------
         * Render
         * --------------------------------------------------------
         */
        // Rotación final de Cash. La altura se calcula automáticamente para
        // apoyar la parte más baja del mesh sobre el piso del nivel.
        const float rx = g_cash_rot_adjust_deg.x * 0.017453292519943f;
        const float ry = g_cash_rot_adjust_deg.y * 0.017453292519943f;
        const float rz = g_cash_rot_adjust_deg.z * 0.017453292519943f;
        const Mat4 extra_rot = mat4_mul(
            mat4_mul(mat4_rotate_z(rz), mat4_rotate_y(ry)),
            mat4_rotate_x(rx)
        );

        const float floor_y = find_floor(
            g_player_pos.x,
            g_player_pos.y + 3.0f,
            g_player_pos.z
        );

        // Calculamos la transformación sólo con rotación y sin traslación
        // para conocer exactamente cuál será el Y mínimo del modelo.
        Mat4 cash_rotation_m = mat4_from_pos_cash(
            {0.0f, 0.0f, 0.0f},
            g_player_yaw + CASH_MODEL_YAW_OFFSET
        );
        cash_rotation_m = mat4_mul(
            cash_rotation_m,
            extra_rot
        );

        float model_min_y = cash_model_min_y(cash_rotation_m);

        float cash_base_y = g_player_pos.y;
        if (floor_y > -1e8f && model_min_y < 1e20f) {
            cash_base_y =
                floor_y
                - model_min_y
                + CASH_FOOT_CLEARANCE;
        }

        const Vec3 center_pos = {
            g_player_pos.x + g_cash_pos_adjust.x,
            cash_base_y + g_cash_y_offset + g_cash_pos_adjust.y,
            g_player_pos.z + g_cash_pos_adjust.z
        };

        Mat4 cash_model_m = mat4_from_pos_cash(
            center_pos,
            g_player_yaw + CASH_MODEL_YAW_OFFSET
        );
        cash_model_m = mat4_mul(
            cash_model_m,
            extra_rot
        );
        Mat4 cash_mvp = mat4_mul(
            vp,
            cash_model_m
        );        glUniformMatrix4fv(
            loc_mvp,
            1,
            GL_FALSE,
            cash_mvp.m
        );
        glUniformMatrix4fv(
            loc_model,
            1,
            GL_FALSE,
            cash_model_m.m
        );
        // Skinning experimental: disabled by default until the
        // Skin palette <-> HAnim mapping is proven against Cash.
        glUniform1i(
            loc_skinned,
            g_cash_skinning_enabled ? 1 : 0
        );
        if (g_cash_skinning_enabled) {
            glUniformMatrix4fv(
                loc_bones,
                96,
                GL_FALSE,
                skin_matrices[0].m
            );
        }
        glBindVertexArray(
            it_cash->second.vao
        );
        for (const auto& group : it_cash->second.groups) {
            if (group.texture_id) {
                glUniform1i(loc_has_tex, 1);
                glBindTexture(
                    GL_TEXTURE_2D,
                    group.texture_id
                );
            } else {
                glUniform1i(loc_has_tex, 0);
            }
            glUniform4fv(loc_mat_color, 1, group.material.color);
            glUniform1f(loc_mat_ambient, group.material.ambient);
            glUniform1f(loc_mat_diffuse, group.material.diffuse);
            
            if (group.material.color[3] < 0.99f) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }

            glBindBuffer(
                GL_ELEMENT_ARRAY_BUFFER,
                group.ebo
            );
            glDrawElements(
                GL_TRIANGLES,
                group.num_indices,
                GL_UNSIGNED_INT,
                nullptr
            );
        }
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
}

// Joystick de movimiento: fwd en -1..1, right en -1..1
JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeMove(JNIEnv*, jobject, jfloat fwd, jfloat right) {
    g_move_fwd   = fwd;
    g_move_right = right;
}

// Arrastrar para mirar (lado derecho de pantalla)
JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeLook(JNIEnv*, jobject, jfloat dx, jfloat dy) {
    const float SENS = 0.003f;
    g_cam_yaw   += dx * SENS;
    g_cam_pitch -= dy * SENS;
    g_cam_pitch = std::max(-1.4f, std::min(1.4f, g_cam_pitch));
}

// Siguiente animación de depuración.
// Ciclo: automático -> bind pose -> animación 0 -> ... -> bind pose.
JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeNextDebugAnimation(JNIEnv*, jobject) {
    if (g_debug_anim_list.empty()) {
        LOGI("DEBUG ANIM: no hay animaciones con tracks");
        g_debug_anim_idx = -2;
        return;
    }

    if (g_debug_anim_idx == -2) {
        g_debug_anim_idx = -1;
    } else if (g_debug_anim_idx == -1) {
        g_debug_anim_idx = 0;
    } else {
        ++g_debug_anim_idx;
        if (g_debug_anim_idx >= static_cast<int>(g_debug_anim_list.size())) {
            g_debug_anim_idx = -1;
        }
    }

    if (g_debug_anim_idx == -1) {
        LOGI("DEBUG ANIM: BIND POSE");
    } else {
        const Animation* anim =
            g_debug_anim_list[static_cast<size_t>(g_debug_anim_idx)];
        LOGI(
            "DEBUG ANIM: [%d] %s duration=%.3f tracks=%zu",
            g_debug_anim_idx,
            anim->name.c_str(),
            anim->duration,
            anim->tracks.size()
        );
    }
}

// Saltar
JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeSetCashTransform(
    JNIEnv*,
    jobject,
    jfloat px,
    jfloat py,
    jfloat pz,
    jfloat rx,
    jfloat ry,
    jfloat rz
) {
    g_cash_pos_adjust = {px, py, pz};
    g_cash_rot_adjust_deg = {rx, ry, rz};
}

JNIEXPORT void JNICALL
Java_com_manhunt_port_ManhuntRenderer_nativeJump(JNIEnv*, jobject) {
    if (g_on_ground) {
        g_vel_y = 8.0f;
        g_on_ground = false;
    }
}

// Mantener compatibilidad con nativeDrag/nativeScale anteriores (los elimino)

} // extern "C"