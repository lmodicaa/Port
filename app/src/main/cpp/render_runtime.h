#pragma once

#include <GLES3/gl3.h>

const char* render_vertex_shader();
const char* render_fragment_shader();
GLuint render_compile_shader(GLenum type, const char* source);
