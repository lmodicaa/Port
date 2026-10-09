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
uniform int u_lighting_debug_mode;

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

    // Debug modes: 0=sin luz, 1=World combinado, 2=ambient solo,
    // 3=dirAmbient solo. El modo 0 conserva exactamente el resultado previo.
    vec3 lit_rgb = base.rgb;
    if (u_lighting_debug_mode == 1) {
        float ndotl = max(dot(normalize(v_normal), normalize(-u_light_dir)), 0.0);
        vec3 world_light = u_world_ambient.rgb + u_dir_ambient.rgb * ndotl;
        lit_rgb = base.rgb * world_light;
    } else if (u_lighting_debug_mode == 2) {
        lit_rgb = base.rgb * u_world_ambient.rgb;
    } else if (u_lighting_debug_mode == 3) {
        float ndotl = max(dot(normalize(v_normal), normalize(-u_light_dir)), 0.0);
        lit_rgb = base.rgb * u_dir_ambient.rgb * ndotl;
    }

    frag_color = vec4(lit_rgb, base.a);
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
