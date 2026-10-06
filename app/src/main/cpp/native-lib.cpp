// native-lib.cpp — Manhunt Android port
// target: Android NDK r25+, OpenGL ES 3.0, C++17

#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <string>
#include <sstream>
#include <vector>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <algorithm>
#include <map>
#include <dirent.h>
#include <ctime>
#include "txd_loader.h"
#include "dff_loader.h"
#include "inst_loader.h"
#include "ifp_loader.h"
#include "col_loader.h"

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

static Vec3 mat4_transform_point(const Mat4& m, Vec3 p) {
    return {
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    };
}

static Mat4 mat4_inverse_rigid(const Mat4& m) {
    Mat4 r = mat4_identity();

    r.m[0] = m.m[0];
    r.m[1] = m.m[4];
    r.m[2] = m.m[8];

    r.m[4] = m.m[1];
    r.m[5] = m.m[5];
    r.m[6] = m.m[9];

    r.m[8] = m.m[2];
    r.m[9] = m.m[6];
    r.m[10] = m.m[10];

    r.m[12] = -(
        r.m[0] * m.m[12] +
        r.m[4] * m.m[13] +
        r.m[8] * m.m[14]
    );
    r.m[13] = -(
        r.m[1] * m.m[12] +
        r.m[5] * m.m[13] +
        r.m[9] * m.m[14]
    );
    r.m[14] = -(
        r.m[2] * m.m[12] +
        r.m[6] * m.m[13] +
        r.m[10] * m.m[14]
    );

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

// Convención de Skin/FrameList elegida automáticamente en bind pose.
// bit 0: transponer rotación del FrameList
// bit 1: multiplicar local * parent
// bit 2: usar skin_bone_to_frame
// bit 3: transponer inverse-bind
// bit 4: multiplicar inverse-bind * globalBone
// bit 5: aplicar inverse del Frame del Atomic
static int g_cash_skin_convention = -1;

// Convención seleccionada automáticamente al validar la pose base.
// -1 = aún no calculada.
// bit 0: transponer rotación del FrameList
// bit 1: multiplicar local * parent en la jerarquía
// bit 2: usar skin_bone_to_frame como remapeo
// bit 3: transponer inverse-bind
// bit 4: multiplicar inverse-bind * globalBone


// Forward declaration: setup_model() may use this diagnostic helper.
static void dump_cash_debug(const DFFModel& model);

static std::string to_lower(std::string s) {
    for (char& c : s) c = tolower((unsigned char)c);
    return s;
}

static std::string normalize_col_name(std::string s) {
    s = to_lower(std::move(s));

    for (char& c : s) {
        if (c == '\\') c = '/';
    }

    const size_t slash = s.find_last_of('/');
    if (slash != std::string::npos) {
        s = s.substr(slash + 1);
    }

    const auto strip_suffix = [&](const char* suffix) {
        const size_t len = std::strlen(suffix);
        if (s.size() >= len &&
            s.compare(s.size() - len, len, suffix) == 0) {
            s.resize(s.size() - len);
            return true;
        }
        return false;
    };

    strip_suffix(".dff");
    strip_suffix(".col");
    return s;
}

static bool is_dynamic_actor_col(const std::string& name) {
    const std::string key = normalize_col_name(name);

    // Estos modelos describen formas de personajes/estados del actor,
    // no obstáculos estáticos del escenario.
    return key == "player" ||
           key == "hunter" ||
           key == "visplayer" ||
           key == "deadped" ||
           key == "crouch" ||
           key == "climb";
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
static Vec3 g_cash_pos_adjust = {0.0f, 1.0f, 0.0f};
static Vec3 g_cash_rot_adjust_deg = {0.0f, -91.0f, 180.0f};

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
// Tope provisional basado en la magnitud de movimiento documentada
// para personajes en PLAYER_PHYS. El valor original exacto de caminar
// no está expuesto directamente en nuestros archivos de datos.
static const float MOVE_SPEED     = 6.5f;

// Altura máxima de escalón que el actor puede salvar sin saltar.
// Se mantiene por debajo de la altura de las esferas inferiores del
// COL "player", evitando convertir paredes bajas en rampas.
static const float PLAYER_MAX_STEP_HEIGHT = 0.50f;

// Desnivel máximo que seguimos automáticamente por subpaso cuando el
// actor ya está apoyado en el suelo. Es menor que el step-up para que
// una pendiente se sienta continua y no como una sucesión de saltos.
static const float PLAYER_GROUND_FOLLOW_HEIGHT = 0.18f;

// Pendiente máxima caminable. Las superficies más inclinadas siguen
// siendo tratadas como obstáculos y requieren otra resolución.
static const float PLAYER_MAX_SLOPE_Y = 0.70f;

// La forma de colisión del jugador se obtiene del modelo "player"
// de collisions.col (2 esferas + 1 línea), como en el juego.
static const ColModel* g_player_col_model = nullptr;
static float g_player_collision_height = 2.0f;

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
static std::vector<ColModel> g_col_models;
static std::map<std::string, size_t> g_col_model_by_name;
static std::vector<std::pair<Vec3, float>> g_col_spheres_world;
static std::vector<EntityInst> g_insts;

// entityTypeData.ini: RECORD -> COLLISION_DATA.
// Manhunt uses this indirection instead of requiring the collision
// resource name to equal the render model name.
static std::map<std::string, std::string> g_entity_collision_data;

struct PlayerControlConfig {
    float stick_dead_zone = 0.35f;
    float move_walk_threshold = 0.53f;
    float move_run_threshold = 0.96f;
    float move_transition_speed = 0.20f;

    float sneak_walk_speed = 1.20f;
    float sneak_run_speed = 1.00f;
    float walk_speed = 1.00f;
    float run_speed = 1.00f;
    float sprint_speed = 1.00f;
    float crouch_forward_speed = 1.30f;
    float crouch_backward_speed = 0.90f;
    float crouch_sideways_speed = 1.20f;

    float turn_pause = 0.27f;
    float turn_acceleration = 4.0f;
    float extra_turn_speed = 50.0f;
    float run_threshold = 0.95f;
};

static PlayerControlConfig g_player_control;

static Vec3 col_to_vec3(const ColVec3& v) {
    return {v.x, v.y, v.z};
}

static void add_collision_box(
    const ColBox& box,
    const Mat4& transform
) {
    const Vec3 p[8] = {
        mat4_transform_point(transform, {box.min.x, box.min.y, box.min.z}),
        mat4_transform_point(transform, {box.max.x, box.min.y, box.min.z}),
        mat4_transform_point(transform, {box.max.x, box.max.y, box.min.z}),
        mat4_transform_point(transform, {box.min.x, box.max.y, box.min.z}),
        mat4_transform_point(transform, {box.min.x, box.min.y, box.max.z}),
        mat4_transform_point(transform, {box.max.x, box.min.y, box.max.z}),
        mat4_transform_point(transform, {box.max.x, box.max.y, box.max.z}),
        mat4_transform_point(transform, {box.min.x, box.max.y, box.max.z})
    };

    // 6 caras x 2 triángulos. El orden evita duplicar caras y
    // permite que find_floor() vea correctamente la cara superior.
    const int triangles[12][3] = {
        // z = min
        {0, 3, 2}, {0, 2, 1},
        // x = max
        {1, 2, 6}, {1, 6, 5},
        // z = max
        {4, 5, 6}, {4, 6, 7},
        // x = min
        {0, 4, 7}, {0, 7, 3},
        // y = max
        {3, 7, 6}, {3, 6, 2},
        // y = min
        {0, 1, 5}, {0, 5, 4}
    };

    for (const auto& tri : triangles) {
        g_col_grid.add({p[tri[0]], p[tri[1]], p[tri[2]]});
    }
}

static std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    s.erase(
        s.begin(),
        std::find_if(s.begin(), s.end(), not_space)
    );
    s.erase(
        std::find_if(
            s.rbegin(),
            s.rend(),
            not_space
        ).base(),
        s.end()
    );
    return s;
}

static void parse_entity_type_data(
    const std::vector<uint8_t>& raw
) {
    g_entity_collision_data.clear();
    g_player_control = PlayerControlConfig{};

    if (raw.empty()) return;

    const std::string text(
        reinterpret_cast<const char*>(raw.data()),
        raw.size()
    );

    std::istringstream stream(text);
    std::string line;
    std::string current_record;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        line = trim_copy(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        // Strip inline comments.
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = trim_copy(line.substr(0, comment));
        }
        if (line.empty()) continue;

        std::istringstream parts(line);
        std::string key;
        parts >> key;
        if (key.empty()) continue;

        if (to_lower(key) == "record") {
            std::string name;
            std::getline(parts, name);
            current_record = normalize_col_name(
                trim_copy(name)
            );
            continue;
        }

        if (to_lower(key) == "end") {
            current_record.clear();
            continue;
        }

        if (current_record.empty()) continue;

        if (to_lower(key) == "collision_data") {
            std::string collision_name;
            parts >> collision_name;
            if (!collision_name.empty()) {
                g_entity_collision_data[current_record] =
                    normalize_col_name(collision_name);
            }
        }

        // La configuración de control del jugador vive dentro de
        // RECORD player del entityTypeData.ini de cada nivel.
        if (current_record == "player") {
            const std::string lower_key = to_lower(key);

            if (lower_key == "stick_dead_zone") {
                parts >> g_player_control.stick_dead_zone;
            } else if (lower_key == "move_thresholds") {
                parts >>
                    g_player_control.move_walk_threshold >>
                    g_player_control.move_run_threshold;
            } else if (lower_key == "move_trans_speed") {
                parts >> g_player_control.move_transition_speed;
            } else if (lower_key == "sneak_walk_speed") {
                parts >> g_player_control.sneak_walk_speed;
            } else if (lower_key == "sneak_run_speed") {
                parts >> g_player_control.sneak_run_speed;
            } else if (lower_key == "walk_speed") {
                parts >> g_player_control.walk_speed;
            } else if (lower_key == "run_speed") {
                parts >> g_player_control.run_speed;
            } else if (lower_key == "sprint_speed") {
                parts >> g_player_control.sprint_speed;
            } else if (lower_key == "crouch_forward_speed") {
                parts >> g_player_control.crouch_forward_speed;
            } else if (lower_key == "crouch_backward_speed") {
                parts >> g_player_control.crouch_backward_speed;
            } else if (lower_key == "crouch_sideways_speed") {
                parts >> g_player_control.crouch_sideways_speed;
            } else if (lower_key == "turn_pause") {
                parts >> g_player_control.turn_pause;
            } else if (lower_key == "turn_acceleration") {
                parts >> g_player_control.turn_acceleration;
            } else if (lower_key == "extra_turn_speed") {
                parts >> g_player_control.extra_turn_speed;
            } else if (lower_key == "run_threshold") {
                parts >> g_player_control.run_threshold;
            }
        }
    }

    LOGI(
        "ENTITY TYPE DATA: %zu records con COLLISION_DATA",
        g_entity_collision_data.size()
    );

    LOGI(
        "PLAYER CONTROL: deadZone=%.2f walkThreshold=%.2f "
        "runThreshold=%.2f transition=%.2f "
        "walkSpeed=%.2f runSpeed=%.2f sprintSpeed=%.2f "
        "turnPause=%.2f turnAccel=%.2f extraTurn=%.2f",
        g_player_control.stick_dead_zone,
        g_player_control.move_walk_threshold,
        g_player_control.move_run_threshold,
        g_player_control.move_transition_speed,
        g_player_control.walk_speed,
        g_player_control.run_speed,
        g_player_control.sprint_speed,
        g_player_control.turn_pause,
        g_player_control.turn_acceleration,
        g_player_control.extra_turn_speed
    );
}

static std::string collision_data_for_instance(
    const EntityInst& inst
) {
    // INST: el primer string es el glgRecord/archetype. Ese es el
    // registro que se relaciona con RECORD ... en entityTypeData.ini.
    const std::string record =
        normalize_col_name(inst.name);

    // Cuando entityTypeData.ini está disponible, su ausencia de
    // COLLISION_DATA es significativa: la entidad no tiene una
    // geometría COL propia. No debemos inventar una usando el MODEL.
    if (!g_entity_collision_data.empty()) {
        if (record.empty()) return {};

        const auto type_it =
            g_entity_collision_data.find(record);

        if (type_it == g_entity_collision_data.end() ||
            type_it->second.empty()) {
            return {};
        }

        return type_it->second;
    }

    // Solo usamos el modelo como fallback cuando el type data original
    // todavía no está disponible.
    return normalize_col_name(inst.model);
}

static const ColModel* find_col_model(
    const std::string& collision_name
) {
    const auto it = g_col_model_by_name.find(
        normalize_col_name(collision_name)
    );
    if (it == g_col_model_by_name.end()) {
        return nullptr;
    }
    return &g_col_models[it->second];
}

static void rebuild_col_inst_collisions() {
    g_col_model_by_name.clear();
    g_col_spheres_world.clear();

    LOGI(
        "COL inst collisions: preparando %zu instancias contra %zu modelos COL",
        g_insts.size(),
        g_col_models.size()
    );

    if (g_col_models.empty() || g_insts.empty()) {
        LOGI("COL inst collisions: no hay modelos COL o instancias");
        return;
    }

    for (size_t i = 0; i < g_col_models.size(); ++i) {
        g_col_model_by_name[
            normalize_col_name(g_col_models[i].name)
        ] = i;
    }

    size_t matched_instances = 0;
    size_t skipped_actor_instances = 0;
    size_t matched_by_type_data = 0;
    size_t matched_by_model_fallback = 0;
    size_t mesh_faces = 0;
    size_t boxes = 0;
    size_t spheres = 0;
    size_t lines = 0;
    size_t invalid_faces = 0;
    size_t no_collision_data = 0;
    size_t missing_type_data_col = 0;
    size_t missing_model_fallback_col = 0;
    std::map<std::string, size_t> unmatched_models;
    std::map<std::string, size_t> unmatched_type_data;
    std::map<std::string, size_t> no_collision_data_classes;

    for (const auto& inst : g_insts) {
        const std::string record_name =
            normalize_col_name(inst.name);

        const auto type_it =
            g_entity_collision_data.find(record_name);

        const bool used_type_data =
            type_it != g_entity_collision_data.end() &&
            !type_it->second.empty();

        const std::string collision_name =
            collision_data_for_instance(inst);

        if (collision_name.empty()) {
            ++no_collision_data;
            ++no_collision_data_classes[
                inst.entity_class.empty()
                    ? "<empty>"
                    : inst.entity_class
            ];
            continue;
        }

        const ColModel* col =
            find_col_model(collision_name);

        if (!col) {
            if (used_type_data) {
                ++missing_type_data_col;
                ++unmatched_type_data[collision_name];
            } else {
                ++missing_model_fallback_col;
            }
            ++unmatched_models[collision_name];
            continue;
        }

        if (used_type_data) {
            ++matched_by_type_data;
        } else {
            ++matched_by_model_fallback;
        }

        if (is_dynamic_actor_col(col->name)) {
            ++skipped_actor_instances;
            continue;
        }

        const Mat4 transform =
            mat4_from_pos_quat(inst.pos, inst.rot);
        ++matched_instances;

        if (matched_instances <= 40) {
            LOGI(
                "COL MATCH[%zu]: entity=%s model=%s class=%s collision=%s faces=%zu boxes=%zu lines=%zu spheres=%zu",
                matched_instances,
                inst.name.c_str(),
                inst.model.c_str(),
                inst.entity_class.c_str(),
                col->name.c_str(),
                col->faces.size(),
                col->boxes.size(),
                col->lines.size(),
                col->spheres.size()
            );
        }

        for (const auto& face : col->faces) {
            if (face.a >= col->vertices.size() ||
                face.b >= col->vertices.size() ||
                face.c >= col->vertices.size()) {
                ++invalid_faces;
                continue;
            }

            const Vec3 a = mat4_transform_point(
                transform,
                col_to_vec3(col->vertices[face.a])
            );
            const Vec3 b = mat4_transform_point(
                transform,
                col_to_vec3(col->vertices[face.b])
            );
            const Vec3 d = mat4_transform_point(
                transform,
                col_to_vec3(col->vertices[face.c])
            );

            g_col_grid.add({a, b, d});
            ++mesh_faces;
        }

        for (const auto& box : col->boxes) {
            add_collision_box(box, transform);
            ++boxes;
        }

        // Lines are a first-class primitive in the COL format. Store
        // them for diagnostics now; actor/player line handling is done
        // through the original player COL below rather than turning
        // every entity line into a fake triangle.
        lines += col->lines.size();

        for (const auto& sphere : col->spheres) {
            const Vec3 center = mat4_transform_point(
                transform,
                col_to_vec3(sphere.center)
            );

            g_col_spheres_world.push_back({
                center,
                sphere.radius
            });
            ++spheres;
        }
    }

    LOGI(
        "COL inst collisions: matched=%zu skippedActors=%zu "
        "byTypeData=%zu byModelFallback=%zu meshFaces=%zu "
        "boxes=%zu lines=%zu spheres=%zu invalidFaces=%zu "
        "noCollisionData=%zu missingTypeDataCOL=%zu "
        "missingFallbackCOL=%zu unmatchedNames=%zu",
        matched_instances,
        skipped_actor_instances,
        matched_by_type_data,
        matched_by_model_fallback,
        mesh_faces,
        boxes,
        lines,
        spheres,
        invalid_faces,
        no_collision_data,
        missing_type_data_col,
        missing_model_fallback_col,
        unmatched_models.size()
    );

    size_t shown_unmatched = 0;
    for (const auto& pair : unmatched_models) {
        if (shown_unmatched++ >= 20) break;
        LOGI(
            "COL UNMATCHED[%zu]: collision=%s x%zu",
            shown_unmatched,
            pair.first.c_str(),
            pair.second
        );
    }

    size_t shown_type_unmatched = 0;
    for (const auto& pair : unmatched_type_data) {
        if (shown_type_unmatched++ >= 20) break;
        LOGI(
            "COL TYPE-DATA-MISSING[%zu]: collision=%s x%zu",
            shown_type_unmatched,
            pair.first.c_str(),
            pair.second
        );
    }

    size_t shown_no_collision_classes = 0;
    for (const auto& pair : no_collision_data_classes) {
        if (shown_no_collision_classes++ >= 20) break;
        LOGI(
            "COL NO-COLLISION-DATA CLASS[%zu]: class=%s x%zu",
            shown_no_collision_classes,
            pair.first.c_str(),
            pair.second
        );
    }
}


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
        
        // El BSP de Manhunt puede contener triángulos con winding
        // invertido. Para colisión vertical nos interesa la inclinación
        // de la superficie, no la orientación de su winding.
        if (fabsf(n.y) < 0.3f) continue;
        
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

// Busca el suelo considerando el pequeño volumen de apoyo del COL del
// jugador, no únicamente un punto central. Esto hace que los bordes y
// cambios de nivel sean más estables, especialmente al bajar escaleras.
static float find_player_floor(float px, float search_y, float pz) {
    float probe_radius = 0.25f;

    if (g_player_col_model) {
        for (const auto& sphere : g_player_col_model->spheres) {
            const float horizontal_radius =
                sqrtf(
                    sphere.center.x * sphere.center.x +
                    sphere.center.z * sphere.center.z
                ) +
                std::max(0.0f, sphere.radius);

            probe_radius = std::max(
                probe_radius,
                horizontal_radius * 0.70f
            );
        }
    }

    probe_radius = std::min(
        std::max(probe_radius, 0.10f),
        0.50f
    );

    const float offsets[5][2] = {
        { 0.0f,         0.0f         },
        { probe_radius, 0.0f         },
        {-probe_radius, 0.0f         },
        { 0.0f,         probe_radius },
        { 0.0f,        -probe_radius }
    };

    float best = -1e9f;

    for (const auto& offset : offsets) {
        const float floor_y = find_floor(
            px + offset[0],
            search_y,
            pz + offset[1]
        );

        if (floor_y > best) {
            best = floor_y;
        }
    }

    return best;
}

// Raycast vertical hacia arriba usando la geometría BSP del nivel.
// El origen del actor corresponde a y=0 del COL "player" y su altura
// real es 2.0 unidades en Asylum.
static float find_ceiling(float px, float search_y, float pz) {
    float best = 1e9f;
    const Vec3 ray_o = {px, search_y, pz};
    const Vec3 ray_d = {0, 1, 0};

    const std::vector<Tri>* tris = g_col_grid.get(px, pz);
    if (!tris) return best;

    for (const auto& tri : *tris) {
        const Vec3 e1 = {
            tri.b.x - tri.a.x,
            tri.b.y - tri.a.y,
            tri.b.z - tri.a.z
        };
        const Vec3 e2 = {
            tri.c.x - tri.a.x,
            tri.c.y - tri.a.y,
            tri.c.z - tri.a.z
        };

        Vec3 n = vec3_cross(e1, e2);
        const float nlen = sqrtf(
            n.x*n.x +
            n.y*n.y +
            n.z*n.z
        );
        if (nlen < 1e-6f) continue;

        n.x /= nlen;
        n.y /= nlen;
        n.z /= nlen;

        // Igual que el raycast de suelo, no depender del winding del
        // triángulo. El rayo hacia arriba descarta por sí mismo las
        // superficies que quedan detrás del origen.
        if (fabsf(n.y) < 0.30f) continue;

        const Vec3 h = vec3_cross(ray_d, e2);
        const float det = vec3_dot(e1, h);
        if (fabsf(det) < 1e-6f) continue;

        const float inv_det = 1.0f / det;
        const Vec3 rel = {
            ray_o.x - tri.a.x,
            ray_o.y - tri.a.y,
            ray_o.z - tri.a.z
        };

        const float u = vec3_dot(rel, h) * inv_det;
        if (u < 0.0f || u > 1.0f) continue;

        const Vec3 q = vec3_cross(rel, e1);
        const float v = vec3_dot(ray_d, q) * inv_det;
        if (v < 0.0f || u + v > 1.0f) continue;

        const float t = vec3_dot(e2, q) * inv_det;
        if (t < 0.0f || t > 200.0f) continue;

        const float hit_y = search_y + t;
        if (hit_y < best) best = hit_y;
    }

    return best;
}

static Vec3 rotate_player_local(Vec3 p) {
    const float c = cosf(g_player_yaw);
    const float s = sinf(g_player_yaw);

    return {
        g_player_pos.x + p.x * c + p.z * s,
        g_player_pos.y + p.y,
        g_player_pos.z - p.x * s + p.z * c
    };
}

static float point_triangle_distance_sq(Vec3 p, const Tri& tri) {
    const Vec3 ab = {
        tri.b.x - tri.a.x,
        tri.b.y - tri.a.y,
        tri.b.z - tri.a.z
    };
    const Vec3 ac = {
        tri.c.x - tri.a.x,
        tri.c.y - tri.a.y,
        tri.c.z - tri.a.z
    };
    const Vec3 ap = {
        p.x - tri.a.x,
        p.y - tri.a.y,
        p.z - tri.a.z
    };

    const float d1 = vec3_dot(ab, ap);
    const float d2 = vec3_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        const Vec3 d = {
            p.x - tri.a.x,
            p.y - tri.a.y,
            p.z - tri.a.z
        };
        return vec3_dot(d, d);
    }

    const Vec3 bp = {
        p.x - tri.b.x,
        p.y - tri.b.y,
        p.z - tri.b.z
    };
    const float d3 = vec3_dot(ab, bp);
    const float d4 = vec3_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        const Vec3 d = {
            p.x - tri.b.x,
            p.y - tri.b.y,
            p.z - tri.b.z
        };
        return vec3_dot(d, d);
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        const Vec3 q = {
            tri.a.x + ab.x * v,
            tri.a.y + ab.y * v,
            tri.a.z + ab.z * v
        };
        const Vec3 d = {
            p.x - q.x,
            p.y - q.y,
            p.z - q.z
        };
        return vec3_dot(d, d);
    }

    const Vec3 cp = {
        p.x - tri.c.x,
        p.y - tri.c.y,
        p.z - tri.c.z
    };
    const float d5 = vec3_dot(ab, cp);
    const float d6 = vec3_dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        const Vec3 d = {
            p.x - tri.c.x,
            p.y - tri.c.y,
            p.z - tri.c.z
        };
        return vec3_dot(d, d);
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        const Vec3 q = {
            tri.a.x + ac.x * w,
            tri.a.y + ac.y * w,
            tri.a.z + ac.z * w
        };
        const Vec3 d = {
            p.x - q.x,
            p.y - q.y,
            p.z - q.z
        };
        return vec3_dot(d, d);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const Vec3 bc = {
            tri.c.x - tri.b.x,
            tri.c.y - tri.b.y,
            tri.c.z - tri.b.z
        };
        const float w = (d4 - d3) /
                        ((d4 - d3) + (d5 - d6));
        const Vec3 q = {
            tri.b.x + bc.x * w,
            tri.b.y + bc.y * w,
            tri.b.z + bc.z * w
        };
        const Vec3 d = {
            p.x - q.x,
            p.y - q.y,
            p.z - q.z
        };
        return vec3_dot(d, d);
    }

    const Vec3 n = vec3_norm(vec3_cross(ab, ac));
    const float signed_dist =
        (p.x - tri.a.x) * n.x +
        (p.y - tri.a.y) * n.y +
        (p.z - tri.a.z) * n.z;

    return signed_dist * signed_dist;
}

static bool segment_intersects_triangle(
    Vec3 origin,
    Vec3 direction,
    const Tri& tri,
    float& t
) {
    const Vec3 e1 = {
        tri.b.x - tri.a.x,
        tri.b.y - tri.a.y,
        tri.b.z - tri.a.z
    };
    const Vec3 e2 = {
        tri.c.x - tri.a.x,
        tri.c.y - tri.a.y,
        tri.c.z - tri.a.z
    };

    const Vec3 h = vec3_cross(direction, e2);
    const float det = vec3_dot(e1, h);
    if (fabsf(det) < 1e-6f) return false;

    const float inv_det = 1.0f / det;
    const Vec3 s = {
        origin.x - tri.a.x,
        origin.y - tri.a.y,
        origin.z - tri.a.z
    };
    const float u = vec3_dot(s, h) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;

    const Vec3 q = vec3_cross(s, e1);
    const float v = vec3_dot(direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;

    t = vec3_dot(e2, q) * inv_det;
    return t >= 0.0f && t <= 1.0f;
}

static float segment_segment_distance_sq(
    Vec3 p1,
    Vec3 q1,
    Vec3 p2,
    Vec3 q2
) {
    const Vec3 d1 = {
        q1.x - p1.x,
        q1.y - p1.y,
        q1.z - p1.z
    };
    const Vec3 d2 = {
        q2.x - p2.x,
        q2.y - p2.y,
        q2.z - p2.z
    };
    const Vec3 r = {
        p1.x - p2.x,
        p1.y - p2.y,
        p1.z - p2.z
    };

    const float a = vec3_dot(d1, d1);
    const float e = vec3_dot(d2, d2);
    const float f = vec3_dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;

    if (a <= 1e-8f && e <= 1e-8f) {
        const Vec3 d = {
            p1.x - p2.x,
            p1.y - p2.y,
            p1.z - p2.z
        };
        return vec3_dot(d, d);
    }

    if (a <= 1e-8f) {
        s = 0.0f;
        t = std::max(0.0f, std::min(1.0f, f / e));
    } else {
        const float c = vec3_dot(d1, r);
        if (e <= 1e-8f) {
            t = 0.0f;
            s = std::max(0.0f, std::min(1.0f, -c / a));
        } else {
            const float b = vec3_dot(d1, d2);
            const float denom = a * e - b * b;

            if (denom != 0.0f) {
                s = std::max(
                    0.0f,
                    std::min(1.0f, (b * f - c * e) / denom)
                );
            }

            const float tnom = b * s + f;
            if (tnom < 0.0f) {
                t = 0.0f;
                s = std::max(0.0f, std::min(1.0f, -c / a));
            } else if (tnom > e) {
                t = 1.0f;
                s = std::max(
                    0.0f,
                    std::min(1.0f, (b - c) / a)
                );
            } else {
                t = tnom / e;
            }
        }
    }

    const Vec3 c1 = {
        p1.x + d1.x * s,
        p1.y + d1.y * s,
        p1.z + d1.z * s
    };
    const Vec3 c2 = {
        p2.x + d2.x * t,
        p2.y + d2.y * t,
        p2.z + d2.z * t
    };
    const Vec3 d = {
        c1.x - c2.x,
        c1.y - c2.y,
        c1.z - c2.z
    };
    return vec3_dot(d, d);
}

static float segment_triangle_distance_sq(
    Vec3 a,
    Vec3 b,
    const Tri& tri
) {
    const Vec3 direction = {
        b.x - a.x,
        b.y - a.y,
        b.z - a.z
    };

    float t = 0.0f;
    if (segment_intersects_triangle(a, direction, tri, t)) {
        return 0.0f;
    }

    float best = std::min(
        point_triangle_distance_sq(a, tri),
        point_triangle_distance_sq(b, tri)
    );

    best = std::min(
        best,
        segment_segment_distance_sq(a, b, tri.a, tri.b)
    );
    best = std::min(
        best,
        segment_segment_distance_sq(a, b, tri.b, tri.c)
    );
    best = std::min(
        best,
        segment_segment_distance_sq(a, b, tri.c, tri.a)
    );

    return best;
}

static bool player_collision_at(
    float px,
    float,
    float pz,
    Vec3* out_normal = nullptr,
    float* out_penetration = nullptr
) {
    if (!g_player_col_model) return false;

    if (out_normal) {
        *out_normal = {0.0f, 0.0f, 0.0f};
    }
    if (out_penetration) {
        *out_penetration = 0.0f;
    }

    const float yaw_c = cosf(g_player_yaw);
    const float yaw_s = sinf(g_player_yaw);

    const auto transform_local = [&](ColVec3 local) -> Vec3 {
        return {
            px + local.x * yaw_c + local.z * yaw_s,
            g_player_pos.y + local.y,
            pz - local.x * yaw_s + local.z * yaw_c
        };
    };

    Vec3 player_centers[8]{};
    float player_radii[8]{};
    size_t sphere_count = std::min<size_t>(
        g_player_col_model->spheres.size(),
        8
    );

    float max_radius = 0.0f;
    for (size_t i = 0; i < sphere_count; ++i) {
        player_centers[i] =
            transform_local(g_player_col_model->spheres[i].center);
        player_radii[i] =
            std::max(0.01f, g_player_col_model->spheres[i].radius);
        max_radius = std::max(max_radius, player_radii[i]);
    }

    Vec3 line_a{};
    Vec3 line_b{};
    bool has_line = false;

    if (!g_player_col_model->lines.empty()) {
        line_a = transform_local(g_player_col_model->lines[0].a);
        line_b = transform_local(g_player_col_model->lines[0].b);
        has_line = true;
    }

    const int center_cx = static_cast<int>(
        std::floor(px / g_col_grid.cell_size)
    );
    const int center_cz = static_cast<int>(
        std::floor(pz / g_col_grid.cell_size)
    );

    const int cell_radius = std::max(
        1,
        static_cast<int>(
            std::ceil(
                (max_radius + 0.25f) /
                g_col_grid.cell_size
            )
        )
    );

    const auto set_triangle_contact = [&](Vec3 n, Vec3 sample, float penetration) {
        if (out_normal) {
            if (vec3_dot(
                    {
                        sample.x,
                        sample.y,
                        sample.z
                    },
                    n
                ) < 0.0f) {
                n.x = -n.x;
                n.y = -n.y;
                n.z = -n.z;
            }

            *out_normal = vec3_norm(n);
        }

        if (out_penetration) {
            *out_penetration = std::max(
                *out_penetration,
                penetration
            );
        }
    };

    for (int cx = center_cx - cell_radius;
         cx <= center_cx + cell_radius;
         ++cx) {
        for (int cz = center_cz - cell_radius;
             cz <= center_cz + cell_radius;
             ++cz) {
            const auto it = g_col_grid.cells.find({cx, cz});
            if (it == g_col_grid.cells.end()) continue;

            for (const auto& tri : it->second) {
                const Vec3 e1 = {
                    tri.b.x - tri.a.x,
                    tri.b.y - tri.a.y,
                    tri.b.z - tri.a.z
                };
                const Vec3 e2 = {
                    tri.c.x - tri.a.x,
                    tri.c.y - tri.a.y,
                    tri.c.z - tri.a.z
                };

                Vec3 n = vec3_cross(e1, e2);
                const float nlen = sqrtf(
                    n.x*n.x +
                    n.y*n.y +
                    n.z*n.z
                );
                if (nlen < 1e-6f) continue;

                n.x /= nlen;
                n.y /= nlen;
                n.z /= nlen;

                // Movimiento horizontal: suprimir suelo y techos.
                if (fabsf(n.y) > 0.70f) continue;

                for (size_t i = 0; i < sphere_count; ++i) {
                    const float dist_sq =
                        point_triangle_distance_sq(
                            player_centers[i],
                            tri
                        );

                    const float r = player_radii[i];
                    if (dist_sq <= r * r) {
                        const float distance = sqrtf(
                            std::max(0.0f, dist_sq)
                        );
                        set_triangle_contact(
                            n,
                            {
                                player_centers[i].x - tri.a.x,
                                player_centers[i].y - tri.a.y,
                                player_centers[i].z - tri.a.z
                            },
                            std::max(0.0f, r - distance)
                        );
                        return true;
                    }
                }

                if (has_line) {
                    const Vec3 line_dir = {
                        line_b.x - line_a.x,
                        line_b.y - line_a.y,
                        line_b.z - line_a.z
                    };

                    float hit_t = 0.0f;
                    if (segment_intersects_triangle(
                            line_a,
                            line_dir,
                            tri,
                            hit_t
                        )) {
                        set_triangle_contact(
                            n,
                            {
                                line_a.x - tri.a.x,
                                line_a.y - tri.a.y,
                                line_a.z - tri.a.z
                            },
                            0.0f
                        );
                        return true;
                    }
                }
            }
        }
    }

    // Colisiones esfera-esfera de las instancias COL.
    for (const auto& object_sphere : g_col_spheres_world) {
        for (size_t i = 0; i < sphere_count; ++i) {
            const Vec3 d = {
                player_centers[i].x - object_sphere.first.x,
                player_centers[i].y - object_sphere.first.y,
                player_centers[i].z - object_sphere.first.z
            };

            const float radius =
                player_radii[i] + object_sphere.second;

            if (vec3_dot(d, d) <= radius * radius) {
                const float distance = sqrtf(
                    std::max(0.0f, vec3_dot(d, d))
                );

                if (out_normal) {
                    Vec3 n = d;

                    if (distance > 1e-6f) {
                        n.x /= distance;
                        n.y /= distance;
                        n.z /= distance;
                    } else {
                        n = {1.0f, 0.0f, 0.0f};
                    }

                    *out_normal = n;
                }

                if (out_penetration) {
                    *out_penetration = std::max(
                        *out_penetration,
                        std::max(0.0f, radius - distance)
                    );
                }

                return true;
            }
        }

        if (has_line) {
            const Vec3 seg = {
                line_b.x - line_a.x,
                line_b.y - line_a.y,
                line_b.z - line_a.z
            };

            const float len_sq = vec3_dot(seg, seg);
            float u = 0.0f;

            if (len_sq > 1e-8f) {
                const Vec3 to_sphere = {
                    object_sphere.first.x - line_a.x,
                    object_sphere.first.y - line_a.y,
                    object_sphere.first.z - line_a.z
                };

                u = vec3_dot(to_sphere, seg) / len_sq;
                u = std::max(
                    0.0f,
                    std::min(1.0f, u)
                );
            }

            const Vec3 closest = {
                line_a.x + seg.x * u,
                line_a.y + seg.y * u,
                line_a.z + seg.z * u
            };

            const Vec3 d = {
                closest.x - object_sphere.first.x,
                closest.y - object_sphere.first.y,
                closest.z - object_sphere.first.z
            };

            const float radius =
                max_radius + object_sphere.second;

            if (vec3_dot(d, d) <= radius * radius) {
                const float distance = sqrtf(
                    std::max(0.0f, vec3_dot(d, d))
                );

                if (out_normal) {
                    // Normal desde la superficie del objeto hacia
                    // la parte del jugador que hizo contacto.
                    Vec3 n = {
                        closest.x - object_sphere.first.x,
                        closest.y - object_sphere.first.y,
                        closest.z - object_sphere.first.z
                    };

                    if (distance > 1e-6f) {
                        n.x /= distance;
                        n.y /= distance;
                        n.z /= distance;
                    } else {
                        n = {1.0f, 0.0f, 0.0f};
                    }

                    *out_normal = n;
                }

                if (out_penetration) {
                    *out_penetration = std::max(
                        *out_penetration,
                        std::max(0.0f, radius - distance)
                    );
                }

                return true;
            }
        }
    }

    return false;
}

static void recover_player_penetration() {
    if (!g_player_col_model) return;

    for (int iteration = 0; iteration < 3; ++iteration) {
        Vec3 normal{};
        float penetration = 0.0f;

        if (!player_collision_at(
                g_player_pos.x,
                g_player_pos.y + 1.0f,
                g_player_pos.z,
                &normal,
                &penetration
            )) {
            return;
        }

        // El contacto exacto no es una penetración y no debe producir
        // jitter contra paredes.
        if (penetration <= 0.02f) {
            return;
        }

        Vec3 horizontal_normal = {
            normal.x,
            0.0f,
            normal.z
        };

        const float len = sqrtf(
            horizontal_normal.x * horizontal_normal.x +
            horizontal_normal.z * horizontal_normal.z
        );

        if (len < 1e-5f) {
            return;
        }

        horizontal_normal.x /= len;
        horizontal_normal.z /= len;

        const float push = penetration + 0.03f;

        // La orientación de los triángulos del BSP no es fiable, así que
        // probamos ambos sentidos y elegimos el que realmente libera al
        // actor. Esto cubre paredes donde el jugador ya quedó parcialmente
        // dentro y también esquinas.
        const float candidates[2][2] = {
            {
                horizontal_normal.x * push,
                horizontal_normal.z * push
            },
            {
                -horizontal_normal.x * push,
                -horizontal_normal.z * push
            }
        };

        bool recovered = false;

        for (const auto& candidate : candidates) {
            const float x = g_player_pos.x + candidate[0];
            const float z = g_player_pos.z + candidate[1];

            float remaining_penetration = 0.0f;
            if (!player_collision_at(
                    x,
                    g_player_pos.y + 1.0f,
                    z,
                    nullptr,
                    &remaining_penetration
                ) ||
                remaining_penetration < penetration * 0.25f) {
                g_player_pos.x = x;
                g_player_pos.z = z;
                recovered = true;
                break;
            }
        }

        if (!recovered) {
            return;
        }
    }
}

static bool hit_wall(
    float,
    float,
    float,
    float x2,
    float y2,
    float z2
) {
    // La prueba se hace sobre la forma de colisión real del jugador
    // en la posición de destino. El resolvedor de movimiento de abajo
    // conserva el deslizamiento por X/Z.
    if (g_player_col_model) {
        if (player_collision_at(x2, 0.0f, z2)) {
            return true;
        }
        return false;
    }

    // Fallback únicamente si el modelo player del COL no está disponible.
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

            const size_t skin_matrix_count =
                g_cash_model.inverse_bind_matrices.size() / 16;

            g_cash_skinning_enabled =
                !g_cash_model.bones.empty() &&
                skin_matrix_count > 0;

            LOGI(
                "CASH SKINNING: enabled=%s frames=%zu "
                "skinMatrices=%zu skinRemap=%zu",
                g_cash_skinning_enabled ? "YES" : "NO",
                g_cash_model.bones.size(),
                skin_matrix_count,
                g_cash_model.skin_bone_to_frame.size()
            );

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

        auto inst2_raw = read_asset("levels/asylum/entity2.inst");
        if (!inst2_raw.empty()) {
            auto inst2 = parse_inst(inst2_raw);
            g_insts.insert(g_insts.end(), inst2.begin(), inst2.end());
            LOGI("Instancias totales: %zu", g_insts.size());
        }
    }

    auto col_raw = read_asset("levels/asylum/collisions.col");
    if (!col_raw.empty()) {
        g_col_models = col_load_all(col_raw.data(), col_raw.size());

        g_player_col_model = nullptr;
        for (const auto& model : g_col_models) {
            if (normalize_col_name(model.name) == "player") {
                g_player_col_model = &model;
                break;
            }
        }

        if (g_player_col_model) {
            g_player_collision_height =
                std::max(
                    0.1f,
                    g_player_col_model->max.y -
                    g_player_col_model->min.y
                );

            LOGI(
                "PLAYER COL: spheres=%zu lines=%zu height=%.3f boundsY=(%.3f,%.3f)",
                g_player_col_model->spheres.size(),
                g_player_col_model->lines.size(),
                g_player_collision_height,
                g_player_col_model->min.y,
                g_player_col_model->max.y
            );

            for (size_t i = 0; i < g_player_col_model->spheres.size(); ++i) {
                const auto& sphere = g_player_col_model->spheres[i];
                LOGI(
                    "PLAYER COL SPHERE[%zu]: center=(%.3f,%.3f,%.3f) radius=%.3f",
                    i,
                    sphere.center.x,
                    sphere.center.y,
                    sphere.center.z,
                    sphere.radius
                );
            }

            for (size_t i = 0; i < g_player_col_model->lines.size(); ++i) {
                const auto& line = g_player_col_model->lines[i];
                LOGI(
                    "PLAYER COL LINE[%zu]: A=(%.3f,%.3f,%.3f) B=(%.3f,%.3f,%.3f)",
                    i,
                    line.a.x,
                    line.a.y,
                    line.a.z,
                    line.b.x,
                    line.b.y,
                    line.b.z
                );
            }
        } else {
            LOGE("PLAYER COL: modelo 'player' no encontrado");
        }

        g_col_spheres_world.clear();

        // El juego define qué COL usar mediante COLLISION_DATA en
        // entityTypeData.ini. En nuestro export de ManHunt.pak el
        // archivo de Asylum está conservado en la misma estructura
        // de directorios del PAK original.
        // Cada nivel conserva su propio entityTypeData.ini. Por
        // ahora el renderer está cargando Asylum, así que usamos su
        // directorio como fuente principal y mantenemos los paths legacy
        // solo como fallback.
        const std::string level_name = "Asylum";

        const std::vector<std::string> type_data_paths = {
            "export/ManHunt#pak/levels/" +
                level_name +
                "/entityTypeData.ini",
            "export/ManHunt#pak/levels/" +
                level_name +
                "/entityTypeData.INI",
            "export/ManHunt#pak/levels/asylum/entityTypeData.ini",
            "entityTypeData.ini",
            "levels/GLOBAL/entityTypeData.ini",
            "levels/global/entityTypeData.ini",
            "levels/GLOBAL/AllEntitiesTypeData.ini",
            "levels/global/AllEntitiesTypeData.ini"
        };

        g_entity_collision_data.clear();
        g_player_control = PlayerControlConfig{};

        for (const auto& type_path : type_data_paths) {
            auto type_raw = read_asset(type_path.c_str());
            if (!type_raw.empty()) {
                LOGI(
                    "ENTITY TYPE DATA cargado: %s (%zu bytes)",
                    type_path.c_str(),
                    type_raw.size()
                );
                parse_entity_type_data(type_raw);

                if (!g_entity_collision_data.empty()) {
                    break;
                }
            }
        }

        if (g_entity_collision_data.empty()) {
            LOGI(
                "ENTITY TYPE DATA no disponible; fallback modelo->COL"
            );
        }

        rebuild_col_inst_collisions();
    } else {
        LOGE("No se encontró levels/asylum/collisions.col");
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
    // Primera prueba del skinning corregido: pose base, sin IFP.
    // Así verificamos Skin + FrameList por separado antes de mezclar
    // animación.
    g_debug_anim_idx = -1;
    g_cash_skinning_enabled = false;
    g_cash_skin_convention = -1;
    g_cash_pos_adjust = {0.0f, 1.0f, 0.0f};
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
    // Si por un borde o un contacto anterior el actor quedó parcialmente
    // dentro de una pared, primero lo sacamos antes de procesar el nuevo
    // desplazamiento. El contacto exacto (penetración <= 0.02) se conserva.
    recover_player_penetration();

    float cam_cy = cosf(g_cam_yaw), cam_sy = sinf(g_cam_yaw);
    Vec3 fwd_xz   = {-cam_sy, 0, -cam_cy};
    Vec3 right_xz = { cam_cy, 0, -cam_sy};

    Vec3 vel_xz = vec3_add(
        vec3_scale(fwd_xz,   g_move_fwd),
        vec3_scale(right_xz, g_move_right)
    );

    // Aplicar el dead zone del stick original. El juego recalibra la
    // distancia para que el borde de la zona muerta sea el nuevo 0.
    float raw_input_strength = sqrtf(
        vel_xz.x * vel_xz.x +
        vel_xz.z * vel_xz.z
    );

    raw_input_strength = std::min(
        raw_input_strength,
        1.0f
    );

    float input_strength = 0.0f;

    if (raw_input_strength > g_player_control.stick_dead_zone) {
        input_strength =
            (raw_input_strength -
             g_player_control.stick_dead_zone) /
            std::max(
                0.001f,
                1.0f - g_player_control.stick_dead_zone
            );

        input_strength = std::max(
            0.0f,
            std::min(1.0f, input_strength)
        );
    }

    if (input_strength > 0.01f) {
        // Mantener dirección, pero usar la intensidad recalibrada.
        const float inv =
            1.0f / std::max(raw_input_strength, 1e-6f);

        vel_xz.x *= inv * input_strength;
        vel_xz.z *= inv * input_strength;

        g_player_yaw = atan2f(
            -vel_xz.x,
            -vel_xz.z
        );
    } else {
        vel_xz = {0.0f, 0.0f, 0.0f};
    }

    // MOVE_THRESHOLDS del juego define las zonas del stick:
    // por debajo de walk = movimiento lento, entre walk/run = walk,
    // y desde run = run. Sprint todavía no tiene botón dedicado en
    // nuestro control, así que el máximo analógico llega hasta run.
    float movement_multiplier = g_player_control.walk_speed;

    if (input_strength < g_player_control.move_walk_threshold) {
        movement_multiplier =
            g_player_control.sneak_walk_speed;
    } else if (input_strength >=
               g_player_control.move_run_threshold ||
               input_strength >= g_player_control.run_threshold) {
        movement_multiplier =
            g_player_control.run_speed;
    }

    const float move_dx =
        vel_xz.x * MOVE_SPEED * movement_multiplier * dt;
    const float move_dz =
        vel_xz.z * MOVE_SPEED * movement_multiplier * dt;

    // Resolver el desplazamiento en pequeños pasos evita atravesar
    // superficies finas cuando un frame produce un movimiento grande.
    // La detección sigue usando las 2 esferas + línea del COL "player".
    const float move_distance = sqrtf(
        move_dx * move_dx +
        move_dz * move_dz
    );
    const float max_collision_step = 0.10f;
    const int move_steps = std::max(
        1,
        std::min(
            16,
            static_cast<int>(
                std::ceil(move_distance / max_collision_step)
            )
        )
    );

    const float step_dx = move_dx / static_cast<float>(move_steps);
    const float step_dz = move_dz / static_cast<float>(move_steps);

    for (int step = 0; step < move_steps; ++step) {
        const float current_waist_y = g_player_pos.y + 1.0f;
        const float wanted_x = g_player_pos.x + step_dx;
        const float wanted_z = g_player_pos.z + step_dz;

        // Si ya estamos sobre el suelo, seguimos el próximo punto del
        // terreno de forma continua. Esto evita que una pendiente larga
        // sea interpretada como una pared o como muchos escalones.
        if (g_on_ground) {
            const float current_floor = find_player_floor(
                g_player_pos.x,
                g_player_pos.y + 2.0f,
                g_player_pos.z
            );
            const float wanted_floor = find_player_floor(
                wanted_x,
                g_player_pos.y + 2.0f,
                wanted_z
            );

            if (current_floor > -1e8f &&
                wanted_floor > -1e8f) {
                const float floor_delta =
                    wanted_floor - current_floor;

                if (fabsf(floor_delta) <=
                        PLAYER_GROUND_FOLLOW_HEIGHT + 0.001f) {
                    // Solo seguimos cambios de altura pequeños. La
                    // comprobación normal de COL sigue siendo obligatoria
                    // antes de aceptar la nueva posición horizontal.
                    Vec3 follow_normal{};
                    if (!player_collision_at(
                            wanted_x,
                            current_waist_y,
                            wanted_z,
                            &follow_normal
                        )) {
                        g_player_pos.x = wanted_x;
                        g_player_pos.z = wanted_z;
                        g_player_pos.y = wanted_floor;
                        g_vel_y = 0.0f;
                        g_on_ground = true;
                        continue;
                    }

                    // Una pendiente caminable puede hacer contacto con
                    // la esfera inferior durante el avance. No dejamos
                    // que ese contacto se convierta en una pared.
                    if (fabsf(follow_normal.y) >=
                            PLAYER_MAX_SLOPE_Y &&
                        floor_delta >= -0.05f) {
                        g_player_pos.x = wanted_x;
                        g_player_pos.z = wanted_z;
                        g_player_pos.y = wanted_floor;
                        g_vel_y = 0.0f;
                        g_on_ground = true;
                        continue;
                    }
                }
            }
        }

        Vec3 collision_normal{};
        if (!player_collision_at(
                wanted_x,
                current_waist_y,
                wanted_z,
                &collision_normal
            )) {
            g_player_pos.x = wanted_x;
            g_player_pos.z = wanted_z;
            continue;
        }

        // Manhunt permite que el actor suba pequeños escalones sin
        // saltar. Primero intentamos resolver el contacto como un
        // "step-up": si en la posición deseada hay un suelo apenas más
        // alto que el actual, desplazamos al actor hasta esa cota y
        // dejamos que el resolvedor vertical siga controlando el resto.
        const float current_floor = find_player_floor(
            g_player_pos.x,
            g_player_pos.y + 2.0f,
            g_player_pos.z
        );
        const float wanted_floor = find_player_floor(
            wanted_x,
            g_player_pos.y + PLAYER_MAX_STEP_HEIGHT + 2.0f,
            wanted_z
        );

        if (current_floor > -1e8f &&
            wanted_floor > -1e8f &&
            wanted_floor > current_floor + 0.01f &&
            wanted_floor - current_floor <= PLAYER_MAX_STEP_HEIGHT + 0.001f) {
            g_player_pos.x = wanted_x;
            g_player_pos.z = wanted_z;
            g_player_pos.y = wanted_floor;
            g_vel_y = 0.0f;
            g_on_ground = true;
            continue;
        }

        // Resolver el contacto proyectando el desplazamiento sobre
        // el plano tangente de la superficie. Esto permite deslizarse
        // por una pared en vez de quedar pegado a ella.
        Vec3 horizontal_normal = {
            collision_normal.x,
            0.0f,
            collision_normal.z
        };

        const float normal_len = sqrtf(
            horizontal_normal.x * horizontal_normal.x +
            horizontal_normal.z * horizontal_normal.z
        );

        bool moved_by_slide = false;

        if (normal_len > 1e-5f) {
            horizontal_normal.x /= normal_len;
            horizontal_normal.z /= normal_len;

            const float into_wall =
                step_dx * horizontal_normal.x +
                step_dz * horizontal_normal.z;

            float slide_dx = step_dx;
            float slide_dz = step_dz;

            if (into_wall < 0.0f) {
                slide_dx -=
                    horizontal_normal.x * into_wall;
                slide_dz -=
                    horizontal_normal.z * into_wall;
            }

            const float slide_len_sq =
                slide_dx * slide_dx +
                slide_dz * slide_dz;

            if (slide_len_sq > 1e-8f) {
                const float slide_x =
                    g_player_pos.x + slide_dx;
                const float slide_z =
                    g_player_pos.z + slide_dz;

                if (!player_collision_at(
                        slide_x,
                        current_waist_y,
                        slide_z
                    )) {
                    g_player_pos.x = slide_x;
                    g_player_pos.z = slide_z;
                    moved_by_slide = true;
                }
            }
        }

        if (!moved_by_slide) {
            // En esquinas, un único vector normal puede apuntar justo
            // hacia la segunda pared y dejar al actor prácticamente
            // inmóvil. Probamos las dos componentes cardinales del
            // desplazamiento original por separado. Esto no atraviesa
            // geometría porque cada destino se vuelve a comprobar con
            // el COL completo del jugador.
            const float candidates[4][2] = {
                { step_dx, 0.0f },
                { 0.0f, step_dz },
                { step_dx * 0.5f, step_dz },
                { step_dx, step_dz * 0.5f }
            };

            float best_dx = 0.0f;
            float best_dz = 0.0f;
            float best_len_sq = 0.0f;

            for (const auto& candidate : candidates) {
                const float candidate_x =
                    g_player_pos.x + candidate[0];
                const float candidate_z =
                    g_player_pos.z + candidate[1];

                if (player_collision_at(
                        candidate_x,
                        current_waist_y,
                        candidate_z
                    )) {
                    continue;
                }

                const float len_sq =
                    candidate[0] * candidate[0] +
                    candidate[1] * candidate[1];

                if (len_sq > best_len_sq) {
                    best_len_sq = len_sq;
                    best_dx = candidate[0];
                    best_dz = candidate[1];
                }
            }

            if (best_len_sq > 1e-8f) {
                g_player_pos.x += best_dx;
                g_player_pos.z += best_dz;
            }
        }
    }

    const float waist_y = g_player_pos.y + 1.0f;

    // Gravedad
    g_vel_y += GRAVITY * dt;

    // Resolver el movimiento vertical en pasos pequeños, igual que
    // el desplazamiento horizontal, para evitar atravesar suelo o techo.
    const float vertical_move = g_vel_y * dt;
    const float vertical_distance = fabsf(vertical_move);
    const float max_vertical_step = 0.10f;
    const int vertical_steps = std::max(
        1,
        std::min(
            16,
            static_cast<int>(
                std::ceil(
                    vertical_distance /
                    max_vertical_step
                )
            )
        )
    );

    const float vertical_step =
        vertical_move /
        static_cast<float>(vertical_steps);

    for (int step = 0; step < vertical_steps; ++step) {
        const float next_y =
            g_player_pos.y + vertical_step;

        if (vertical_step < 0.0f) {
            // El punto de apoyo del player COL está en local Y=0.
            // Por eso el suelo se traduce directamente en la posición
            // vertical del actor.
            const float floor_y = find_player_floor(
                g_player_pos.x,
                std::max(
                    g_player_pos.y + 1.0f,
                    next_y + 2.0f
                ),
                g_player_pos.z
            );

            // En una pendiente el suelo puede quedar ligeramente por
            // encima de la posición actual durante el avance horizontal.
            // Permitimos recuperar hasta la misma altura máxima que un
            // escalón pequeño, pero nunca "teletransportar" al actor a una
            // plataforma mucho más alta.
            if (floor_y > -1e8f &&
                floor_y <= g_player_pos.y +
                    PLAYER_MAX_STEP_HEIGHT + 0.02f &&
                next_y < floor_y) {
                g_player_pos.y = floor_y;
                g_vel_y = 0.0f;
                g_on_ground = true;
                continue;
            }
        } else {
            // La parte superior del player COL está en Y=2.0.
            // Impedimos atravesar techos mientras asciende.
            const float head_y = next_y + g_player_collision_height;
            const float ceiling_y = find_ceiling(
                g_player_pos.x,
                g_player_pos.y + 1.5f,
                g_player_pos.z
            );

            if (ceiling_y < 1e8f &&
                head_y > ceiling_y) {
                g_player_pos.y =
                    ceiling_y -
                    g_player_collision_height;
                g_vel_y = 0.0f;
                continue;
            }
        }

        g_player_pos.y = next_y;
        g_on_ground = false;
    }

    // Una comprobación final evita quedar ligeramente por debajo del suelo
    // por errores de redondeo después del último paso.
    const float final_floor_y = find_player_floor(
        g_player_pos.x,
        g_player_pos.y + 1.0f,
        g_player_pos.z
    );

    if (final_floor_y > -1e8f &&
        final_floor_y <= g_player_pos.y +
            PLAYER_MAX_STEP_HEIGHT + 0.02f &&
        g_player_pos.y < final_floor_y) {
        g_player_pos.y = final_floor_y;
        g_vel_y = 0.0f;
        g_on_ground = true;
    }

    // ── Cámara Orbit ───────────────────────────────────────────────────────
    const Vec3 target = {
        g_player_pos.x,
        g_player_pos.y + 1.5f,
        g_player_pos.z
    };

    Vec3 cam_pos;
    cam_pos.x =
        g_player_pos.x +
        sinf(g_cam_yaw) *
        cosf(g_cam_pitch) *
        g_cam_dist;
    cam_pos.y =
        g_player_pos.y +
        1.5f -
        sinf(g_cam_pitch) *
        g_cam_dist;
    cam_pos.z =
        g_player_pos.z +
        cosf(g_cam_yaw) *
        cosf(g_cam_pitch) *
        g_cam_dist;

    // Cámara de tercera persona: si una pared queda entre Cash y la
    // posición deseada de la cámara, acercamos la cámara al jugador en
    // vez de permitir que atraviese la geometría.
    {
        const Vec3 camera_delta = {
            cam_pos.x - target.x,
            cam_pos.y - target.y,
            cam_pos.z - target.z
        };

        float nearest_t = 1.0f;
        const std::vector<Tri>* camera_tris =
            g_col_grid.get(target.x, target.z);

        // La cámara puede cruzar celdas durante el trayecto. Recorremos
        // una región basada en su longitud para no depender solo de la
        // celda donde está el jugador.
        const float horizontal_length = sqrtf(
            camera_delta.x * camera_delta.x +
            camera_delta.z * camera_delta.z
        );
        const int camera_cell_radius =
            std::max(
                1,
                static_cast<int>(
                    std::ceil(
                        horizontal_length /
                        g_col_grid.cell_size
                    )
                )
            );

        const int target_cx = static_cast<int>(
            std::floor(target.x / g_col_grid.cell_size)
        );
        const int target_cz = static_cast<int>(
            std::floor(target.z / g_col_grid.cell_size)
        );

        for (int cx = target_cx - camera_cell_radius;
             cx <= target_cx + camera_cell_radius;
             ++cx) {
            for (int cz = target_cz - camera_cell_radius;
                 cz <= target_cz + camera_cell_radius;
                 ++cz) {
                const auto it =
                    g_col_grid.cells.find({cx, cz});
                if (it == g_col_grid.cells.end()) continue;

                for (const auto& tri : it->second) {
                    Vec3 n = vec3_norm(
                        vec3_cross(
                            {
                                tri.b.x - tri.a.x,
                                tri.b.y - tri.a.y,
                                tri.b.z - tri.a.z
                            },
                            {
                                tri.c.x - tri.a.x,
                                tri.c.y - tri.a.y,
                                tri.c.z - tri.a.z
                            }
                        )
                    );

                    // Para la cámara ignoramos suelo/techo: lo que nos
                    // importa aquí son las superficies que realmente
                    // pueden ocultar al personaje desde atrás.
                    if (fabsf(n.y) > 0.70f) continue;

                    float hit_t = 0.0f;
                    if (segment_intersects_triangle(
                            target,
                            camera_delta,
                            tri,
                            hit_t
                        ) &&
                        hit_t >= 0.0f &&
                        hit_t < nearest_t) {
                        nearest_t = hit_t;
                    }
                }
            }
        }

        if (nearest_t < 1.0f) {
            // Dejamos una separación mínima para evitar que la cámara
            // termine justo dentro de la pared por redondeo.
            const float camera_wall_margin = 0.12f;
            const float safe_t = std::max(
                0.05f,
                nearest_t - camera_wall_margin /
                    std::max(
                        0.001f,
                        sqrtf(
                            camera_delta.x * camera_delta.x +
                            camera_delta.y * camera_delta.y +
                            camera_delta.z * camera_delta.z
                        )
                    )
            );

            cam_pos.x =
                target.x + camera_delta.x * safe_t;
            cam_pos.y =
                target.y + camera_delta.y * safe_t;
            cam_pos.z =
                target.z + camera_delta.z * safe_t;
        }

        (void)camera_tris;
    }

    // ── Render ────────────────────────────────────────────────────────────
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
            // Usar la misma intensidad de movimiento que ya fue
            // recalibrada con STICK_DEAD_ZONE y MOVE_THRESHOLDS.
            const float animation_input = input_strength;

            auto find_anim = [&](const char* wanted) -> const Animation* {
                const std::string query = to_lower(wanted);
                for (const auto& pair : g_anims) {
                    if (to_lower(pair.first) == query) {
                        return &pair.second;
                    }
                }
                return nullptr;
            };

            if (animation_input >=
                    std::max(
                        g_player_control.move_run_threshold,
                        g_player_control.run_threshold
                    )) {
                anim = find_anim("Run_Fwd");
            } else if (animation_input >=
                       g_player_control.move_walk_threshold) {
                anim = find_anim("Walk_Fwd");
            } else if (animation_input > 0.01f) {
                // El archivo define una zona por debajo de WALK.
                // Todavía no tenemos el estado de sigilo separado en
                // nuestro control, así que mantenemos la animación de
                // caminar antes que inventar una animación nueva.
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
        // ------------------------------------------------------------
        // Resolver automáticamente la convención de Skin en bind pose.
        // ------------------------------------------------------------
        if (g_cash_skin_convention < 0 &&
            !g_cash_model.inverse_bind_matrices.empty() &&
            !g_cash_model.bones.empty() &&
            g_debug_anim_idx == -1) {

            Mat4 candidate_globals[4][96];

            auto make_bind_local = [&](size_t idx,
                                       bool transpose_rot) -> Mat4 {
                const DFFBone& b = g_cash_model.bones[idx];
                Mat4 m = mat4_identity();

                if (!transpose_rot) {
                    m.m[0] = b.rot_mat[0];
                    m.m[1] = b.rot_mat[1];
                    m.m[2] = b.rot_mat[2];
                    m.m[4] = b.rot_mat[3];
                    m.m[5] = b.rot_mat[4];
                    m.m[6] = b.rot_mat[5];
                    m.m[8] = b.rot_mat[6];
                    m.m[9] = b.rot_mat[7];
                    m.m[10] = b.rot_mat[8];
                } else {
                    m.m[0] = b.rot_mat[0];
                    m.m[1] = b.rot_mat[3];
                    m.m[2] = b.rot_mat[6];
                    m.m[4] = b.rot_mat[1];
                    m.m[5] = b.rot_mat[4];
                    m.m[6] = b.rot_mat[7];
                    m.m[8] = b.rot_mat[2];
                    m.m[9] = b.rot_mat[5];
                    m.m[10] = b.rot_mat[8];
                }

                m.m[12] = b.pos_x;
                m.m[13] = b.pos_y;
                m.m[14] = b.pos_z;
                return m;
            };

            auto build_globals = [&](Mat4* out,
                                     bool transpose_rot,
                                     bool local_parent_first) {
                for (int i = 0; i < 96; ++i) {
                    out[i] = mat4_identity();
                }

                int state[96] = {};
                auto visit = [&](auto&& self, size_t idx) -> void {
                    if (idx >= g_cash_model.bones.size() ||
                        idx >= 96) {
                        return;
                    }

                    if (state[idx] == 2) {
                        return;
                    }

                    const Mat4 local =
                        make_bind_local(idx, transpose_rot);

                    if (state[idx] == 1) {
                        out[idx] = local;
                        state[idx] = 2;
                        return;
                    }

                    state[idx] = 1;
                    const uint32_t parent =
                        g_cash_model.bones[idx].parent;

                    if (parent != 0xFFFFFFFF &&
                        parent < g_cash_model.bones.size() &&
                        parent < 96 &&
                        parent != idx) {
                        self(self, static_cast<size_t>(parent));

                        if (local_parent_first) {
                            out[idx] =
                                mat4_mul(local, out[parent]);
                        } else {
                            out[idx] =
                                mat4_mul(out[parent], local);
                        }
                    } else {
                        out[idx] = local;
                    }

                    state[idx] = 2;
                };

                for (size_t i = 0;
                     i < g_cash_model.bones.size() &&
                     i < 96;
                     ++i) {
                    visit(visit, i);
                }
            };

            build_globals(candidate_globals[0], false, false);
            build_globals(candidate_globals[1], true,  false);
            build_globals(candidate_globals[2], false, true);
            build_globals(candidate_globals[3], true,  true);

            const size_t skin_bone_count =
                g_cash_model.inverse_bind_matrices.size() / 16;

            auto transpose_matrix = [](const Mat4& src) -> Mat4 {
                Mat4 dst = {0};
                for (int row = 0; row < 4; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        dst.m[col * 4 + row] =
                            src.m[row * 4 + col];
                    }
                }
                return dst;
            };

            float best_score = 1e30f;
            float best_max_error = 1e30f;
            int best_convention = 0;

            for (int frame_mode = 0; frame_mode < 4; ++frame_mode) {
                for (int remap = 0; remap < 2; ++remap) {
                    for (int inv_transpose = 0;
                         inv_transpose < 2;
                         ++inv_transpose) {
                        for (int inv_first = 0;
                             inv_first < 2;
                             ++inv_first) {

                            float sum_error = 0.0f;
                            float max_error = 0.0f;
                            size_t sample_count = 0;

                            for (size_t skin_bone = 0;
                                 skin_bone < skin_bone_count &&
                                 skin_bone < 96;
                                 ++skin_bone) {

                                size_t frame_index = skin_bone;

                                if (remap &&
                                    skin_bone <
                                    g_cash_model
                                        .skin_bone_to_frame.size()) {
                                    frame_index =
                                        g_cash_model
                                            .skin_bone_to_frame[
                                                skin_bone
                                            ];
                                }

                                if (frame_index >=
                                    g_cash_model.bones.size() ||
                                    frame_index >= 96) {
                                    continue;
                                }

                                Mat4 inverse_bind;
                                for (int j = 0; j < 16; ++j) {
                                    inverse_bind.m[j] =
                                        g_cash_model
                                            .inverse_bind_matrices[
                                                skin_bone * 16 + j
                                            ];
                                }

                                if (inv_transpose) {
                                    inverse_bind =
                                        transpose_matrix(
                                            inverse_bind
                                        );
                                }

                                const Mat4 skin =
                                    inv_first
                                    ? mat4_mul(
                                        inverse_bind,
                                        candidate_globals[
                                            frame_mode
                                        ][frame_index]
                                    )
                                    : mat4_mul(
                                        candidate_globals[
                                            frame_mode
                                        ][frame_index],
                                        inverse_bind
                                    );

                                float matrix_error = 0.0f;
                                for (int j = 0; j < 16; ++j) {
                                    const float target =
                                        (j == 0 || j == 5 ||
                                         j == 10 || j == 15)
                                        ? 1.0f : 0.0f;
                                    const float err =
                                        fabsf(
                                            skin.m[j] - target
                                        );
                                    matrix_error += err;
                                    max_error =
                                        std::max(
                                            max_error,
                                            err
                                        );
                                }

                                sum_error += matrix_error;
                                ++sample_count;
                            }

                            if (sample_count == 0) {
                                continue;
                            }

                            const float average_error =
                                sum_error /
                                static_cast<float>(
                                    sample_count
                                );
                            const float score =
                                average_error +
                                max_error * 0.25f;

                            const int convention =
                                ((frame_mode & 1) ? 1 : 0) |
                                ((frame_mode & 2) ? 2 : 0) |
                                (remap ? 4 : 0) |
                                (inv_transpose ? 8 : 0) |
                                (inv_first ? 16 : 0);

                            if (score < best_score) {
                                best_score = score;
                                best_max_error = max_error;
                                best_convention = convention;
                            }
                        }
                    }
                }
            }

            g_cash_skin_convention = best_convention;

            LOGI(
                "SKIN CONVENTION: mode=%d score=%.6f max=%.6f "
                "frameT=%d localParent=%d remap=%d invT=%d invFirst=%d",
                g_cash_skin_convention,
                best_score,
                best_max_error,
                (g_cash_skin_convention & 1) ? 1 : 0,
                (g_cash_skin_convention & 2) ? 1 : 0,
                (g_cash_skin_convention & 4) ? 1 : 0,
                (g_cash_skin_convention & 8) ? 1 : 0,
                (g_cash_skin_convention & 16) ? 1 : 0
            );
        }

        /*
         * --------------------------------------------------------
         * Procesar huesos
         * --------------------------------------------------------
         */
        Mat4 local_bones[96];
        for (int i = 0; i < 96; ++i) {
            local_bones[i] = mat4_identity();
        }

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
            const bool skin_frame_transpose =
                (g_cash_skin_convention & 1) != 0;

            Mat4 bind_mat = mat4_identity();
            if (!skin_frame_transpose) {
                bind_mat.m[0] = bone.rot_mat[0];
                bind_mat.m[1] = bone.rot_mat[1];
                bind_mat.m[2] = bone.rot_mat[2];
                bind_mat.m[4] = bone.rot_mat[3];
                bind_mat.m[5] = bone.rot_mat[4];
                bind_mat.m[6] = bone.rot_mat[5];
                bind_mat.m[8] = bone.rot_mat[6];
                bind_mat.m[9] = bone.rot_mat[7];
                bind_mat.m[10] = bone.rot_mat[8];
            } else {
                bind_mat.m[0] = bone.rot_mat[0];
                bind_mat.m[1] = bone.rot_mat[3];
                bind_mat.m[2] = bone.rot_mat[6];
                bind_mat.m[4] = bone.rot_mat[1];
                bind_mat.m[5] = bone.rot_mat[4];
                bind_mat.m[6] = bone.rot_mat[7];
                bind_mat.m[8] = bone.rot_mat[2];
                bind_mat.m[9] = bone.rot_mat[5];
                bind_mat.m[10] = bone.rot_mat[8];
            }
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
             * Guardar matriz local.
             *
             * La jerarquía se resuelve después de procesar TODOS los
             * huesos. Así no importa si el padre aparece después del hijo
             * en el FrameList.
             * --------------------------------------------------------
             */
            local_bones[bone_idx] = local_mat;
        }

        // ------------------------------------------------------------
        // Resolver la jerarquía completa recursivamente.
        // ------------------------------------------------------------
        int global_state[96] = {};

        auto build_global_bone =
            [&](auto&& self, size_t idx) -> void {
                if (idx >= g_cash_model.bones.size() ||
                    idx >= 96) {
                    return;
                }

                if (global_state[idx] == 2) {
                    return;
                }

                if (global_state[idx] == 1) {
                    // Ciclo defensivo: usar la transformación local.
                    global_bones[idx] = local_bones[idx];
                    global_state[idx] = 2;
                    return;
                }

                global_state[idx] = 1;

                const uint32_t parent =
                    g_cash_model.bones[idx].parent;

                if (parent != 0xFFFFFFFF &&
                    parent < g_cash_model.bones.size() &&
                    parent < 96 &&
                    parent != idx) {

                    self(
                        self,
                        static_cast<size_t>(parent)
                    );

                    if (g_cash_skin_convention & 2) {
                        global_bones[idx] =
                            mat4_mul(
                                local_bones[idx],
                                global_bones[parent]
                            );
                    } else {
                        global_bones[idx] =
                            mat4_mul(
                                global_bones[parent],
                                local_bones[idx]
                            );
                    }
                } else {
                    global_bones[idx] =
                        local_bones[idx];
                }

                global_state[idx] = 2;
            };

        for (size_t bone_idx = 0;
             bone_idx < g_cash_model.bones.size() &&
             bone_idx < 96;
             ++bone_idx) {
            build_global_bone(
                build_global_bone,
                bone_idx
            );
        }

        // ------------------------------------------------------------
        // Construir la palette Skin con la convención seleccionada.
        // ------------------------------------------------------------
        const size_t skin_bone_count =
            g_cash_model.inverse_bind_matrices.size() / 16;

        const bool skin_use_remap =
            (g_cash_skin_convention & 4) != 0;
        const bool skin_inv_transpose =
            (g_cash_skin_convention & 8) != 0;
        const bool skin_inv_first =
            (g_cash_skin_convention & 16) != 0;

        auto transpose_skin_matrix =
            [](const Mat4& src) -> Mat4 {
                Mat4 dst = {0};
                for (int row = 0; row < 4; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        dst.m[col * 4 + row] =
                            src.m[row * 4 + col];
                    }
                }
                return dst;
            };

        for (size_t skin_bone = 0;
             skin_bone < skin_bone_count &&
             skin_bone < 96;
             ++skin_bone) {
            size_t frame_index = skin_bone;

            if (skin_use_remap &&
                skin_bone <
                g_cash_model.skin_bone_to_frame.size()) {
                frame_index =
                    g_cash_model.skin_bone_to_frame[
                        skin_bone
                    ];
            }

            if (frame_index >= g_cash_model.bones.size() ||
                frame_index >= 96) {
                continue;
            }

            Mat4 inverse_bind;
            for (int j = 0; j < 16; ++j) {
                inverse_bind.m[j] =
                    g_cash_model.inverse_bind_matrices[
                        skin_bone * 16 + j
                    ];
            }

            if (skin_inv_transpose) {
                inverse_bind =
                    transpose_skin_matrix(inverse_bind);
            }

            if (skin_inv_first) {
                skin_matrices[skin_bone] =
                    mat4_mul(
                        inverse_bind,
                        global_bones[frame_index]
                    );
            } else {
                skin_matrices[skin_bone] =
                    mat4_mul(
                        global_bones[frame_index],
                        inverse_bind
                    );
            }
        }

        // Imprime el error de la convención ya elegida.
        if (skin_bone_count > 0 &&
            g_cash_skin_convention >= 0) {
            float max_bind_error = 0.0f;
            float sum_bind_error = 0.0f;
            size_t samples = 0;

            for (size_t skin_bone = 0;
                 skin_bone < skin_bone_count &&
                 skin_bone < 96;
                 ++skin_bone) {
                float matrix_error = 0.0f;
                for (int j = 0; j < 16; ++j) {
                    const float target =
                        (j == 0 || j == 5 ||
                         j == 10 || j == 15)
                        ? 1.0f : 0.0f;

                    matrix_error += fabsf(
                        skin_matrices[skin_bone].m[j] -
                        target
                    );
                }

                sum_bind_error += matrix_error;
                max_bind_error =
                    std::max(
                        max_bind_error,
                        matrix_error
                    );
                ++samples;
            }

            static bool logged_bind_check = false;
            if (!logged_bind_check) {
                LOGI(
                    "SKIN SELECTED CHECK: avg=%.6f max=%.6f "
                    "samples=%zu mode=%d",
                    samples
                        ? sum_bind_error /
                          static_cast<float>(samples)
                        : 0.0f,
                    max_bind_error,
                    samples,
                    g_cash_skin_convention
                );
                logged_bind_check = true;
            }
        }

        Vec3 center_pos = vec3_add(
            g_player_pos,
            {
                g_cash_pos_adjust.x,
                g_cash_y_offset + g_cash_pos_adjust.y,
                g_cash_pos_adjust.z
            }
        );
        Mat4 cash_model_m = mat4_from_pos_cash(
            center_pos,
            g_player_yaw + CASH_MODEL_YAW_OFFSET
        );

        // Temporary visual-calibration rotation, applied after the
        // established baseline transform so the menu can tune local axes.
        const float rx = g_cash_rot_adjust_deg.x * 0.017453292519943f;
        const float ry = g_cash_rot_adjust_deg.y * 0.017453292519943f;
        const float rz = g_cash_rot_adjust_deg.z * 0.017453292519943f;
        const Mat4 extra_rot = mat4_mul(
            mat4_mul(mat4_rotate_z(rz), mat4_rotate_y(ry)),
            mat4_rotate_x(rx)
        );

        cash_model_m = mat4_mul(cash_model_m, extra_rot);
        Mat4 cash_mvp = mat4_mul(
            vp,
            cash_model_m
        );
        glUniformMatrix4fv(
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
        // Skinning real del skeleton HAnim + Skin del DFF.
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