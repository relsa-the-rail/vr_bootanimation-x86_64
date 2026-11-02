// Minimal column-major math, bit-for-bit the same formulas the device binary uses
// (verified numerically against the AArch64 code, see tools/verify/).
#pragma once
#include <cmath>
#include <cstring>

namespace bootanim {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

struct Mat4 {
    float m[16];  // column-major: m[col*4+row]
    static Mat4 identity() {
        Mat4 r; std::memset(r.m, 0, sizeof r.m);
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f; return r;
    }
    static Mat4 zero() { Mat4 r; std::memset(r.m, 0, sizeof r.m); return r; }
    float& at(int col, int row) { return m[col * 4 + row]; }
    float at(int col, int row) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.at(k, row) * b.at(c, k);
            r.at(c, row) = s;
        }
    return r;
}

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 a) { float l = std::sqrt(dot(a, a)); return a * (1.f / l); }

// rotate a vector by a unit quaternion
inline Vec3 rotate(const Quat& q, Vec3 v) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Vec3 r;
    r.x = (1 - 2 * (y * y + z * z)) * v.x + 2 * (x * y - z * w) * v.y + 2 * (x * z + y * w) * v.z;
    r.y = 2 * (x * y + z * w) * v.x + (1 - 2 * (x * x + z * z)) * v.y + 2 * (y * z - x * w) * v.z;
    r.z = 2 * (x * z - y * w) * v.x + 2 * (y * z + x * w) * v.y + (1 - 2 * (x * x + y * y)) * v.z;
    return r;
}

inline Mat4 mat4_cast(const Quat& q) {
    Mat4 r = Mat4::identity();
    float x = q.x, y = q.y, z = q.z, w = q.w;
    r.at(0, 0) = 1 - 2 * (y * y + z * z); r.at(0, 1) = 2 * (x * y + z * w); r.at(0, 2) = 2 * (x * z - y * w);
    r.at(1, 0) = 2 * (x * y - z * w); r.at(1, 1) = 1 - 2 * (x * x + z * z); r.at(1, 2) = 2 * (y * z + x * w);
    r.at(2, 0) = 2 * (x * z + y * w); r.at(2, 1) = 2 * (y * z - x * w); r.at(2, 2) = 1 - 2 * (x * x + y * y);
    return r;
}

// NodeTransformation::toMat4  ==  T * R * S
inline Mat4 trs(Vec3 t, const Quat& q, Vec3 s) {
    Mat4 T = Mat4::identity(); T.at(3, 0) = t.x; T.at(3, 1) = t.y; T.at(3, 2) = t.z;
    Mat4 S = Mat4::identity(); S.at(0, 0) = s.x; S.at(1, 1) = s.y; S.at(2, 2) = s.z;
    return T * mat4_cast(q) * S;
}

// glm::rotate(m, angle, axis)  ==  m * R(angle, axis)
inline Mat4 rotate(const Mat4& m, float angle, Vec3 axis) {
    float c = std::cos(angle), s = std::sin(angle);
    axis = normalize(axis);
    Vec3 t = axis * (1.f - c);
    Mat4 R = Mat4::identity();
    R.at(0, 0) = c + t.x * axis.x;           R.at(0, 1) = t.x * axis.y + s * axis.z;  R.at(0, 2) = t.x * axis.z - s * axis.y;
    R.at(1, 0) = t.y * axis.x - s * axis.z;  R.at(1, 1) = c + t.y * axis.y;           R.at(1, 2) = t.y * axis.z + s * axis.x;
    R.at(2, 0) = t.z * axis.x + s * axis.y;  R.at(2, 1) = t.z * axis.y - s * axis.x;  R.at(2, 2) = c + t.z * axis.z;
    return m * R;
}

// Right-handed look-at, identical to glm::lookAtRH
inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x; r.at(1, 0) = s.y; r.at(2, 0) = s.z;
    r.at(0, 1) = u.x; r.at(1, 1) = u.y; r.at(2, 1) = u.z;
    r.at(0, 2) = -f.x; r.at(1, 2) = -f.y; r.at(2, 2) = -f.z;
    r.at(3, 0) = -dot(s, eye); r.at(3, 1) = -dot(u, eye); r.at(3, 2) = dot(f, eye);
    return r;
}

// Same terms Renderer::bindCamera builds (OpenGL clip space, RH)
inline Mat4 perspective(double yfov, float aspect, double znear, double zfar) {
    double t = std::tan(yfov * 0.5);
    Mat4 r = Mat4::zero();
    r.at(0, 0) = (float)(1.0 / (t * (double)aspect));
    r.at(1, 1) = (float)(1.0 / t);
    r.at(2, 2) = (float)(-(znear + zfar) / (zfar - znear));
    r.at(2, 3) = -1.f;
    r.at(3, 2) = (float)((-2.0 * zfar * znear) / (zfar - znear));
    return r;
}

// readBufferViewLerp<glm::qua>: shortest-path slerp, lerp when nearly parallel
inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0.f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.99999988f) {
        float s = 1.f - t;
        return {a.x * s + b.x * t, a.y * s + b.y * t, a.z * s + b.z * t, a.w * s + b.w * t};
    }
    float ang = std::acos(d);
    float s0 = std::sin((1.f - t) * ang), s1 = std::sin(t * ang), inv = std::sin(ang);
    return {(a.x * s0 + b.x * s1) / inv, (a.y * s0 + b.y * s1) / inv,
            (a.z * s0 + b.z * s1) / inv, (a.w * s0 + b.w * s1) / inv};
}

}  // namespace bootanim
