#include "render_runtime.h"

#include <android/log.h>

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

    // RenderWare: textura * color de vértice * color del material.
    vec4 base = tex_color * v_color * u_mat_color;

    if (base.a < 0.1) discard;

    // La iluminación RGB del RW_WORLD todavía no está suficientemente
    // verificada contra el pipeline exacto de Manhunt PC. Aplicarla aquí
    // puede teñir toda la escena (por ejemplo, de azul) aunque las
    // texturas y colores originales sean correctos.
    //
    // Conservamos el color de textura + vertex color + material y evitamos
    // introducir una dominante cromática inventada mientras reconstruimos
    // el pipeline de iluminación original.
    //
    // No usamos una niebla negra fija: 10..45 era un fallback inventado
    // y cambiaba la imagen respecto del PC.
    frag_color = base;
})";



GLuint render_compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "Manhunt", "Shader error: %s", log);
        glDeleteShader(s); return 0;
    }
    return s;
}



const char* render_vertex_shader() {
    return VERT_SRC;
}

const char* render_fragment_shader() {
    return FRAG_SRC;
}
