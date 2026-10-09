#pragma once

// Shared math primitives for the Android RenderWare port.
struct Vec3 { float x, y, z; };
struct Mat4 { float m[16]; };

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 a, float s);
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
Vec3 vec3_norm(Vec3 a);

Mat4 mat4_identity();
Mat4 mat4_mul(const Mat4& a, const Mat4& b);
Mat4 mat4_perspective(float fovY, float aspect, float nearZ, float farZ);
[[maybe_unused]] Mat4 mat4_fps_view(Vec3 pos, float yaw, float pitch);
void quat_slerp(const float* a, const float* b, float t, float* out);

Mat4 mat4_from_pos_quat(const float* pos, const float* rot);
Mat4 mat4_from_pos_yaw(Vec3 pos, float yaw);
Mat4 mat4_from_pos_cash(Vec3 pos, float yaw);
Vec3 mat4_transform_point(const Mat4& m, Vec3 p);
Mat4 mat4_inverse_rigid(const Mat4& m);
Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up);
Mat4 mat4_rotate_x(float angle);
Mat4 mat4_rotate_y(float angle);
Mat4 mat4_rotate_z(float angle);
