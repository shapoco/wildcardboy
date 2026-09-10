#include "shapoco/gfx3d/gfx3d.hpp"

#include <algorithm>

// テクスチャの透視補正 ((u/w, v/w, 1/w) を補間してピクセル単位で除算する)。
// デフォルト無効 (アフィン補間)。有効にするには 1 を定義する。
#ifndef GFX3D_PERSPECTIVE_CORRECT_UV
#define GFX3D_PERSPECTIVE_CORRECT_UV 0
#endif

namespace shapoco::gfx3d {

// ---------------------------------------------------------------------------
// 内部データ構造

// ライティング・投影変換済みの頂点
struct ShadedVertex {
    float sx, sy; // スクリーン座標
    float zNdc;   // NDC 深度 (スクリーン空間で線形、小さいほど手前)
    vec2f uv;     // GFX3D_PERSPECTIVE_CORRECT_UV 有効時は (u/w, v/w)
#if GFX3D_PERSPECTIVE_CORRECT_UV
    float invW;   // 1/w
#endif
    colorf color;
};

struct Triangle {
    ShadedVertex v[3];
    const Material *mat;
    float depth;         // ビュー空間 z の平均 (小さいほど遠い)
    int16_t yMin, yMax;  // 交差するラインの範囲 (両端含む)
};

// スキャンライン上の線分。両端の属性を保持し、ピクセル単位で線形補間する
struct Span {
    float x0, x1;
    float z0, z1;    // NDC 深度
    colorf c0, c1;
    vec2f uv0, uv1;  // GFX3D_PERSPECTIVE_CORRECT_UV 有効時は (u/w, v/w)
#if GFX3D_PERSPECTIVE_CORRECT_UV
    float iw0, iw1;  // 1/w
#endif
    const Material *mat;
    Span *next;
};

struct StackEntry {
    mat4f matrix;
    const Material *material;
};

static constexpr int STACK_DEPTH = 16;
static constexpr int SPAN_CAPACITY_MIN = 32;
static constexpr int SPAN_CAPACITY_MAX = 512;

struct State {
    int16_t screenW = 0, screenH = 0;

    Triangle *tris = nullptr;
    int triCapacity = 0;
    int triCount = 0;

    Span *spanPool = nullptr;
    int spanCapacity = 0;
    int spanCount = 0;
    Span *spanHead = nullptr;

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

static inline void unpackRGB565(uint16_t c, float &r, float &g, float &b) {
    r = (float)((c >> 11) & 0x1f) * (1.0f / 31.0f);
    g = (float)((c >> 5) & 0x3f) * (1.0f / 63.0f);
    b = (float)(c & 0x1f) * (1.0f / 31.0f);
}

// ---------------------------------------------------------------------------
// 初期化

void init(int16_t w, int16_t h, void *arena, size_t arenaSize) {
    s = State();
    s.screenW = w;
    s.screenH = h;

    // アリーナを線分プールとトライアングルバッファに分配する
    uintptr_t p = (uintptr_t)arena;
    uintptr_t end = p + arenaSize;
    p = (p + 7u) & ~(uintptr_t)7u;

    size_t avail = (end > p) ? (size_t)(end - p) : 0;
    int spanCap = (int)((avail / 4) / sizeof(Span));
    spanCap = std::max(SPAN_CAPACITY_MIN, std::min(SPAN_CAPACITY_MAX, spanCap));
    s.spanPool = (Span *)p;
    s.spanCapacity = spanCap;
    p += (size_t)spanCap * sizeof(Span);
    p = (p + 7u) & ~(uintptr_t)7u;

    s.tris = (Triangle *)p;
    s.triCapacity = (end > p) ? (int)((end - p) / sizeof(Triangle)) : 0;
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
// 三角形の登録 (ライティング + 投影変換)

static void emitTriangle(const Vertex &a, const Vertex &b, const Vertex &c, const Material *mat) {
    if (!mat) return;
    if (s.triCount >= s.triCapacity) return; // バッファあふれ: このフレームでは破棄

    const Vertex *verts[3] = {&a, &b, &c};
    vec3f viewPos[3];
    for (int i = 0; i < 3; i++) {
        viewPos[i] = s.cur.transformPoint(verts[i]->position);
        // ニア平面をまたぐ・手前にある三角形は破棄する
        if (viewPos[i].z > -s.zNear) return;
    }

    Triangle &t = s.tris[s.triCount];
    for (int i = 0; i < 3; i++) {
        float w;
        vec3f clip = s.proj.transformPoint4(viewPos[i], w);
        if (w <= 0.0f) return;
        float invW = 1.0f / w;
        t.v[i].sx = (clip.x * invW * 0.5f + 0.5f) * s.screenW;
        t.v[i].sy = (0.5f - clip.y * invW * 0.5f) * s.screenH;
        t.v[i].zNdc = clip.z * invW;

        vec3f n = normalize(s.cur.transformDir(verts[i]->normal));

        vec2f uv;
        if (mat->flags & MaterialFlags::ENV_MAP) {
            // ビュー空間法線から環境マップの UV を求める
            uv = {n.x * 0.5f + 0.5f, 0.5f - n.y * 0.5f};
        } else {
            uv = verts[i]->uv;
        }
#if GFX3D_PERSPECTIVE_CORRECT_UV
        t.v[i].invW = invW;
        t.v[i].uv = uv * invW; // 透視補正のため 1/w を乗じる
#else
        t.v[i].uv = uv; // アフィン補間
#endif

        // グーローシェーディング: 頂点単位でライティング
        colorf col = {0, 0, 0, mat->diffuse.a};
        bool lit = false;
        if (s.envEnabled) {
            col.r += mat->ambient.r * s.envCol.r;
            col.g += mat->ambient.g * s.envCol.g;
            col.b += mat->ambient.b * s.envCol.b;
            lit = true;
        }
        if (s.lightEnabled) {
            float d = dot(n, -s.lightDir);
            if (d > 0.0f) {
                col.r += mat->diffuse.r * s.lightCol.r * d;
                col.g += mat->diffuse.g * s.lightCol.g * d;
                col.b += mat->diffuse.b * s.lightCol.b * d;
            }
            lit = true;
        }
        if (!lit) {
            col.r = mat->diffuse.r;
            col.g = mat->diffuse.g;
            col.b = mat->diffuse.b;
        }
        t.v[i].color = col;
    }

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

    t.mat = mat;
    t.depth = (viewPos[0].z + viewPos[1].z + viewPos[2].z) * (1.0f / 3.0f);
    t.yMin = (int16_t)yMin;
    t.yMax = (int16_t)yMax;
    s.triCount++;
}

void putPrimitive(const Primitive &prim) {
    const Material *mat = prim.material ? prim.material : s.curMat;
    const Vertex *verts = prim.vertexBuffer->vertices;
    const uint16_t *idx = prim.indices;
    int n = prim.indexCount;

    switch (prim.type) {
    case PrimitiveType::TRIANGLES:
        for (int i = 0; i + 2 < n; i += 3) {
            emitTriangle(verts[idx[i]], verts[idx[i + 1]], verts[idx[i + 2]], mat);
        }
        break;
    case PrimitiveType::TRIANGLE_STRIP:
        for (int i = 2; i < n; i++) {
            if (i & 1) {
                emitTriangle(verts[idx[i - 1]], verts[idx[i - 2]], verts[idx[i]], mat);
            } else {
                emitTriangle(verts[idx[i - 2]], verts[idx[i - 1]], verts[idx[i]], mat);
            }
        }
        break;
    case PrimitiveType::TRIANGLE_FAN:
        for (int i = 2; i < n; i++) {
            emitTriangle(verts[idx[0]], verts[idx[i - 1]], verts[idx[i]], mat);
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
    // このソートは主に半透明の合成順を決める
    std::sort(s.tris, s.tris + s.triCount,
              [](const Triangle &a, const Triangle &b) { return a.depth < b.depth; });
}

void endRender() {}

static Span *allocSpan() {
    if (s.spanCount >= s.spanCapacity) return nullptr;
    return &s.spanPool[s.spanCount++];
}

// 線分の右端を xc まで切り詰める (属性を再補間する)
static void clipSpanRight(Span &sp, float xc) {
    float t = (xc - sp.x0) / (sp.x1 - sp.x0);
    sp.c1 = lerp(sp.c0, sp.c1, t);
    sp.uv1 = lerp(sp.uv0, sp.uv1, t);
    sp.z1 = sp.z0 + (sp.z1 - sp.z0) * t;
#if GFX3D_PERSPECTIVE_CORRECT_UV
    sp.iw1 = sp.iw0 + (sp.iw1 - sp.iw0) * t;
#endif
    sp.x1 = xc;
}

// 線分の左端を xc まで切り詰める (属性を再補間する)
static void clipSpanLeft(Span &sp, float xc) {
    float t = (xc - sp.x0) / (sp.x1 - sp.x0);
    sp.c0 = lerp(sp.c0, sp.c1, t);
    sp.uv0 = lerp(sp.uv0, sp.uv1, t);
    sp.z0 = sp.z0 + (sp.z1 - sp.z0) * t;
#if GFX3D_PERSPECTIVE_CORRECT_UV
    sp.iw0 = sp.iw0 + (sp.iw1 - sp.iw0) * t;
#endif
    sp.x0 = xc;
}

// 線分上の位置 x における NDC 深度
static inline float spanDepthAt(const Span &sp, float x) {
    return sp.z0 + (sp.z1 - sp.z0) * (x - sp.x0) / (sp.x1 - sp.x0);
}

// 線分をリスト末尾に追加する。
// 不透明な線分が関与する重なりについては、三角形単位のソート順だけに頼らず、
// 重なり区間の中央での深度比較によって手前側の線分が残るように奥側を削除する。
// (巨大なポリゴンと小さなポリゴンの前後関係もこれで正しく解決される)
static void insertSpan(const Span &nIn) {
    // 新しい線分が既存の手前の線分に切られて複数の断片になる場合がある
    constexpr int MAX_FRAGS = 8;
    Span pending[MAX_FRAGS];
    int numPending = 0;
    pending[numPending++] = nIn;

    while (numPending > 0) {
        Span frag = pending[--numPending];
        bool fragOpaque = (frag.mat->blendMode == BlendMode::NONE);
        bool dropped = false;

        Span **pp = &s.spanHead;
        while (*pp) {
            Span *e = *pp;
            if (e->x1 <= frag.x0 || e->x0 >= frag.x1) {
                pp = &e->next;
                continue;
            }

            float ox0 = std::max(e->x0, frag.x0);
            float ox1 = std::min(e->x1, frag.x1);
            float xm = (ox0 + ox1) * 0.5f;
            bool fragNearer = spanDepthAt(frag, xm) < spanDepthAt(*e, xm);
            bool eOpaque = (e->mat->blendMode == BlendMode::NONE);

            if (fragNearer && fragOpaque) {
                // 既存線分の重なり部分を削除する
                bool leftRemains = e->x0 < ox0;
                bool rightRemains = e->x1 > ox1;
                if (leftRemains && rightRemains) {
                    // 中抜き: 右側を新しい線分として分割する
                    Span *r = allocSpan();
                    if (r) {
                        *r = *e;
                        clipSpanLeft(*r, ox1);
                        r->next = e->next;
                        e->next = r;
                    }
                    clipSpanRight(*e, ox0);
                    pp = &e->next;
                } else if (leftRemains) {
                    clipSpanRight(*e, ox0);
                    pp = &e->next;
                } else if (rightRemains) {
                    clipSpanLeft(*e, ox1);
                    pp = &e->next;
                } else {
                    *pp = e->next; // 完全に覆われた線分を削除
                }
            } else if (!fragNearer && eOpaque) {
                // 新しい線分の重なり部分を削除する
                bool leftRemains = frag.x0 < ox0;
                bool rightRemains = frag.x1 > ox1;
                if (leftRemains && rightRemains) {
                    if (numPending < MAX_FRAGS) {
                        Span right = frag;
                        clipSpanLeft(right, ox1);
                        pending[numPending++] = right;
                    }
                    clipSpanRight(frag, ox0);
                    pp = &e->next;
                } else if (leftRemains) {
                    clipSpanRight(frag, ox0);
                    pp = &e->next;
                } else if (rightRemains) {
                    clipSpanLeft(frag, ox1);
                    pp = &e->next;
                } else {
                    dropped = true; // 完全に隠されている
                    break;
                }
            } else {
                // 半透明同士、または半透明が手前にある場合はどちらも残す
                // (合成はリスト順 = 三角形の遠い順ソートに従う)
                pp = &e->next;
            }
        }

        if (dropped) continue;
        if (frag.x0 >= frag.x1) continue;
        Span *sp = allocSpan();
        if (!sp) continue; // プールあふれ: この線分は破棄
        *sp = frag;
        sp->next = nullptr;
        Span **tail = &s.spanHead;
        while (*tail) tail = &(*tail)->next;
        *tail = sp;
    }
}

// スキャンライン yc と三角形の交差から線分を生成する。交差しない場合は false
static bool makeSpan(const Triangle &t, float yc, Span &out) {
    struct EndPt {
        float x;
        float z;
        colorf c;
        vec2f uv;
#if GFX3D_PERSPECTIVE_CORRECT_UV
        float iw;
#endif
    };
    EndPt pts[3];
    int n = 0;

    for (int e = 0; e < 3; e++) {
        const ShadedVertex &a = t.v[e];
        const ShadedVertex &b = t.v[(e + 1) % 3];
        // 半開区間で判定して頂点上の交差の二重カウントを防ぐ
        if ((a.sy <= yc && yc < b.sy) || (b.sy <= yc && yc < a.sy)) {
            float tt = (yc - a.sy) / (b.sy - a.sy);
            pts[n].x = a.sx + (b.sx - a.sx) * tt;
            pts[n].z = a.zNdc + (b.zNdc - a.zNdc) * tt;
            pts[n].c = lerp(a.color, b.color, tt);
            pts[n].uv = lerp(a.uv, b.uv, tt);
#if GFX3D_PERSPECTIVE_CORRECT_UV
            pts[n].iw = a.invW + (b.invW - a.invW) * tt;
#endif
            n++;
        }
    }
    if (n < 2) return false;

    int li = 0, ri = 1;
    if (pts[li].x > pts[ri].x) std::swap(li, ri);
    if (pts[li].x >= pts[ri].x) return false; // 幅ゼロ

    out.x0 = pts[li].x;
    out.x1 = pts[ri].x;
    out.c0 = pts[li].c;
    out.c1 = pts[ri].c;
    out.z0 = pts[li].z;
    out.z1 = pts[ri].z;
    out.uv0 = pts[li].uv;
    out.uv1 = pts[ri].uv;
#if GFX3D_PERSPECTIVE_CORRECT_UV
    out.iw0 = pts[li].iw;
    out.iw1 = pts[ri].iw;
#endif
    out.mat = t.mat;
    out.next = nullptr;
    return true;
}

static inline void sampleTexture(const Texture &tex, const vec2f &uv, float &r, float &g, float &b) {
    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);
    int tx = (int)(u * tex.width);
    int ty = (int)(v * tex.height);
    if (tx >= tex.width) tx = tex.width - 1;
    if (ty >= tex.height) ty = tex.height - 1;
    unpackRGB565(tex.pixels[ty * tex.width + tx], r, g, b);
}

static void rasterizeLine(uint16_t *dst, float regionX0, float regionX1) {
    for (const Span *sp = s.spanHead; sp; sp = sp->next) {
        const Material &mat = *sp->mat;
        bool useTex = (mat.flags & (MaterialFlags::TEXTURE | MaterialFlags::ENV_MAP)) && mat.texture;
        float invW = 1.0f / (sp->x1 - sp->x0);

        // ピクセル中心 (xi + 0.5) が [x0, x1) に入るピクセルを塗る
        int xiBegin = (int)std::ceil(sp->x0 - 0.5f);
        int xiEnd = (int)std::ceil(sp->x1 - 0.5f);
        xiBegin = std::max(xiBegin, (int)regionX0);
        xiEnd = std::min(xiEnd, (int)regionX1);

        for (int xi = xiBegin; xi < xiEnd; xi++) {
            float t = ((float)xi + 0.5f - sp->x0) * invW;
            colorf col = lerp(sp->c0, sp->c1, t);

            if (useTex) {
#if GFX3D_PERSPECTIVE_CORRECT_UV
                // 透視補正: (u/w, v/w) と 1/w を線形補間し、ピクセル単位で除算する
                vec2f uvw = lerp(sp->uv0, sp->uv1, t);
                float iw = sp->iw0 + (sp->iw1 - sp->iw0) * t;
                vec2f uv = uvw * (1.0f / iw);
#else
                vec2f uv = lerp(sp->uv0, sp->uv1, t); // アフィン補間
#endif
                float tr, tg, tb;
                sampleTexture(*mat.texture, uv, tr, tg, tb);
                col.r *= tr;
                col.g *= tg;
                col.b *= tb;
            }

            uint16_t *px = &dst[xi - (int)regionX0];
            switch (mat.blendMode) {
            case BlendMode::NONE:
                *px = packRGB565(col.r, col.g, col.b);
                break;
            case BlendMode::ALPHA: {
                float dr, dg, db;
                unpackRGB565(*px, dr, dg, db);
                float ia = 1.0f - col.a;
                *px = packRGB565(col.r * col.a + dr * ia, col.g * col.a + dg * ia, col.b * col.a + db * ia);
                break;
            }
            case BlendMode::ADD: {
                float dr, dg, db;
                unpackRGB565(*px, dr, dg, db);
                *px = packRGB565(dr + col.r * col.a, dg + col.g * col.a, db + col.b * col.a);
                break;
            }
            }
        }
    }
}

void render(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *dst, uint32_t stride) {
    uint16_t clear565 = packRGB565(s.clearColor.r, s.clearColor.g, s.clearColor.b);
    float regionX0 = (float)x;
    float regionX1 = (float)(x + w);

    for (int yi = y; yi < y + h; yi++) {
        // 線分リストをクリアする (ライン単位)
        s.spanCount = 0;
        s.spanHead = nullptr;

        float yc = (float)yi + 0.5f;
        for (int i = 0; i < s.triCount; i++) {
            const Triangle &t = s.tris[i];
            if (yi < t.yMin || yi > t.yMax) continue;
            Span sp;
            if (!makeSpan(t, yc, sp)) continue;
            // 領域の水平範囲でクリップする
            if (sp.x1 <= regionX0 || sp.x0 >= regionX1) continue;
            if (sp.x0 < regionX0) clipSpanLeft(sp, regionX0);
            if (sp.x1 > regionX1) clipSpanRight(sp, regionX1);
            if (sp.x0 >= sp.x1) continue;
            insertSpan(sp);
        }

        uint16_t *line = dst + (size_t)(yi - y) * stride;
        for (int xi = 0; xi < w; xi++) line[xi] = clear565;
        rasterizeLine(line, regionX0, regionX1);
    }
}

} // namespace shapoco::gfx3d
