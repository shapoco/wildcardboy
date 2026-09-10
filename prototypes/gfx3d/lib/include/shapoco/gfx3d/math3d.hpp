#pragma once

#include <cmath>
#include <cstdint>

namespace shapoco::gfx3d {

// ---------------------------------------------------------------------------
// vec2f

struct vec2f {
    float x, y;
};

static inline vec2f operator+(const vec2f &a, const vec2f &b) { return {a.x + b.x, a.y + b.y}; }
static inline vec2f operator-(const vec2f &a, const vec2f &b) { return {a.x - b.x, a.y - b.y}; }
static inline vec2f operator*(const vec2f &a, float s) { return {a.x * s, a.y * s}; }

static inline vec2f lerp(const vec2f &a, const vec2f &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

// ---------------------------------------------------------------------------
// vec3f

struct vec3f {
    float x, y, z;
};

static inline vec3f operator+(const vec3f &a, const vec3f &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline vec3f operator-(const vec3f &a, const vec3f &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline vec3f operator-(const vec3f &a) { return {-a.x, -a.y, -a.z}; }
static inline vec3f operator*(const vec3f &a, float s) { return {a.x * s, a.y * s, a.z * s}; }

static inline float dot(const vec3f &a, const vec3f &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static inline vec3f cross(const vec3f &a, const vec3f &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static inline float length(const vec3f &a) { return std::sqrt(dot(a, a)); }

static inline vec3f normalize(const vec3f &a) {
    float len = length(a);
    if (len <= 0.0f) return {0, 0, 0};
    return a * (1.0f / len);
}

static inline vec3f lerp(const vec3f &a, const vec3f &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// ---------------------------------------------------------------------------
// colorf

struct colorf {
    float r, g, b, a;
};

static inline colorf operator+(const colorf &x, const colorf &y) {
    return {x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a};
}
static inline colorf operator*(const colorf &x, const colorf &y) {
    return {x.r * y.r, x.g * y.g, x.b * y.b, x.a * y.a};
}
static inline colorf operator*(const colorf &x, float s) {
    return {x.r * s, x.g * s, x.b * s, x.a * s};
}

static inline colorf lerp(const colorf &x, const colorf &y, float t) {
    return {
        x.r + (y.r - x.r) * t,
        x.g + (y.g - x.g) * t,
        x.b + (y.b - x.b) * t,
        x.a + (y.a - x.a) * t,
    };
}

// ---------------------------------------------------------------------------
// mat4f (column-major, OpenGL 互換)

struct mat4f {
    float m[16]; // m[col * 4 + row]

    static mat4f identity() {
        mat4f r = {};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static mat4f translation(float x, float y, float z) {
        mat4f r = identity();
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    // angle: ラジアン、axis: 正規化不要 (内部で正規化する)
    static mat4f rotation(float angle, const vec3f &axis) {
        vec3f n = normalize(axis);
        float c = std::cos(angle), s = std::sin(angle), ic = 1.0f - c;
        mat4f r = identity();
        r.m[0] = c + n.x * n.x * ic;
        r.m[1] = n.y * n.x * ic + n.z * s;
        r.m[2] = n.z * n.x * ic - n.y * s;
        r.m[4] = n.x * n.y * ic - n.z * s;
        r.m[5] = c + n.y * n.y * ic;
        r.m[6] = n.z * n.y * ic + n.x * s;
        r.m[8] = n.x * n.z * ic + n.y * s;
        r.m[9] = n.y * n.z * ic - n.x * s;
        r.m[10] = c + n.z * n.z * ic;
        return r;
    }

    static mat4f scaling(float x, float y, float z) {
        mat4f r = identity();
        r.m[0] = x;
        r.m[5] = y;
        r.m[10] = z;
        return r;
    }

    // fovY: ラジアン
    static mat4f perspective(float fovY, float aspect, float zNear, float zFar) {
        float f = 1.0f / std::tan(fovY * 0.5f);
        mat4f r = {};
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
        return r;
    }

    static mat4f orthographic(float left, float right, float bottom, float top, float zNear, float zFar) {
        mat4f r = identity();
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (zFar - zNear);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(zFar + zNear) / (zFar - zNear);
        return r;
    }

    mat4f operator*(const mat4f &o) const {
        mat4f r;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                float sum = 0.0f;
                for (int k = 0; k < 4; k++) {
                    sum += m[k * 4 + row] * o.m[col * 4 + k];
                }
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    // 点の変換 (w = 1 とみなし、除算はしない)
    vec3f transformPoint(const vec3f &v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14],
        };
    }

    // 点の変換 (w を返す)
    vec3f transformPoint4(const vec3f &v, float &wOut) const {
        wOut = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
        return transformPoint(v);
    }

    // 方向ベクトルの変換 (平行移動成分を無視する)
    vec3f transformDir(const vec3f &v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8] * v.z,
            m[1] * v.x + m[5] * v.y + m[9] * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z,
        };
    }
};

} // namespace shapoco::gfx3d
