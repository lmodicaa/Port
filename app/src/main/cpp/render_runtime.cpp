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

uniform int u_lighting_debug_mode;
uniform int u_is_cash;
uniform float u_prelit_multiplier;
uniform float u_prelit_gamma;
uniform float u_cash_ambient;
uniform float u_cash_directional;

uniform vec4 u_mat_color;
uniform vec4 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform int u_fog_enabled;

out vec4 frag_color;

void main() {
    vec4 tex_color = vec4(1.0);
    if (u_has_tex == 1) {
        tex_color = texture(u_tex, v_uv);
    }

    // Diagnóstico de color; no es una reconstrucción de la iluminación original:
    // 0 = prelit, 1 = prelit * material, 2 = textura sola,
    // 3 = prelit boosted (multiplicador y gamma ajustables),
    // 4 = luz experimental SOLO para Cash; el mundo conserva textura,
    // 5 = textura * prelit (RenderWare por defecto), con multiplicador ajustable.
    vec4 output_color;
    if (u_lighting_debug_mode == 0) {
        output_color = v_color;
    } else if (u_lighting_debug_mode == 1) {
        output_color = v_color * u_mat_color;
    } else if (u_lighting_debug_mode == 2) {
        output_color = tex_color;
    } else if (u_lighting_debug_mode == 3) {
        vec3 boosted = clamp(v_color.rgb * u_prelit_multiplier, 0.0, 1.0);
        float safe_gamma = max(u_prelit_gamma, 0.05);
        output_color = vec4(pow(boosted, vec3(1.0 / safe_gamma)), v_color.a);
    } else if (u_lighting_debug_mode == 5) {
        vec3 boosted_prelit = clamp(v_color.rgb * u_prelit_multiplier, 0.0, 1.0);
        output_color = vec4(tex_color.rgb * boosted_prelit, tex_color.a * v_color.a);
    } else if (u_lighting_debug_mode == 4 && u_is_cash == 1) {
        vec3 n = normalize(v_normal);
        vec3 light_dir = normalize(vec3(-0.35, 0.80, 0.48));
        float ndotl = max(dot(n, light_dir), 0.0);
        vec3 light = vec3(max(u_cash_ambient, 0.0)) +
                     vec3(max(u_cash_directional, 0.0) * ndotl);
        output_color = vec4(tex_color.rgb * u_mat_color.rgb * light,
                            tex_color.a * u_mat_color.a);
    } else {
        output_color = tex_color;
    }

    if (u_fog_enabled == 1) {
        float fog_range = max(u_fog_end - u_fog_start, 0.001);
        float fog_factor = clamp((v_dist - u_fog_start) / fog_range, 0.0, 1.0);
        output_color.rgb = mix(output_color.rgb, u_fog_color.rgb, fog_factor);
    }
    if (output_color.a < 0.1) discard;
    frag_color = output_color;
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
