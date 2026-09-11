#include "shapoco/gfx3d/gfx3d.hpp"

#include <algorithm>
#include <cmath>

// テクスチャの透視補正 ((u/w, v/w, 1/w) を補間してピクセル単位で除算する)。
// デフォルト無効 (アフィン補間)。有効にするには 1 を定義する。
#ifndef GFX3D_PERSPECTIVE_CORRECT_UV
#define GFX3D_PERSPECTIVE_CORRECT_UV 0
#endif

namespace shapoco::gfx3d {

// ---------------------------------------------------------------------------
// 内部データ構造
//
// 頂点処理 (変換・ライティング・投影) と線分の両端の算出は float で行い、
// ピクセル単位の処理はすべて整数 (16 ビット小数の固定小数点) で行う。

static constexpr int FIX_SHIFT = 16;
static constexpr float FIX_ONE = 65536.0f;

// ライティング・投影変換済みの頂点
struct ShadedVertex {
    float sx, sy;   // スクリーン座標
    float zNdc;     // NDC 深度 (スクリーン空間で線形、小さいほど手前)
    float u, v;     // テクセル単位のテクスチャ座標 (透視補正時は u/w, v/w)
    float r, g, b;  // 頂点色 0..255 (加算合成時は不透明度を乗算済み)
};

namespace TriFlags {
constexpr uint8_t FLAT = 1u << 0;    // 3 頂点の色が同一 (色の補間が不要)
constexpr uint8_t TEX = 1u << 1;     // テクスチャを参照する
constexpr uint8_t OPAQUE = 1u << 2;  // 不透明 (BlendMode::NONE)
} // namespace TriFlags

struct Triangle {
    ShadedVertex v[3];
#if GFX3D_PERSPECTIVE_CORRECT_UV
    float invW[3];      // 1/w
#endif
    float invDy[3];     // 辺 i (v[i] → v[i+1]) の 1/(sy の差)。水平な辺は 0
    const Material *mat;
    float depth;        // ビュー空間 z の平均 (小さいほど遠い)
    int16_t yMin, yMax; // 交差するラインの範囲 (両端含む)
    uint8_t flags;      // TriFlags
    uint8_t alpha64;    // 不透明度 (0..64)
    uint8_t rasterFn;   // ラスタライズ関数のインデックス (blend * 4 + tex * 2 + flat)
};

// スキャンライン上の線分。左端ピクセルでの属性値と 1 ピクセルあたりの増分を保持する
struct Span {
    int32_t x0, x1;   // ピクセル範囲 [x0, x1)
    float z0, dz;     // NDC 深度 (重なりの前後判定にのみ使用)
    int32_t r, g, b;  // 8.16 固定小数 (0..255)
    int32_t dr, dg, db;
#if GFX3D_PERSPECTIVE_CORRECT_UV
    float uw, vw, iw; // (u/w, v/w, 1/w)
    float duw, dvw, diw;
#else
    int32_t u, v;     // テクセル単位 16.16 固定小数
    int32_t du, dv;
#endif
    const Triangle *tri;
    Span *next;
};

struct StackEntry {
    mat4f matrix;
    const Material *material;
};

// putPrimitive() 内で変換済み頂点を使い回すためのダイレクトマップキャッシュ
struct CachedVertex {
    ShadedVertex sv;
    float invW;
    float viewZ;
    uint16_t tag;   // 頂点インデックス (NONE = 空)
    bool ok;        // false: ニア平面より手前 (三角形ごと破棄)
};

static constexpr int STACK_DEPTH = 16;
static constexpr int SPAN_CAPACITY_MIN = 32;
static constexpr int SPAN_CAPACITY_MAX = 512;
static constexpr int VCACHE_SIZE = 64; // 2 の冪
static constexpr uint16_t NONE = 0xFFFF;

struct State {
    int16_t screenW = 0, screenH = 0;

    Triangle *tris = nullptr;
    int triCapacity = 0;
    int triCount = 0;
    uint16_t *order = nullptr;      // 深度順 (遠い順) の三角形インデックス
    uint16_t *link = nullptr;       // order の位置ごとのリンク (ラインバケット / アクティブリスト)
    uint16_t *bucketHead = nullptr; // ラインごとの三角形リスト (render() 呼び出し内で使用)
    uint16_t *bucketTail = nullptr;

    Span *spanPool = nullptr;
    int spanCapacity = 0;
    int spanCount = 0;
    Span *opaqueHead = nullptr;                    // 不透明: x 昇順、互いに重ならない
    Span *transHead = nullptr, *transTail = nullptr; // 半透明: 登録順 (遠い順)

    StackEntry stack[STACK_DEPTH];
    int stackTop = 0;

    mat4f cur = mat4f::identity();
    const Material *curMat = nullptr;

    mat4f proj = mat4f::identity();
    float zNear = 0.1f;

    bool lightEnabled = false;
    vec3f lightDir = {0, 0, -1}; // 変換済み (ビュー空間)、正規化済み
    colorf lightCol = {1, 1, 1, 1};

    bool envEnabled = false;
    colorf envCol = {0, 0, 0, 1};

    colorf clearColor = {0, 0, 0, 1};

    CachedVertex vcache[VCACHE_SIZE];
};

static State s;

// ---------------------------------------------------------------------------
// ユーティリティ

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline uint16_t packRGB565(float r, float g, float b) {
    int ri = (int)(clamp01(r) * 31.0f + 0.5f);
    int gi = (int)(clamp01(g) * 63.0f + 0.5f);
    int bi = (int)(clamp01(b) * 31.0f + 0.5f);
    return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

static inline int log2Floor(int v) {
    int n = 0;
    while (v > 1) {
        v >>= 1;
        n++;
    }
    return n;
}

// n ピクセルを color で塗る (可能な限り 32 ビット単位で書く)
static void fill16(uint16_t *dst, int n, uint16_t color) {
    if (n <= 0) return;
    if ((uintptr_t)dst & 2u) {
        *dst++ = color;
        n--;
    }
    uint32_t *d32 = (uint32_t *)dst;
    uint32_t c32 = ((uint32_t)color << 16) | color;
    for (int i = 0; i < (n >> 1); i++) d32[i] = c32;
    if (n & 1) dst[n - 1] = color;
}

// ---------------------------------------------------------------------------
// 初期化

void init(int16_t w, int16_t h, void *arena, size_t arenaSize) {
    s = State();
    s.screenW = w;
    s.screenH = h;

    uintptr_t p = (uintptr_t)arena;
    uintptr_t end = p + arenaSize;
    p = (p + 7u) & ~(uintptr_t)7u;
    size_t avail = (end > p) ? (size_t)(end - p) : 0;

    // ラインバケット (screenH x 2)
    size_t bucketBytes = (size_t)h * 2 * sizeof(uint16_t);
    if (avail < bucketBytes) return;
    s.bucketHead = (uint16_t *)p;
    s.bucketTail = s.bucketHead + h;
    p += bucketBytes;
    p = (p + 7u) & ~(uintptr_t)7u;

    // 線分プール: 残りの 1/4
    avail = (end > p) ? (size_t)(end - p) : 0;
    int spanCap = (int)((avail / 4) / sizeof(Span));
    spanCap = std::max(SPAN_CAPACITY_MIN, std::min(SPAN_CAPACITY_MAX, spanCap));
    s.spanPool = (Span *)p;
    s.spanCapacity = spanCap;
    p += (size_t)spanCap * sizeof(Span);
    p = (p + 7u) & ~(uintptr_t)7u;

    // トライアングルバッファ + order/link (三角形 1 個あたり 4 バイト): 残り全部
    avail = (end > p) ? (size_t)(end - p) : 0;
    int triCap = (int)(avail / (sizeof(Triangle) + 2 * sizeof(uint16_t)));
    triCap = std::min(triCap, (int)NONE - 1);
    s.order = (uint16_t *)p;
    s.link = s.order + triCap;
    p += (size_t)triCap * 2 * sizeof(uint16_t);
    p = (p + 7u) & ~(uintptr_t)7u;
    s.tris = (Triangle *)p;
    s.triCapacity = (end > p) ? std::min(triCap, (int)((end - p) / sizeof(Triangle))) : 0;
}

void deinit() { s = State(); }

// ---------------------------------------------------------------------------
// シーン構築

void beginScene() {
    s.triCount = 0;
    s.stackTop = 0;
    s.cur = mat4f::identity();
}

void endScene() {}

void loadIdentity() { s.cur = mat4f::identity(); }

void translate(const vec3f &v) { s.cur = s.cur * mat4f::translation(v.x, v.y, v.z); }
void translate(float x, float y, float z) { s.cur = s.cur * mat4f::translation(x, y, z); }
void rotate(float angle, const vec3f &axis) { s.cur = s.cur * mat4f::rotation(angle, axis); }
void rotate(float angle, float x, float y, float z) { s.cur = s.cur * mat4f::rotation(angle, {x, y, z}); }
void scale(const vec3f &v) { s.cur = s.cur * mat4f::scaling(v.x, v.y, v.z); }
void scale(float x, float y, float z) { s.cur = s.cur * mat4f::scaling(x, y, z); }

void pushState() {
    if (s.stackTop >= STACK_DEPTH) return;
    s.stack[s.stackTop].matrix = s.cur;
    s.stack[s.stackTop].material = s.curMat;
    s.stackTop++;
}

void popState() {
    if (s.stackTop <= 0) return;
    s.stackTop--;
    s.cur = s.stack[s.stackTop].matrix;
    s.curMat = s.stack[s.stackTop].material;
}

void setMaterial(const Material &mat) { s.curMat = &mat; }

void enableParallelLight(const vec3f &dir, const colorf &col) {
    s.lightEnabled = true;
    s.lightDir = normalize(s.cur.transformDir(dir));
    s.lightCol = col;
}

void disableParallelLight() { s.lightEnabled = false; }

void enableEnvironmentLight(const colorf &col) {
    s.envEnabled = true;
    s.envCol = col;
}

void disableEnvironmentLight() { s.envEnabled = false; }

void setClearColor(const colorf &col) { s.clearColor = col; }

void setPerspectiveProjection(float fovY, float aspect, float zNear, float zFar) {
    s.proj = mat4f::perspective(fovY, aspect, zNear, zFar);
    s.zNear = zNear;
}

void setOrthographicProjection(float left, float right, float bottom, float top, float zNear, float zFar) {
    s.proj = mat4f::orthographic(left, right, bottom, top, zNear, zFar);
    s.zNear = zNear;
}

// ---------------------------------------------------------------------------
// 頂点処理 (変換 + ライティング + 投影)

static inline const Texture *materialTexture(const Material *mat) {
    return (mat->flags & (MaterialFlags::TEXTURE | MaterialFlags::ENV_MAP)) ? mat->texture : nullptr;
}

static void shadeVertex(const Vertex &in, const Material *mat, const Texture *tex, CachedVertex &out) {
    out.ok = false;
    vec3f viewPos = s.cur.transformPoint(in.position);
    // ニア平面をまたぐ・手前にある三角形は破棄する
    if (viewPos.z > -s.zNear) return;
    float w;
    vec3f clip = s.proj.transformPoint4(viewPos, w);
    if (w <= 0.0f) return;
    float invW = 1.0f / w;

    ShadedVertex &sv = out.sv;
    sv.sx = (clip.x * invW * 0.5f + 0.5f) * s.screenW;
    sv.sy = (0.5f - clip.y * invW * 0.5f) * s.screenH;
    sv.zNdc = clip.z * invW;

    vec3f n = normalize(s.cur.transformDir(in.normal));

    if (tex) {
        vec2f uv;
        if (mat->flags & MaterialFlags::ENV_MAP) {
            // ビュー空間法線から環境マップの UV を求める
            uv = {n.x * 0.5f + 0.5f, 0.5f - n.y * 0.5f};
        } else {
            uv = in.uv;
        }
        sv.u = uv.x * tex->width;
        sv.v = uv.y * tex->height;
    } else {
        sv.u = sv.v = 0.0f;
    }

    // グーローシェーディング: 頂点単位でライティング
    float r = 0, g = 0, b = 0;
    bool lit = false;
    if (s.envEnabled) {
        r += mat->ambient.r * s.envCol.r;
        g += mat->ambient.g * s.envCol.g;
        b += mat->ambient.b * s.envCol.b;
        lit = true;
    }
    if (s.lightEnabled) {
        float d = dot(n, -s.lightDir);
        if (d > 0.0f) {
            r += mat->diffuse.r * s.lightCol.r * d;
            g += mat->diffuse.g * s.lightCol.g * d;
            b += mat->diffuse.b * s.lightCol.b * d;
        }
        lit = true;
    }
    if (!lit) {
        r = mat->diffuse.r;
        g = mat->diffuse.g;
        b = mat->diffuse.b;
    }
    if (mat->blendMode == BlendMode::ADD) {
        // 加算合成は (色 x 不透明度) を足すだけなので、あらかじめ乗じておく
        float a = clamp01(mat->diffuse.a);
        r *= a;
        g *= a;
        b *= a;
    }
    sv.r = clamp01(r) * 255.0f;
    sv.g = clamp01(g) * 255.0f;
    sv.b = clamp01(b) * 255.0f;

    out.invW = invW;
    out.viewZ = viewPos.z;
    out.ok = true;
}

// ---------------------------------------------------------------------------
// 三角形の登録

static void emitTriangle(const CachedVertex &a, const CachedVertex &b, const CachedVertex &c,
                         const Material *mat, const Texture *tex) {
    if (!a.ok || !b.ok || !c.ok) return;
    if (s.triCount >= s.triCapacity) return; // バッファあふれ: このフレームでは破棄

    Triangle &t = s.tris[s.triCount];
    t.v[0] = a.sv;
    t.v[1] = b.sv;
    t.v[2] = c.sv;

    // バックフェイスカリング (スクリーン座標は y が下向きなので、表向き = 面積が負)
    float area2 = (t.v[1].sx - t.v[0].sx) * (t.v[2].sy - t.v[0].sy) -
                  (t.v[2].sx - t.v[0].sx) * (t.v[1].sy - t.v[0].sy);
    if (area2 == 0.0f) return;
    if (area2 > 0.0f && !(mat->flags & MaterialFlags::DOUBLE_SIDED)) return;

    float syMin = std::min({t.v[0].sy, t.v[1].sy, t.v[2].sy});
    float syMax = std::max({t.v[0].sy, t.v[1].sy, t.v[2].sy});
    int yMin = (int)std::ceil(syMin - 0.5f);
    int yMax = (int)std::floor(syMax - 0.5f);
    yMin = std::max(yMin, 0);
    yMax = std::min(yMax, (int)s.screenH - 1);
    if (yMin > yMax) return;

    uint8_t flags = 0;
    if (t.v[0].r == t.v[1].r && t.v[0].r == t.v[2].r && t.v[0].g == t.v[1].g && t.v[0].g == t.v[2].g &&
        t.v[0].b == t.v[1].b && t.v[0].b == t.v[2].b) {
        flags |= TriFlags::FLAT;
    }
    if (mat->blendMode == BlendMode::NONE) flags |= TriFlags::OPAQUE;

    if (tex) {
        flags |= TriFlags::TEX;
        // 固定小数点の桁あふれを防ぐため、テクスチャ座標を三角形単位でラップする
        // (最小値の整数周期分を 3 頂点から引く。相対値は変わらない)
        float uMin = std::min({t.v[0].u, t.v[1].u, t.v[2].u});
        float vMin = std::min({t.v[0].v, t.v[1].v, t.v[2].v});
        float uOff = std::floor(uMin / tex->width) * tex->width;
        float vOff = std::floor(vMin / tex->height) * tex->height;
        for (int i = 0; i < 3; i++) {
            t.v[i].u -= uOff;
            t.v[i].v -= vOff;
        }
    }
#if GFX3D_PERSPECTIVE_CORRECT_UV
    const CachedVertex *cv[3] = {&a, &b, &c};
    for (int i = 0; i < 3; i++) {
        t.invW[i] = cv[i]->invW;
        t.v[i].u *= cv[i]->invW; // 透視補正のため 1/w を乗じる
        t.v[i].v *= cv[i]->invW;
    }
#endif

    for (int e = 0; e < 3; e++) {
        float dy = t.v[e == 2 ? 0 : e + 1].sy - t.v[e].sy;
        t.invDy[e] = (dy != 0.0f) ? 1.0f / dy : 0.0f;
    }

    int alpha64 = (int)(clamp01(mat->diffuse.a) * 64.0f + 0.5f);
    t.mat = mat;
    t.depth = (a.viewZ + b.viewZ + c.viewZ) * (1.0f / 3.0f);
    t.yMin = (int16_t)yMin;
    t.yMax = (int16_t)yMax;
    t.flags = flags;
    t.alpha64 = (uint8_t)alpha64;
    t.rasterFn = (uint8_t)((int)mat->blendMode * 4 + ((flags & TriFlags::TEX) ? 2 : 0) +
                           ((flags & TriFlags::FLAT) ? 1 : 0));
    s.triCount++;
}

void putPrimitive(const Primitive &prim) {
    const Material *mat = prim.material ? prim.material : s.curMat;
    if (!mat) return;
    const Texture *tex = materialTexture(mat);
    const Vertex *verts = prim.vertexBuffer->vertices;
    const uint16_t *idx = prim.indices;
    int n = prim.indexCount;

    // 頂点キャッシュ: 同じ頂点を共有する三角形 (ストリップ・ファン・インデックス付き)
    // で変換を繰り返さない。プリミティブ単位で無効化する
    for (int i = 0; i < VCACHE_SIZE; i++) s.vcache[i].tag = NONE;
    auto fetch = [&](uint16_t vi) -> const CachedVertex & {
        CachedVertex &cv = s.vcache[vi & (VCACHE_SIZE - 1)];
        if (cv.tag != vi) {
            shadeVertex(verts[vi], mat, tex, cv);
            cv.tag = vi;
        }
        return cv;
    };

    switch (prim.type) {
    case PrimitiveType::TRIANGLES:
        for (int i = 0; i + 2 < n; i += 3) {
            emitTriangle(fetch(idx[i]), fetch(idx[i + 1]), fetch(idx[i + 2]), mat, tex);
        }
        break;
    case PrimitiveType::TRIANGLE_STRIP:
        for (int i = 2; i < n; i++) {
            if (i & 1) {
                emitTriangle(fetch(idx[i - 1]), fetch(idx[i - 2]), fetch(idx[i]), mat, tex);
            } else {
                emitTriangle(fetch(idx[i - 2]), fetch(idx[i - 1]), fetch(idx[i]), mat, tex);
            }
        }
        break;
    case PrimitiveType::TRIANGLE_FAN:
        for (int i = 2; i < n; i++) {
            emitTriangle(fetch(idx[0]), fetch(idx[i - 1]), fetch(idx[i]), mat, tex);
        }
        break;
    }
}

void putCube(const vec3f &center, const vec3f &size, int divs) {
    if (divs < 1) divs = 1;

    static const int8_t FACE_NORMALS[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };
    // 各面を外から見て反時計回りになる 4 頂点 (単位立方体の角の符号)
    static const int8_t FACE_CORNERS[6][4][3] = {
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},       // +X
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},   // -X
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},       // +Y
        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},   // -Y
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},       // +Z
        {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},   // -Z
    };
    static const vec2f FACE_UVS[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    static const uint16_t QUAD_INDICES[6] = {0, 1, 2, 0, 2, 3};

    vec3f half = size * 0.5f;

    for (int f = 0; f < 6; f++) {
        vec3f corner[4];
        for (int i = 0; i < 4; i++) {
            corner[i] = {
                center.x + half.x * FACE_CORNERS[f][i][0],
                center.y + half.y * FACE_CORNERS[f][i][1],
                center.z + half.z * FACE_CORNERS[f][i][2],
            };
        }
        vec3f normal = {(float)FACE_NORMALS[f][0], (float)FACE_NORMALS[f][1], (float)FACE_NORMALS[f][2]};
        // corner[0] を原点、corner[0]→corner[1] を u 方向、corner[0]→corner[3] を v 方向とする
        vec3f du = corner[1] - corner[0];
        vec3f dv = corner[3] - corner[0];
        vec2f duvU = FACE_UVS[1] - FACE_UVS[0];
        vec2f duvV = FACE_UVS[3] - FACE_UVS[0];

        // 面を divs x divs のポリゴンに分割する (中間点の UV は補間で生成)
        for (int j = 0; j < divs; j++) {
            for (int i = 0; i < divs; i++) {
                float u0 = (float)i / divs, u1 = (float)(i + 1) / divs;
                float v0 = (float)j / divs, v1 = (float)(j + 1) / divs;
                const float us[4] = {u0, u1, u1, u0};
                const float vs[4] = {v0, v0, v1, v1};

                Vertex quad[4];
                for (int k = 0; k < 4; k++) {
                    quad[k].position = corner[0] + du * us[k] + dv * vs[k];
                    quad[k].normal = normal;
                    quad[k].uv = FACE_UVS[0] + duvU * us[k] + duvV * vs[k];
                }
                VertexBuffer vb = {4, quad};
                Primitive prim = {PrimitiveType::TRIANGLES, &vb, 6, QUAD_INDICES, nullptr};
                putPrimitive(prim);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// レンダリング

void beginRender() {
    // 遠い順 (ビュー空間 z の昇順 = より負のものが先) にソートする。
    // 不透明同士の前後関係は線分挿入時の深度比較で解決されるため、
    // このソートは主に半透明の合成順を決める。三角形本体ではなくインデックスを並べ替える
    for (int i = 0; i < s.triCount; i++) s.order[i] = (uint16_t)i;
    const Triangle *tris = s.tris;
    std::sort(s.order, s.order + s.triCount,
              [tris](uint16_t a, uint16_t b) { return tris[a].depth < tris[b].depth; });
}

void endRender() {}

static inline Span *allocSpan() {
    if (s.spanCount >= s.spanCapacity) return nullptr;
    return &s.spanPool[s.spanCount++];
}

// 線分の左端を n ピクセル進める (属性を増分で更新する)
static inline void spanAdvance(Span &sp, int n) {
    sp.x0 += n;
    sp.z0 += sp.dz * n;
    sp.r += sp.dr * n;
    sp.g += sp.dg * n;
    sp.b += sp.db * n;
#if GFX3D_PERSPECTIVE_CORRECT_UV
    sp.uw += sp.duw * n;
    sp.vw += sp.dvw * n;
    sp.iw += sp.diw * n;
#else
    sp.u += sp.du * n;
    sp.v += sp.dv * n;
#endif
}

// 線分上の位置 x (ピクセル) における NDC 深度
static inline float spanDepthAt(const Span &sp, float x) { return sp.z0 + sp.dz * (x - (float)sp.x0); }

// 重なり区間 [ox0, ox1) の中央で frag が e より手前にあるか
static inline bool fragNearer(const Span &frag, const Span &e, int ox0, int ox1) {
    float xm = (float)(ox0 + ox1 - 1) * 0.5f;
    return spanDepthAt(frag, xm) < spanDepthAt(e, xm);
}

// リスト要素 e = *pp から区間 [ox0, ox1) を取り除く。
// 走査を続ける位置 (残った部分の次、または e の残り) を返す
static Span **cutSpan(Span **pp, int ox0, int ox1) {
    Span *e = *pp;
    bool leftRemains = e->x0 < ox0;
    bool rightRemains = e->x1 > ox1;
    if (leftRemains && rightRemains) {
        // 中抜き: 右側を新しい線分として分割する (プールがあふれた場合は右側を捨てる)
        Span *r = allocSpan();
        if (r) {
            *r = *e;
            spanAdvance(*r, ox1 - r->x0);
            r->next = e->next;
            e->next = r;
        }
        e->x1 = ox0;
        return &e->next;
    } else if (leftRemains) {
        e->x1 = ox0;
        return &e->next;
    } else if (rightRemains) {
        spanAdvance(*e, ox1 - e->x0);
        return pp;
    } else {
        *pp = e->next; // 完全に覆われた線分を削除
        return pp;
    }
}

// 半透明リストの末尾に線分 (の一部 [sp.x0, x1)) を追加する
static void appendTranslucent(const Span &sp, int x1) {
    Span *n = allocSpan();
    if (!n) return; // プールあふれ: この線分は破棄
    *n = sp;
    n->x1 = x1;
    n->next = nullptr;
    if (s.transTail) {
        s.transTail->next = n;
    } else {
        s.transHead = n;
    }
    s.transTail = n;
}

// 不透明な線分を x 昇順のリストへ挿入する。
// 既存の線分との重なりは、重なり区間の中央での深度比較で奥側を削除する。
// 三角形単位のソート順に頼らないため、大きなポリゴンと小さなポリゴンの前後関係も正しく解決される
static void insertOpaque(Span &frag) {
    Span **pp = &s.opaqueHead;
    while (*pp && (*pp)->x1 <= frag.x0) pp = &(*pp)->next;
    while (*pp && (*pp)->x0 < frag.x1) {
        Span *e = *pp;
        int ox0 = std::max(e->x0, frag.x0);
        int ox1 = std::min(e->x1, frag.x1);
        if (fragNearer(frag, *e, ox0, ox1)) {
            pp = cutSpan(pp, ox0, ox1);
        } else {
            // e より左にはみ出した部分は確定 (これより左の要素は処理済み)
            if (frag.x0 < ox0) {
                Span *n = allocSpan();
                if (n) {
                    *n = frag;
                    n->x1 = ox0;
                    n->next = e;
                    *pp = n;
                    pp = &n->next;
                }
            }
            if (frag.x1 > ox1) {
                spanAdvance(frag, ox1 - frag.x0);
                pp = &e->next;
            } else {
                return; // 残りは完全に隠されている
            }
        }
    }
    if (frag.x0 < frag.x1) {
        Span *n = allocSpan();
        if (n) {
            *n = frag;
            n->next = *pp;
            *pp = n;
        }
    }
}

// 半透明な線分を、手前にある不透明線分で隠される部分を除いて半透明リストに追加する
static void insertTranslucent(Span &frag) {
    Span **pp = &s.opaqueHead;
    while (*pp && (*pp)->x1 <= frag.x0) pp = &(*pp)->next;
    while (*pp && (*pp)->x0 < frag.x1) {
        Span *e = *pp;
        int ox0 = std::max(e->x0, frag.x0);
        int ox1 = std::min(e->x1, frag.x1);
        if (fragNearer(frag, *e, ox0, ox1)) {
            pp = &e->next; // 半透明が手前: 両方残す
            continue;
        }
        if (frag.x0 < ox0) appendTranslucent(frag, ox0);
        if (frag.x1 > ox1) {
            spanAdvance(frag, ox1 - frag.x0);
            pp = &e->next;
        } else {
            return;
        }
    }
    if (frag.x0 < frag.x1) appendTranslucent(frag, frag.x1);
}

// 新しい不透明線分 frag の奥にある半透明線分の重なり部分を削除する
static void clipTranslucent(const Span &frag) {
    Span **pp = &s.transHead;
    bool modified = false;
    while (*pp) {
        Span *e = *pp;
        if (e->x1 <= frag.x0 || e->x0 >= frag.x1) {
            pp = &e->next;
            continue;
        }
        int ox0 = std::max(e->x0, frag.x0);
        int ox1 = std::min(e->x1, frag.x1);
        if (fragNearer(frag, *e, ox0, ox1)) {
            pp = cutSpan(pp, ox0, ox1);
            modified = true;
        } else {
            pp = &e->next;
        }
    }
    if (modified) {
        s.transTail = nullptr;
        for (Span *e = s.transHead; e; e = e->next) s.transTail = e;
    }
}

// スキャンライン yc と三角形の交差から線分を生成する。交差しない、または
// 領域 [rx0, rx1) にかからない場合は false
static bool makeSpan(const Triangle &t, float yc, int rx0, int rx1, Span &out) {
    struct EndPt {
        float x, z, u, v, r, g, b;
#if GFX3D_PERSPECTIVE_CORRECT_UV
        float iw;
#endif
    };
    EndPt pts[2];
    int n = 0;

    for (int e = 0; e < 3 && n < 2; e++) {
        const ShadedVertex &a = t.v[e];
        const ShadedVertex &b = t.v[e == 2 ? 0 : e + 1];
        // 半開区間で判定して頂点上の交差の二重カウントを防ぐ
        if ((a.sy <= yc && yc < b.sy) || (b.sy <= yc && yc < a.sy)) {
            float tt = (yc - a.sy) * t.invDy[e];
            EndPt &p = pts[n++];
            p.x = a.sx + (b.sx - a.sx) * tt;
            p.z = a.zNdc + (b.zNdc - a.zNdc) * tt;
            p.u = a.u + (b.u - a.u) * tt;
            p.v = a.v + (b.v - a.v) * tt;
            p.r = a.r + (b.r - a.r) * tt;
            p.g = a.g + (b.g - a.g) * tt;
            p.b = a.b + (b.b - a.b) * tt;
#if GFX3D_PERSPECTIVE_CORRECT_UV
            p.iw = t.invW[e] + (t.invW[e == 2 ? 0 : e + 1] - t.invW[e]) * tt;
#endif
        }
    }
    if (n < 2) return false;

    const EndPt *l = &pts[0], *r = &pts[1];
    if (l->x > r->x) std::swap(l, r);
    float width = r->x - l->x;
    if (width <= 0.0f) return false; // 幅ゼロ

    // ピクセル中心 (xi + 0.5) が [x0, x1) に入るピクセルを塗る
    int xi0 = (int)std::ceil(l->x - 0.5f);
    int xi1 = (int)std::ceil(r->x - 0.5f);
    xi0 = std::max(xi0, rx0);
    xi1 = std::min(xi1, rx1);
    if (xi0 >= xi1) return false;

    // 左端ピクセルの中心における属性値と 1 ピクセルあたりの増分
    float invW = 1.0f / width;
    float t0 = ((float)xi0 + 0.5f - l->x) * invW;
    auto lin = [&](float a0, float a1, float &start, float &delta) {
        float d = (a1 - a0) * invW;
        start = a0 + (a1 - a0) * t0;
        delta = d;
    };
    // 色: 両端を 0..255 に収めれば途中の値も収まる。丸め誤差で僅かに負になるのを防ぐため 0 で切る
    auto linColor = [&](float a0, float a1, int32_t &start, int32_t &delta) {
        float sf, df;
        lin(std::min(a0, 255.0f), std::min(a1, 255.0f), sf, df);
        start = (int32_t)(std::max(sf, 0.0f) * FIX_ONE);
        delta = (int32_t)(df * FIX_ONE);
    };

    out.x0 = xi0;
    out.x1 = xi1;
    lin(l->z, r->z, out.z0, out.dz);
    linColor(l->r, r->r, out.r, out.dr);
    linColor(l->g, r->g, out.g, out.dg);
    linColor(l->b, r->b, out.b, out.db);
#if GFX3D_PERSPECTIVE_CORRECT_UV
    lin(l->u, r->u, out.uw, out.duw);
    lin(l->v, r->v, out.vw, out.dvw);
    lin(l->iw, r->iw, out.iw, out.diw);
#else
    float sf, df;
    lin(l->u, r->u, sf, df);
    out.u = (int32_t)(sf * FIX_ONE);
    out.du = (int32_t)(df * FIX_ONE);
    lin(l->v, r->v, sf, df);
    out.v = (int32_t)(sf * FIX_ONE);
    out.dv = (int32_t)(df * FIX_ONE);
#endif
    out.tri = &t;
    out.next = nullptr;
    return true;
}

// ---------------------------------------------------------------------------
// ピクセル処理 (固定小数点)
//
// ブレンドモード x テクスチャ有無 x フラット (色が一定) の組み合わせごとに
// ループを特殊化し、ループ内に分岐と無駄な補間を残さない。

template <BlendMode B, bool TEX, bool FLAT>
static void rasterSpanT(uint16_t *px, int n, const Span &sp) {
    const Triangle &t = *sp.tri;
    int32_t r = sp.r, g = sp.g, b = sp.b;
    const int32_t dr = sp.dr, dg = sp.dg, db = sp.db;
    const uint32_t a = t.alpha64, ia = 64 - a;

    // 3 頂点の色が同じでテクスチャもなければ、色は線分全体で一定
    uint32_t sr = (uint32_t)(r >> 19) & 31u;
    uint32_t sg = (uint32_t)(g >> 18) & 63u;
    uint32_t sb = (uint32_t)(b >> 19) & 31u;
    if (B == BlendMode::NONE && FLAT && !TEX) {
        fill16(px, n, (uint16_t)((sr << 11) | (sg << 5) | sb));
        return;
    }

    // テクスチャ (幅・高さは 2 の冪であること)
    const uint16_t *tp = nullptr;
    uint32_t uMask = 0, vMask = 0;
    int wShift = 0;
    if (TEX) {
        const Texture &tex = *t.mat->texture;
        wShift = log2Floor(tex.width);
        uMask = (1u << wShift) - 1;
        vMask = (1u << log2Floor(tex.height)) - 1;
        tp = tex.pixels;
    }
#if GFX3D_PERSPECTIVE_CORRECT_UV
    float uw = sp.uw, vw = sp.vw, iw = sp.iw;
    const float duw = sp.duw, dvw = sp.dvw, diw = sp.diw;
#else
    int32_t u = sp.u, v = sp.v;
    const int32_t du = sp.du, dv = sp.dv;
#endif

    for (int i = 0; i < n; i++) {
        if (TEX) {
#if GFX3D_PERSPECTIVE_CORRECT_UV
            // 透視補正: (u/w, v/w) と 1/w を線形補間し、ピクセル単位で除算する
            float inv = 1.0f / iw;
            uint32_t ui = (uint32_t)(int32_t)(uw * inv);
            uint32_t vi = (uint32_t)(int32_t)(vw * inv);
#else
            uint32_t ui = (uint32_t)(u >> FIX_SHIFT);
            uint32_t vi = (uint32_t)(v >> FIX_SHIFT);
#endif
            uint32_t texel = tp[((vi & vMask) << wShift) | (ui & uMask)];
            // 頂点色 (0..255) でテクセル (5/6/5 ビット) を変調する。(c + 1) * t >> 8 で最大値を保つ
            uint32_t cr = ((uint32_t)(r >> FIX_SHIFT) & 0xFFu) + 1;
            uint32_t cg = ((uint32_t)(g >> FIX_SHIFT) & 0xFFu) + 1;
            uint32_t cb = ((uint32_t)(b >> FIX_SHIFT) & 0xFFu) + 1;
            sr = (cr * (texel >> 11)) >> 8;
            sg = (cg * ((texel >> 5) & 63u)) >> 8;
            sb = (cb * (texel & 31u)) >> 8;
        } else if (!FLAT) {
            sr = (uint32_t)(r >> 19) & 31u;
            sg = (uint32_t)(g >> 18) & 63u;
            sb = (uint32_t)(b >> 19) & 31u;
        }
        uint32_t src = (sr << 11) | (sg << 5) | sb;

        if (B == BlendMode::NONE) {
            px[i] = (uint16_t)src;
        } else if (B == BlendMode::ALPHA) {
            // RB と G を分けて一度に補間する (各フィールドの積は隣のフィールドに繰り上がらない)
            uint32_t d = px[i];
            uint32_t rb = (((d & 0xF81Fu) * ia + (src & 0xF81Fu) * a) >> 6) & 0xF81Fu;
            uint32_t gg = (((d & 0x07E0u) * ia + (src & 0x07E0u) * a) >> 6) & 0x07E0u;
            px[i] = (uint16_t)(rb | gg);
        } else {
            // 加算合成 (色は不透明度を乗算済み): チャネルごとに飽和加算
            uint32_t d = px[i];
            uint32_t ar = (d >> 11) + sr;
            uint32_t ag = ((d >> 5) & 63u) + sg;
            uint32_t ab = (d & 31u) + sb;
            if (ar > 31u) ar = 31u;
            if (ag > 63u) ag = 63u;
            if (ab > 31u) ab = 31u;
            px[i] = (uint16_t)((ar << 11) | (ag << 5) | ab);
        }

        if (!FLAT) {
            r += dr;
            g += dg;
            b += db;
        }
        if (TEX) {
#if GFX3D_PERSPECTIVE_CORRECT_UV
            uw += duw;
            vw += dvw;
            iw += diw;
#else
            u += du;
            v += dv;
#endif
        }
    }
}

using RasterFn = void (*)(uint16_t *, int, const Span &);

// Triangle::rasterFn = blend * 4 + tex * 2 + flat
static const RasterFn RASTER_FNS[12] = {
    rasterSpanT<BlendMode::NONE, false, false>,  rasterSpanT<BlendMode::NONE, false, true>,
    rasterSpanT<BlendMode::NONE, true, false>,   rasterSpanT<BlendMode::NONE, true, true>,
    rasterSpanT<BlendMode::ALPHA, false, false>, rasterSpanT<BlendMode::ALPHA, false, true>,
    rasterSpanT<BlendMode::ALPHA, true, false>,  rasterSpanT<BlendMode::ALPHA, true, true>,
    rasterSpanT<BlendMode::ADD, false, false>,   rasterSpanT<BlendMode::ADD, false, true>,
    rasterSpanT<BlendMode::ADD, true, false>,    rasterSpanT<BlendMode::ADD, true, true>,
};

static inline void rasterSpan(uint16_t *line, int rx0, const Span &sp) {
    RASTER_FNS[sp.tri->rasterFn](line + (sp.x0 - rx0), sp.x1 - sp.x0, sp);
}

// 2 つの昇順リストをマージする (リンクは s.link)
static uint16_t mergeLists(uint16_t a, uint16_t b) {
    uint16_t *link = s.link;
    uint16_t head = NONE;
    uint16_t *pp = &head;
    while (a != NONE && b != NONE) {
        if (a < b) {
            *pp = a;
            pp = &link[a];
            a = link[a];
        } else {
            *pp = b;
            pp = &link[b];
            b = link[b];
        }
    }
    *pp = (a != NONE) ? a : b;
    return head;
}

void render(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *dst, uint32_t stride) {
    if (!s.tris || !s.spanPool) return;
    const int rx0 = x, rx1 = x + w;
    const int y0 = std::max((int)y, 0);
    const int y1 = std::min((int)y + h, (int)s.screenH);
    if (y0 >= y1 || rx0 >= rx1) return;
    const uint16_t clear565 = packRGB565(s.clearColor.r, s.clearColor.g, s.clearColor.b);
    uint16_t *const link = s.link;

    // 領域内の各ラインについて、そのラインから交差し始める三角形のリストを作る
    // (order の位置 = 深度順で連結する)
    for (int i = y0; i < y1; i++) s.bucketHead[i] = s.bucketTail[i] = NONE;
    for (int p = 0; p < s.triCount; p++) {
        const Triangle &t = s.tris[s.order[p]];
        if (t.yMax < y0 || t.yMin >= y1) continue;
        int line = std::max((int)t.yMin, y0);
        link[p] = NONE;
        if (s.bucketTail[line] != NONE) {
            link[s.bucketTail[line]] = (uint16_t)p;
        } else {
            s.bucketHead[line] = (uint16_t)p;
        }
        s.bucketTail[line] = (uint16_t)p;
    }

    uint16_t active = NONE; // 現在のラインと交差する三角形 (深度順)
    for (int yi = y0; yi < y1; yi++) {
        active = mergeLists(active, s.bucketHead[yi]);

        // 線分リストをクリアする (ライン単位)
        s.spanCount = 0;
        s.opaqueHead = nullptr;
        s.transHead = s.transTail = nullptr;

        const float yc = (float)yi + 0.5f;
        uint16_t *pp = &active;
        while (*pp != NONE) {
            const uint16_t p = *pp;
            const Triangle &t = s.tris[s.order[p]];
            if (yi > t.yMax) {
                *pp = link[p]; // 通り過ぎた三角形をアクティブリストから外す
                continue;
            }
            Span sp;
            if (makeSpan(t, yc, rx0, rx1, sp)) {
                if (t.flags & TriFlags::OPAQUE) {
                    clipTranslucent(sp);
                    insertOpaque(sp);
                } else {
                    insertTranslucent(sp);
                }
            }
            pp = &link[p];
        }

        // 不透明線分 (x 昇順) とその隙間の背景色を描き、その上に半透明線分を合成する
        uint16_t *line = dst + (size_t)(yi - y) * stride;
        int cursor = rx0;
        for (const Span *e = s.opaqueHead; e; e = e->next) {
            if (e->x0 > cursor) fill16(line + (cursor - rx0), e->x0 - cursor, clear565);
            rasterSpan(line, rx0, *e);
            cursor = e->x1;
        }
        if (cursor < rx1) fill16(line + (cursor - rx0), rx1 - cursor, clear565);
        for (const Span *e = s.transHead; e; e = e->next) rasterSpan(line, rx0, *e);
    }
}

} // namespace shapoco::gfx3d
