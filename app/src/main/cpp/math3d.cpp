#include "math3d.h"

#include <algorithm>
#include <cmath>

Vec3 vec3_add(Vec3 a, Vec3 b)   { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 vec3_scale(Vec3 a, float s){ return {a.x*s, a.y*s, a.z*s}; }
float vec3_dot(Vec3 a, Vec3 b)  { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
Vec3 vec3_norm(Vec3 a) {
    float l = sqrtf(vec3_dot(a, a));
    if (l < 1e-6f) return {0,1,0};
    return {a.x/l, a.y/l, a.z/l};
}

Mat4 mat4_identity() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 r = {0};
    for(int c=0;c<4;++c)
        for(int row=0;row<4;++row)
            for(int k=0;k<4;++k)
                r.m[c*4+row] += a.m[k*4+row] * b.m[c*4+k];
    return r;
}

Mat4 mat4_perspective(float fovY, float aspect, float nearZ, float farZ) {
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
[[maybe_unused]] Mat4 mat4_fps_view(Vec3 pos, float yaw, float pitch) {
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

void quat_slerp(
    const float* a,
    const float* b,
    float t,
    float* out)
{
    float bx = b[0];
    float by = b[1];
    float bz = b[2];
    float bw = b[3];

    float dot =
        a[0] * bx +
        a[1] * by +
        a[2] * bz +
        a[3] * bw;

    if (dot < 0.0f) {
        dot = -dot;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }

    dot = std::max(-1.0f, std::min(1.0f, dot));

    if (dot > 0.9995f) {
        out[0] = a[0] + t * (bx - a[0]);
        out[1] = a[1] + t * (by - a[1]);
        out[2] = a[2] + t * (bz - a[2]);
        out[3] = a[3] + t * (bw - a[3]);

        const float len = sqrtf(
            out[0] * out[0] +
            out[1] * out[1] +
            out[2] * out[2] +
            out[3] * out[3]
        );

        if (len > 1e-6f) {
            out[0] /= len;
            out[1] /= len;
            out[2] /= len;
            out[3] /= len;
        }
        return;
    }

    const float theta = acosf(dot);
    const float sinTheta = sinf(theta);

    if (fabsf(sinTheta) < 1e-6f) {
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        out[3] = a[3];
        return;
    }

    const float w0 = sinf((1.0f - t) * theta) / sinTheta;
    const float w1 = sinf(t * theta) / sinTheta;

    out[0] = w0 * a[0] + w1 * bx;
    out[1] = w0 * a[1] + w1 * by;
    out[2] = w0 * a[2] + w1 * bz;
    out[3] = w0 * a[3] + w1 * bw;
}

Mat4 mat4_from_pos_quat(const float* pos, const float* rot) {
    // RtQuatUnitConvertToMatrix / RenderWare HAnim:
    // quaternion -> RwMatrix con la misma convención que nuestro
    // Mat4 column-major de OpenGL.
    float x = rot[0];
    float y = rot[1];
    float z = rot[2];
    float w = rot[3];

    const float xx = 2.0f * x * x;
    const float xy = 2.0f * x * y;
    const float xz = 2.0f * x * z;
    const float xw = 2.0f * x * w;
    const float yy = 2.0f * y * y;
    const float yz = 2.0f * y * z;
    const float yw = 2.0f * y * w;
    const float zz = 2.0f * z * z;
    const float zw = 2.0f * z * w;

    Mat4 r = {0};

    r.m[0]  = 1.0f - (yy + zz);
    r.m[1]  = xy - zw;
    r.m[2]  = xz + yw;
    r.m[3]  = 0.0f;

    r.m[4]  = xy + zw;
    r.m[5]  = 1.0f - (xx + zz);
    r.m[6]  = yz - xw;
    r.m[7]  = 0.0f;

    r.m[8]  = xz - yw;
    r.m[9]  = yz + xw;
    r.m[10] = 1.0f - (xx + yy);
    r.m[11] = 0.0f;

    r.m[12] = pos[0];
    r.m[13] = pos[1];
    r.m[14] = pos[2];
    r.m[15] = 1.0f;

    return r;
}

Mat4 mat4_from_pos_yaw(Vec3 pos, float yaw) {
    Mat4 r = {0};
    float c = cosf(yaw), s = sinf(yaw);
    r.m[0] = c;  r.m[1] = 0; r.m[2] = -s; r.m[3] = 0;
    r.m[4] = 0;  r.m[5] = 1; r.m[6] = 0;  r.m[7] = 0;
    r.m[8] = s;  r.m[9] = 0; r.m[10]= c;  r.m[11]= 0;
    r.m[12]= pos.x; r.m[13]= pos.y; r.m[14]= pos.z; r.m[15]= 1.0f;
    return r;
}

Mat4 mat4_from_pos_cash(Vec3 pos, float yaw) {
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

Vec3 mat4_transform_point(const Mat4& m, Vec3 p) {
    return {
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    };
}

Mat4 mat4_inverse_rigid(const Mat4& m) {
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

Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
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

Mat4 mat4_rotate_x(float angle) {
    const float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

Mat4 mat4_rotate_y(float angle) {
    const float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

Mat4 mat4_rotate_z(float angle) {
    const float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}
