// =====================================================================
//  Nekonečný destruktivní terén – SURVIVAL  (C++ + raylib 5.x, OpenGL 3.3)
//
//  * hladký terén, nekonečný do stran i do hloubky (pod povrchem kámen donekonečna)
//  * skutečná stébla trávy s větrem (kolem hráče)
//  * pracovní stůl: u něj se KLADÍVKEM vyrábí granáty, rakety a dynamit
//  * survival: srdíčka, hlad, žízeň, dech, pád, výbuchy zraňují
//  * inventář + hotbar, kácení stromů (dřevo, jablka), pití vody
//  * plynulý denní cyklus s nocí, hvězdami a měsícem
//
//  Ovládání:
//    WASD pohyb, myš rozhled, MEZERNÍK skok, SHIFT sprint
//    LMB (držet)  kopat / kácet strom (u stolu: bít kladívkem)
//    PRAVÉ        použít vybranou věc (kámen položit, jablko sníst, granát/dynamit
//                 hodit, raketa vystřelit, stůl postavit)
//    1-8 / kolečko = výběr věci     F (držet) = pít z vody
//    E = použít pracovní stůl (kouknout na něj, mít kladívko)
//    TAB = inventář a ruční výroba (1 = stůl, 2 = kladívko)
//    F11 / Alt+Enter = celá obrazovka     R = nový svět     ESC = konec
//
//  Android: stejný zdroj, překládá se s -DPLATFORM_ANDROID (viz balíček build_android.sh).
//    levá půlka = joystick, pravá půlka = rozhlížení, tlačítka KOP / POUZIJ / SKOK / BEH / INV / E / F,
//    hotbar se vybírá klepnutím na slot.
// =====================================================================

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------- Konstanty
constexpr float CELL = 0.5f;               // rozestup vzorků hustoty (m)
constexpr int   N = 32;                    // buněk na chunk
constexpr float CHUNK_M = N * CELL;        // 16 m
constexpr int   M = N + 2;                 // vzorků na osu (-1..N)
#if defined(PLATFORM_ANDROID)
constexpr int   VIEW_R = 6;                // dohled v chunkách (telefon)
#else
constexpr int   VIEW_R = 10;               // dohled v chunkách
#endif
constexpr float SEA = 0.0f;                // hladina moře
constexpr float G = 9.81f;
constexpr float DAY_SECONDS = 600.0f;      // délka celého dne v reálných sekundách
#if defined(PLATFORM_ANDROID)
constexpr size_t MAX_PARTS = 2500;
#else
constexpr size_t MAX_PARTS = 6000;
#endif

#if defined(PLATFORM_ANDROID)
constexpr float GRASS_DENSITY = 5.0f;      // stébel na m²
constexpr float GRASS_BUILD = 36.0f;       // do této vzdálenosti se tráva generuje
constexpr float GRASS_UNLOAD = 52.0f;      // dál se uvolní
constexpr float GRASS_DRAW = 40.0f;        // do této vzdálenosti se kreslí
constexpr double CAM_NEAR = 0.15, CAM_FAR = 600.0;
#else
constexpr float GRASS_DENSITY = 9.0f;      // stébel na m²
constexpr float GRASS_BUILD = 54.0f;       // do této vzdálenosti se tráva generuje
constexpr float GRASS_UNLOAD = 74.0f;      // dál se uvolní
constexpr float GRASS_DRAW = 60.0f;        // do této vzdálenosti se kreslí
constexpr double CAM_NEAR = 0.1, CAM_FAR = 2000.0;
#endif
static float gUIS = 1.0f;                  // měřítko HUD (na telefonu podle výšky displeje)

// ---------------------------------------------------------------- Pomocné
static inline Vector3 vec3(float x, float y, float z) { return {x, y, z}; }
static inline Vector3 vadd(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vector3 vsub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vector3 vmul(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float vdot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vector3 vcross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline float vlen(Vector3 a) { return sqrtf(vdot(a, a)); }
static inline Vector3 vnorm(Vector3 a) {
    float l = vlen(a);
    return l > 1e-6f ? vmul(a, 1.0f / l) : vec3(0, 1, 0);
}
static inline Vector3 vlerp(Vector3 a, Vector3 b, float t) { return vadd(vmul(a, 1.0f - t), vmul(b, t)); }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float smoothstepf(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
static inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}
static inline unsigned char clamp8f(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static float frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.0f; }

struct IVec3 { int x, y, z; };

// ---------------------------------------------------------------- Šum a terén
static float hash2(int x, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777216.0f;
}
static float valueNoise(float x, float z) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    float a = hash2(ix, iz), b = hash2(ix + 1, iz), c = hash2(ix, iz + 1), d = hash2(ix + 1, iz + 1);
    float t = a + (b - a) * fx, u = c + (d - c) * fx;
    return t + (u - t) * fz;
}
static float fbm2(float x, float z, int oct) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < oct; i++) {
        sum += valueNoise(x, z) * amp;
        norm += amp;
        amp *= 0.5f;
        x = x * 2.03f + 17.1f;
        z = z * 2.03f + 9.7f;
    }
    return sum / norm;
}
static float contrast(float v, float k) { return clamp01((v - 0.5f) * k + 0.5f); }

static float gSeedX = 0, gSeedZ = 0;

static float terrainH(float x, float z) {
    x += gSeedX;
    z += gSeedZ;
    float cont = contrast(fbm2(x * 0.0022f + 31.7f, z * 0.0022f + 17.3f, 4), 2.2f);
    float hill = contrast(fbm2(x * 0.010f + 5.1f, z * 0.010f + 9.7f, 4), 1.8f);
    float rid = 1.0f - fabsf(2.0f * fbm2(x * 0.0055f + 100.0f, z * 0.0055f + 55.0f, 3) - 1.0f);
    float mask = smoothstepf(0.50f, 0.80f, cont);
    float det = fbm2(x * 0.045f, z * 0.045f, 3);
    return (cont - 0.42f) * 64.0f + (hill - 0.5f) * 24.0f + rid * rid * mask * 52.0f + (det - 0.5f) * 4.0f;
}

// ---------------------------------------------------------------- Pole hustoty + úpravy
// hustota > 0 = pevná hmota, <= 0 = vzduch (zhruba vzdálenost k povrchu v metrech)
static std::unordered_map<uint64_t, float> edits;
static std::unordered_map<uint64_t, IVec3> editedChunks;
static std::unordered_set<uint64_t> deadTrees;
static std::unordered_map<uint64_t, int> treeHits;

static inline uint64_t skey(int x, int y, int z) {
    return (uint64_t)(x + 1048576) | ((uint64_t)(y + 1048576) << 21) | ((uint64_t)(z + 1048576) << 42);
}
static inline uint64_t ckey(int x, int y, int z) { return skey(x, y, z); }
static inline void decodeKey(uint64_t k, int &x, int &y, int &z) {
    x = (int)(k & 0x1FFFFF) - 1048576;
    y = (int)((k >> 21) & 0x1FFFFF) - 1048576;
    z = (int)((k >> 42) & 0x1FFFFF) - 1048576;
}

static float sampleD(int sx, int sy, int sz) {
    if (!edits.empty()) {
        auto it = edits.find(skey(sx, sy, sz));
        if (it != edits.end()) return it->second;
    }
    return terrainH(sx * CELL, sz * CELL) - sy * CELL;
}

// trilineární hustota + gradient v libovolném bodě
static float densityGrad(Vector3 p, Vector3 *g) {
    float fx = p.x / CELL, fy = p.y / CELL, fz = p.z / CELL;
    int ix = (int)floorf(fx), iy = (int)floorf(fy), iz = (int)floorf(fz);
    float tx = fx - ix, ty = fy - iy, tz = fz - iz;
    float c[2][2][2];
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int d = 0; d < 2; d++) c[a][b][d] = sampleD(ix + a, iy + b, iz + d);
    float wy[2] = {1 - ty, ty}, wz[2] = {1 - tz, tz}, wx[2] = {1 - tx, tx};
    float dens = 0, gx = 0, gy = 0, gz = 0;
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int d = 0; d < 2; d++) dens += c[a][b][d] * wx[a] * wy[b] * wz[d];
    if (g) {
        for (int b = 0; b < 2; b++)
            for (int d = 0; d < 2; d++) gx += (c[1][b][d] - c[0][b][d]) * wy[b] * wz[d];
        for (int a = 0; a < 2; a++)
            for (int d = 0; d < 2; d++) gy += (c[a][1][d] - c[a][0][d]) * wx[a] * wz[d];
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) gz += (c[a][b][1] - c[a][b][0]) * wx[a] * wy[b];
        *g = vec3(gx / CELL, gy / CELL, gz / CELL);
    }
    return dens;
}

// ---------------------------------------------------------------- Barva terénu
static void groundColor(float x, float y, float z, float ny, float hb, float &R, float &Gc, float &B,
                        float &rockW, float &grassW) {
    float depth = hb - y;
    float n1 = valueNoise(x * 0.30f + 3.0f, z * 0.30f + 9.0f);
    float n2 = valueNoise(x * 1.7f + y * 0.9f, z * 1.7f - y * 0.6f);
    float rockSlope = smoothstepf(0.80f, 0.52f, ny);
    float wR, wG = 0, wD = 0, wS = 0, wN = 0;
    if (depth < -0.45f) {
        wR = 1;                              // přidaný kámen
    } else {
        wR = fmaxf(smoothstepf(2.4f, 3.6f, depth), rockSlope);
        float soil = 1.0f - wR;
        float sf = smoothstepf(2.2f, 0.7f, y) * smoothstepf(1.4f, 0.3f, depth);
        float gf = smoothstepf(1.1f, 0.25f, depth);
        wS = soil * sf;
        wG = soil * (1 - sf) * gf;
        wD = soil * (1 - sf) * (1 - gf);
        wN = smoothstepf(58.0f, 70.0f, y + (n1 - 0.5f) * 14.0f) * smoothstepf(0.50f, 0.75f, ny) * (depth < 1.5f ? 1.0f : 0.0f);
    }
    float gr = 70 + (112 - 70) * n1, gg = 108 + (134 - 108) * n1, gb = 38 + (56 - 38) * n1;
    float gd = 0.80f + 0.35f * n2;
    float dr = 106 * (0.85f + 0.3f * n2), dg = 80 * (0.85f + 0.3f * n2), db = 58 * (0.85f + 0.3f * n2);
    float rk = 0.78f + 0.40f * n2;
    float rr = 116 * rk, rg = 112 * rk, rb = 106 * rk;
    R = gr * gd * wG + dr * wD + rr * wR + 198 * wS;
    Gc = gg * gd * wG + dg * wD + rg * wR + 180 * wS;
    B = gb * gd * wG + db * wD + rb * wR + 130 * wS;
    R = R * (1 - wN) + 236 * wN;
    Gc = Gc * (1 - wN) + 241 * wN;
    B = B * (1 - wN) + 248 * wN;
    rockW = wR * (1 - wN);
    grassW = wG * (1 - wN);
}

// ---------------------------------------------------------------- Stromy a kameny
struct TreeInfo { Vector3 p; float h, rad, s; int type; };

static uint64_t treeId(int gx, int gz) { return ((uint64_t)(uint32_t)gx << 32) | (uint32_t)gz; }

static bool treeAt(int gx, int gz, TreeInfo &t) {
    const float CT = 4.0f;
    float r0 = hash2(gx * 7 + 13, gz * 11 + 5);
    float x = (gx + hash2(gx + 101, gz + 7)) * CT, z = (gz + hash2(gx + 11, gz + 303)) * CT;
    float forest = fbm2((x + gSeedX) * 0.012f + 50, (z + gSeedZ) * 0.012f + 80, 3);
    float prob = clamp01((forest - 0.44f) * 2.4f) * 0.8f;
    if (r0 > prob) return false;
    float y = terrainH(x, z);
    if (y < 1.6f || y > 50.0f) return false;
    float sx = terrainH(x + 1, z) - terrainH(x - 1, z), sz = terrainH(x, z + 1) - terrainH(x, z - 1);
    if (fabsf(sx) + fabsf(sz) > 1.1f) return false;
    float r1 = hash2(gx * 3 + 77, gz * 5 + 19), r2 = hash2(gx * 13 + 1, gz * 17 + 3);
    t.p = vec3(x, y - 0.3f, z);
    t.type = (y > 28.0f || r2 > 0.8f) ? 1 : 0;
    t.s = 0.8f + 0.5f * r1;
    if (t.type == 0) { t.h = 6.0f + 4.0f * r1; t.rad = 2.2f + 1.2f * r2; }
    else { t.h = 9.0f + 6.0f * r1; t.rad = 1.6f + 0.8f * r2; }
    return true;
}

static bool rockAt(int gx, int gz, Vector3 &p, float &size) {
    float r = hash2(gx * 19 + 4, gz * 23 + 8);
    if (r > 0.045f) return false;
    float x = (gx + hash2(gx + 5, gz + 91)) * 4.0f, z = (gz + hash2(gx + 61, gz + 17)) * 4.0f;
    float y = terrainH(x, z);
    if (y < 0.8f) return false;
    p = vec3(x, y, z);
    size = 0.7f + 1.6f * hash2(gx + 33, gz + 71);
    return true;
}

// ---------------------------------------------------------------- Mesh builder
struct MB {
    std::vector<float> pos, nor;
    std::vector<unsigned char> col;
    std::vector<unsigned short> idx;
    int count() const { return (int)(pos.size() / 3); }
    int add(Vector3 p, Vector3 n, float r, float g, float b, unsigned char a) {
        pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
        nor.push_back(n.x); nor.push_back(n.y); nor.push_back(n.z);
        col.push_back(clamp8f(r)); col.push_back(clamp8f(g)); col.push_back(clamp8f(b)); col.push_back(a);
        return count() - 1;
    }
    void tri(int a, int b, int c) {
        idx.push_back((unsigned short)a); idx.push_back((unsigned short)b); idx.push_back((unsigned short)c);
    }
};

static Mesh makeMesh(const MB &mb) {
    Mesh m = {};
    m.vertexCount = mb.count();
    m.triangleCount = (int)mb.idx.size() / 3;
    m.vertices = (float *)MemAlloc((unsigned)(mb.pos.size() * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned)(mb.nor.size() * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned)mb.col.size());
    m.indices = (unsigned short *)MemAlloc((unsigned)(mb.idx.size() * sizeof(unsigned short)));
    memcpy(m.vertices, mb.pos.data(), mb.pos.size() * sizeof(float));
    memcpy(m.normals, mb.nor.data(), mb.nor.size() * sizeof(float));
    memcpy(m.colors, mb.col.data(), mb.col.size());
    memcpy(m.indices, mb.idx.data(), mb.idx.size() * sizeof(unsigned short));
    UploadMesh(&m, false);
    return m;
}

static void addBlob(MB &mb, Vector3 ctr, Vector3 rad, Color c, unsigned char a, int seed, float amp) {
    const int LAT = 7, LON = 10;
    int base = mb.count();
    for (int i = 0; i <= LAT; i++) {
        float th = PI * i / LAT;
        for (int j = 0; j <= LON; j++) {
            float ph = 2.0f * PI * (j % LON) / LON;
            Vector3 d = {sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph)};
            float nz = valueNoise(d.x * 2.2f + seed * 1.7f, d.z * 2.2f + d.y * 1.9f + seed * 0.9f);
            float s = 1.0f + amp * (nz - 0.5f) * 2.0f;
            Vector3 p = {ctr.x + d.x * rad.x * s, ctr.y + d.y * rad.y * s, ctr.z + d.z * rad.z * s};
            float shade = 0.74f + 0.34f * (d.y * 0.5f + 0.5f) + (nz - 0.5f) * 0.35f;
            mb.add(p, d, c.r * shade, c.g * shade, c.b * shade, a);
        }
    }
    for (int i = 0; i < LAT; i++)
        for (int j = 0; j < LON; j++) {
            int a0 = base + i * (LON + 1) + j, b0 = a0 + LON + 1;
            mb.tri(a0, b0 + 1, b0);
            mb.tri(a0, a0 + 1, b0 + 1);
        }
}

static void addCone(MB &mb, Vector3 base, float radius, float height, Color c, int seed) {
    const int SEG = 8;
    int r0 = mb.count();
    for (int j = 0; j <= SEG; j++) {
        float ph = 2.0f * PI * (j % SEG) / SEG;
        Vector3 n = vnorm(vec3(cosf(ph) * height, radius, sinf(ph) * height));
        float v = 0.75f + 0.25f * hash2(seed + j, seed * 3);
        mb.add(vec3(base.x + cosf(ph) * radius, base.y, base.z + sinf(ph) * radius), n, c.r * v * 0.8f, c.g * v * 0.8f, c.b * v * 0.8f, 0);
    }
    int a0 = mb.count();
    for (int j = 0; j < SEG; j++) {
        float ph = 2.0f * PI * (j + 0.5f) / SEG;
        Vector3 n = vnorm(vec3(cosf(ph) * height, radius, sinf(ph) * height));
        mb.add(vec3(base.x, base.y + height, base.z), n, c.r * 1.1f, c.g * 1.1f, c.b * 1.1f, 0);
    }
    for (int j = 0; j < SEG; j++) mb.tri(r0 + j, a0 + j, r0 + j + 1);
}

static void addTrunk(MB &mb, Vector3 base, float h, float rb, float rt, Color c) {
    const int SEG = 6;
    int b0 = mb.count();
    for (int j = 0; j <= SEG; j++) {
        float ph = 2.0f * PI * (j % SEG) / SEG;
        Vector3 n = vec3(cosf(ph), 0.1f, sinf(ph));
        mb.add(vec3(base.x + cosf(ph) * rb, base.y, base.z + sinf(ph) * rb), n, c.r * 0.8f, c.g * 0.8f, c.b * 0.8f, 0);
    }
    int t0 = mb.count();
    for (int j = 0; j <= SEG; j++) {
        float ph = 2.0f * PI * (j % SEG) / SEG;
        Vector3 n = vec3(cosf(ph), 0.1f, sinf(ph));
        mb.add(vec3(base.x + cosf(ph) * rt, base.y + h, base.z + sinf(ph) * rt), n, c.r, c.g, c.b, 0);
    }
    for (int j = 0; j < SEG; j++) {
        mb.tri(b0 + j, t0 + j, b0 + j + 1);
        mb.tri(b0 + j + 1, t0 + j, t0 + j + 1);
    }
}

static void addTree(MB &mb, const TreeInfo &t, Vector3 origin, int gx, int gz) {
    Vector3 b = vsub(t.p, origin);
    Color trunk = {74, 54, 38, 255};
    int seed = gx * 31 + gz * 17;
    if (t.type == 0) {
        float th = t.h * 0.60f;
        addTrunk(mb, b, th, 0.34f * t.s, 0.12f * t.s, trunk);
        Vector3 top = vadd(b, vec3(0, th, 0));
        float gv = hash2(seed, 5);
        Color leaf = {(unsigned char)(48 + 30 * gv), (unsigned char)(92 + 30 * gv), (unsigned char)(34 + 10 * gv), 255};
        addBlob(mb, vadd(top, vec3(0, t.h * 0.20f, 0)), vec3(t.rad, t.rad * 0.85f, t.rad), leaf, 0, seed, 0.28f);
        for (int i = 0; i < 3; i++) {
            float a = 2.0f * PI * (i / 3.0f + hash2(seed, i) * 0.3f);
            addBlob(mb, vadd(top, vec3(cosf(a) * t.rad * 0.6f, t.h * 0.08f + i * 0.2f, sinf(a) * t.rad * 0.6f)),
                    vec3(t.rad * 0.65f, t.rad * 0.55f, t.rad * 0.65f), leaf, 0, seed + i + 1, 0.3f);
        }
    } else {
        addTrunk(mb, b, t.h * 0.35f, 0.30f * t.s, 0.12f * t.s, trunk);
        Color leaf = {34, 74, 46, 255};
        for (int i = 0; i < 4; i++) {
            float f = i / 4.0f;
            addCone(mb, vadd(b, vec3(0, t.h * (0.22f + 0.2f * f), 0)), t.rad * (1.15f - 0.7f * f), t.h * (0.42f - 0.1f * f), leaf, seed + i);
        }
    }
}

// kvádr (pro pracovní stůl)
static const int BOX_FACE[6][4][3] = {
    {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},  // +Y
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},  // -Y
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},  // +X
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},  // -X
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},  // +Z
    {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},  // -Z
};
static const float BOX_NRM[6][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
static const float BOX_SHADE[6] = {1.0f, 0.55f, 0.85f, 0.85f, 0.75f, 0.75f};

static void addBox(MB &mb, Vector3 mn, Vector3 sz, Color c) {
    for (int f = 0; f < 6; f++) {
        int base = mb.count();
        for (int i = 0; i < 4; i++) {
            Vector3 p = {mn.x + BOX_FACE[f][i][0] * sz.x, mn.y + BOX_FACE[f][i][1] * sz.y, mn.z + BOX_FACE[f][i][2] * sz.z};
            float s = BOX_SHADE[f];
            mb.add(p, vec3(BOX_NRM[f][0], BOX_NRM[f][1], BOX_NRM[f][2]), c.r * s, c.g * s, c.b * s, 0);
        }
        mb.tri(base, base + 1, base + 2);
        mb.tri(base, base + 2, base + 3);
    }
}

static Mesh buildTableMesh() {
    MB mb;
    Color top = {154, 112, 66, 255}, leg = {112, 80, 48, 255}, shelf = {124, 90, 54, 255};
    Color metal = {128, 130, 136, 255}, handle = {160, 120, 76, 255}, dark = {70, 52, 34, 255};
    addBox(mb, vec3(-0.75f, 0.80f, -0.45f), vec3(1.5f, 0.12f, 0.9f), top);              // deska
    for (int sx = 0; sx < 2; sx++)
        for (int sz = 0; sz < 2; sz++)
            addBox(mb, vec3(sx ? 0.60f : -0.72f, 0.0f, sz ? 0.30f : -0.42f), vec3(0.12f, 0.80f, 0.12f), leg);
    addBox(mb, vec3(-0.66f, 0.22f, -0.36f), vec3(1.32f, 0.06f, 0.72f), shelf);          // police
    addBox(mb, vec3(-0.75f, 0.60f, -0.45f), vec3(1.5f, 0.08f, 0.06f), dark);            // zárubeň
    addBox(mb, vec3(-0.30f, 0.92f, -0.10f), vec3(0.55f, 0.05f, 0.05f), handle);         // kladívko na stole
    addBox(mb, vec3(0.20f, 0.92f, -0.15f), vec3(0.10f, 0.11f, 0.16f), metal);
    addBox(mb, vec3(0.38f, 0.92f, 0.12f), vec3(0.30f, 0.14f, 0.22f), metal);            // kovadlinka
    addBox(mb, vec3(0.43f, 1.06f, 0.15f), vec3(0.20f, 0.06f, 0.16f), metal);
    return makeMesh(mb);
}

// ---------------------------------------------------------------- Sloupce a chunky
struct Column {
    std::vector<float> h;   // výšky pro vzorky (-1..N) v X a Z
    float hmin = 1e9f, hmax = -1e9f;
};
static std::unordered_map<uint64_t, Column> columns;

static Column &getColumn(int cx, int cz) {
    uint64_t key = skey(cx, 0, cz);
    auto it = columns.find(key);
    if (it != columns.end()) return it->second;
    Column col;
    col.h.resize(M * M);
    for (int i = 0; i < M; i++)
        for (int k = 0; k < M; k++) {
            float h = terrainH((cx * N - 1 + i) * CELL, (cz * N - 1 + k) * CELL);
            col.h[i * M + k] = h;
            col.hmin = std::min(col.hmin, h);
            col.hmax = std::max(col.hmax, h);
        }
    return columns.emplace(key, std::move(col)).first->second;
}

struct Chunk {
    Mesh mesh{};
    bool has = false;
    bool dirty = true;
    std::vector<unsigned char> gw;    // "travnatost" vrcholů povrchu (0..255)
    int surfV = 0, surfI = 0;         // počet vrcholů / indexů samotného povrchu (bez stromů)
    Mesh grass{};
    bool hasGrass = false;
    bool grassDone = false;
};
static std::unordered_map<uint64_t, Chunk> chunkMap;

static void freeGrass(Chunk &ch) {
    if (ch.hasGrass) { UnloadMesh(ch.grass); ch.hasGrass = false; }
    ch.grassDone = false;
}

static const int EDGES[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

static void buildChunk(int cx, int cy, int cz, Chunk &ch) {
    if (ch.has) { UnloadMesh(ch.mesh); ch.has = false; }
    freeGrass(ch);
    ch.gw.clear();
    ch.surfV = ch.surfI = 0;
    ch.dirty = false;
    Column &col = getColumn(cx, cz);
    bool edited = editedChunks.count(ckey(cx, cy, cz)) > 0;
    float y0 = (cy * N - 1) * CELL, y1 = (cy * N + N) * CELL;
    if (!edited && (y0 > col.hmax + 1.0f || y1 < col.hmin - 1.0f)) return;

    static std::vector<float> D;
    D.resize((size_t)M * M * M);
    float dmin = 1e9f, dmax = -1e9f;
    for (int i = 0; i < M; i++)
        for (int j = 0; j < M; j++) {
            float y = (cy * N - 1 + j) * CELL;
            for (int k = 0; k < M; k++) {
                float d = col.h[i * M + k] - y;
                if (edited) {
                    auto it = edits.find(skey(cx * N - 1 + i, cy * N - 1 + j, cz * N - 1 + k));
                    if (it != edits.end()) d = it->second;
                }
                D[((size_t)i * M + j) * M + k] = d;
                dmin = std::min(dmin, d);
                dmax = std::max(dmax, d);
            }
        }
    if (dmin > 0 || dmax <= 0) return;   // žádný povrch

    Vector3 origin = {cx * CHUNK_M, cy * CHUNK_M, cz * CHUNK_M};
    auto Dat = [&](int i, int j, int k) { return D[((size_t)i * M + j) * M + k]; };
    auto localD = [&](Vector3 lp) {
        float u = lp.x / CELL + 1.0f, v = lp.y / CELL + 1.0f, w = lp.z / CELL + 1.0f;
        u = std::min(std::max(u, 0.0f), M - 1.001f);
        v = std::min(std::max(v, 0.0f), M - 1.001f);
        w = std::min(std::max(w, 0.0f), M - 1.001f);
        int i = (int)u, j = (int)v, k = (int)w;
        float fu = u - i, fv = v - j, fw = w - k;
        float r = 0;
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++)
                for (int c = 0; c < 2; c++)
                    r += Dat(i + a, j + b, k + c) * (a ? fu : 1 - fu) * (b ? fv : 1 - fv) * (c ? fw : 1 - fw);
        return r;
    };

    const int C = N + 1;
    static std::vector<int> vid;
    vid.assign((size_t)C * C * C, -1);
    MB mb;
    mb.pos.reserve(30000); mb.nor.reserve(30000); mb.col.reserve(40000); mb.idx.reserve(60000);

    for (int ci = 0; ci < C; ci++)
        for (int cj = 0; cj < C; cj++)
            for (int ck = 0; ck < C; ck++) {
                float v[8];
                int mask = 0;
                for (int c = 0; c < 8; c++) {
                    v[c] = Dat(ci + (c & 1), cj + ((c >> 1) & 1), ck + ((c >> 2) & 1));
                    if (v[c] > 0) mask |= 1 << c;
                }
                if (mask == 0 || mask == 255) continue;
                Vector3 sum = {0, 0, 0};
                int cnt = 0;
                for (int e = 0; e < 12; e++) {
                    int a = EDGES[e][0], b = EDGES[e][1];
                    if (((mask >> a) & 1) == ((mask >> b) & 1)) continue;
                    float t = v[a] / (v[a] - v[b]);
                    Vector3 pa = {(float)(a & 1), (float)((a >> 1) & 1), (float)((a >> 2) & 1)};
                    Vector3 pb = {(float)(b & 1), (float)((b >> 1) & 1), (float)((b >> 2) & 1)};
                    sum = vadd(sum, vlerp(pa, pb, t));
                    cnt++;
                }
                Vector3 f = vmul(sum, 1.0f / cnt);

                // normála z trilineárního gradientu
                float wx[2] = {1 - f.x, f.x}, wy[2] = {1 - f.y, f.y}, wz[2] = {1 - f.z, f.z};
                float gx = 0, gy = 0, gz = 0;
                for (int b = 0; b < 2; b++)
                    for (int d = 0; d < 2; d++) gx += (v[1 + 2 * b + 4 * d] - v[0 + 2 * b + 4 * d]) * wy[b] * wz[d];
                for (int a = 0; a < 2; a++)
                    for (int d = 0; d < 2; d++) gy += (v[a + 2 + 4 * d] - v[a + 0 + 4 * d]) * wx[a] * wz[d];
                for (int a = 0; a < 2; a++)
                    for (int b = 0; b < 2; b++) gz += (v[a + 2 * b + 4] - v[a + 2 * b]) * wx[a] * wy[b];
                Vector3 n = vnorm(vec3(-gx, -gy, -gz));

                Vector3 lp = {(ci - 1 + f.x) * CELL, (cj - 1 + f.y) * CELL, (ck - 1 + f.z) * CELL};
                Vector3 wp = vadd(origin, lp);

                float R, Gc, B, rockW, grassW;
                groundColor(wp.x, wp.y, wp.z, n.y, terrainH(wp.x, wp.z), R, Gc, B, rockW, grassW);

                // ambient occlusion z hustoty (dutiny jsou tmavší)
                float occ = 0.45f * smoothstepf(-0.3f, 0.3f, localD(vadd(lp, vmul(n, 0.4f)))) +
                            0.35f * smoothstepf(-0.3f, 0.3f, localD(vadd(lp, vmul(n, 0.9f)))) +
                            0.25f * smoothstepf(-0.3f, 0.3f, localD(vadd(lp, vmul(n, 1.8f))));
                float ao = 1.0f - 0.85f * occ;
                float k = 0.45f + 0.55f * ao;
                vid[((size_t)ci * C + cj) * C + ck] = mb.add(lp, n, R * k, Gc * k, B * k, clamp8f(rockW * 255.0f));
                ch.gw.push_back(clamp8f(grassW * 255.0f));
            }

    if (mb.count() > 60000) { ch.gw.clear(); return; }

    auto vidAt = [&](int i, int j, int k) { return vid[((size_t)i * C + j) * C + k]; };
    for (int i = 1; i <= N; i++)
        for (int j = 1; j <= N; j++)
            for (int k = 1; k <= N; k++) {
                float d0 = Dat(i, j, k);
                for (int a = 0; a < 3; a++) {
                    float d1 = Dat(i + (a == 0), j + (a == 1), k + (a == 2));
                    bool in0 = d0 > 0, in1 = d1 > 0;
                    if (in0 == in1) continue;
                    int b = (a + 1) % 3, c = (a + 2) % 3;
                    int p[3] = {i, j, k};
                    int pb[3] = {i, j, k}, pc[3] = {i, j, k}, pbc[3] = {i, j, k};
                    pb[b]--; pc[c]--; pbc[b]--; pbc[c]--;
                    int v00 = vidAt(p[0], p[1], p[2]);
                    int v10 = vidAt(pb[0], pb[1], pb[2]);
                    int v11 = vidAt(pbc[0], pbc[1], pbc[2]);
                    int v01 = vidAt(pc[0], pc[1], pc[2]);
                    if (v00 < 0 || v10 < 0 || v11 < 0 || v01 < 0) continue;
                    if (in0) { mb.tri(v00, v10, v11); mb.tri(v00, v11, v01); }
                    else     { mb.tri(v00, v11, v10); mb.tri(v00, v01, v11); }
                }
            }
    ch.surfV = (int)ch.gw.size();
    ch.surfI = (int)mb.idx.size();

    // stromy a kameny
    if (mb.count() < 52000) {
        for (int gx = cx * 4; gx < cx * 4 + 4; gx++)
            for (int gz = cz * 4; gz < cz * 4 + 4; gz++) {
                if (mb.count() > 56000) break;
                TreeInfo t;
                if (treeAt(gx, gz, t) && !deadTrees.count(treeId(gx, gz))) {
                    int tcy = floorDiv((int)floorf(t.p.y / CELL), N);
                    if (tcy == cy && densityGrad(vadd(t.p, vec3(0, -0.2f, 0)), nullptr) > 0.1f)
                        addTree(mb, t, origin, gx, gz);
                }
                Vector3 rp; float rs;
                if (rockAt(gx, gz, rp, rs) && !deadTrees.count(treeId(gx, gz) ^ 0x5bd1e995ull)) {
                    int rcy = floorDiv((int)floorf(rp.y / CELL), N);
                    if (rcy == cy && densityGrad(vadd(rp, vec3(0, -0.1f, 0)), nullptr) > 0.1f) {
                        Vector3 c = vsub(rp, origin);
                        c.y += rs * 0.25f;
                        addBlob(mb, c, vec3(rs, rs * 0.6f, rs * 0.9f), {118, 114, 108, 255}, 190, gx * 7 + gz, 0.35f);
                    }
                }
            }
    }

    if (mb.count() == 0 || mb.idx.empty()) { ch.gw.clear(); ch.surfV = ch.surfI = 0; return; }
    ch.mesh = makeMesh(mb);
    ch.has = true;
}

// ---------------------------------------------------------------- Tráva (stébla)
// Stébla se generují z trojúhelníků povrchu, jen poblíž hráče.
static void buildGrass(int cx, int cy, int cz, Chunk &ch) {
    ch.grassDone = true;
    if (!ch.has || ch.surfI < 3 || (int)ch.gw.size() < ch.surfV) return;
    const float *V = ch.mesh.vertices;
    const float *Nn = ch.mesh.normals;
    const unsigned char *Co = ch.mesh.colors;
    const unsigned short *I = ch.mesh.indices;

    std::vector<float> pos, nrm;
    std::vector<unsigned char> col;
    pos.reserve(60000); nrm.reserve(60000); col.reserve(80000);
    auto push = [&](Vector3 p, Vector3 collapse, float r, float g, float b, float tip) {
        pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
        nrm.push_back(collapse.x); nrm.push_back(collapse.y); nrm.push_back(collapse.z);
        col.push_back(clamp8f(r)); col.push_back(clamp8f(g)); col.push_back(clamp8f(b)); col.push_back(clamp8f(tip * 255.0f));
    };

    int salt = cx * 73856093 ^ cy * 19349663 ^ cz * 83492791;
    int blades = 0;
    const int MAXB = 9000;
    int tris = ch.surfI / 3;
    for (int t = 0; t < tris && blades < MAXB; t++) {
        int i0 = I[t * 3], i1 = I[t * 3 + 1], i2 = I[t * 3 + 2];
        if (i0 >= ch.surfV || i1 >= ch.surfV || i2 >= ch.surfV) continue;
        float g = (ch.gw[i0] + ch.gw[i1] + ch.gw[i2]) / (3.0f * 255.0f);
        if (g < 0.40f) continue;
        float ny = (Nn[i0 * 3 + 1] + Nn[i1 * 3 + 1] + Nn[i2 * 3 + 1]) / 3.0f;
        if (ny < 0.72f) continue;
        Vector3 a = {V[i0 * 3], V[i0 * 3 + 1], V[i0 * 3 + 2]};
        Vector3 b = {V[i1 * 3], V[i1 * 3 + 1], V[i1 * 3 + 2]};
        Vector3 c = {V[i2 * 3], V[i2 * 3 + 1], V[i2 * 3 + 2]};
        float area = 0.5f * vlen(vcross(vsub(b, a), vsub(c, a)));
        float expect = area * GRASS_DENSITY * clamp01((g - 0.30f) * 1.8f);
        int n = (int)expect;
        if (hash2(salt + t, 977) < expect - n) n++;
        for (int k = 0; k < n && blades < MAXB; k++) {
            int sd = salt + t * 17 + k;
            float u = hash2(sd, 11), v = hash2(sd, 23);
            if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
            float w0 = 1.0f - u - v;
            Vector3 r = {a.x * w0 + b.x * u + c.x * v, a.y * w0 + b.y * u + c.y * v, a.z * w0 + b.z * u + c.z * v};
            float cr = (Co[i0 * 4] * w0 + Co[i1 * 4] * u + Co[i2 * 4] * v);
            float cg = (Co[i0 * 4 + 1] * w0 + Co[i1 * 4 + 1] * u + Co[i2 * 4 + 1] * v);
            float cb = (Co[i0 * 4 + 2] * w0 + Co[i1 * 4 + 2] * u + Co[i2 * 4 + 2] * v);
            float br = 0.85f + 0.35f * hash2(sd, 31);
            float h = 0.22f + 0.40f * hash2(sd, 41);
            float wd = 0.030f + 0.030f * hash2(sd, 53);
            float th = 6.2831853f * hash2(sd, 61);
            float lf = 0.04f + 0.14f * hash2(sd, 71);
            float lt = 6.2831853f * hash2(sd, 83);
            Vector3 side = {cosf(th) * wd, 0, sinf(th) * wd};
            Vector3 lean = {cosf(lt) * lf, 0, sinf(lt) * lf};
            Vector3 up = {0, h, 0};
            Vector3 A = vsub(r, side), B = vadd(r, side);
            Vector3 mid = vadd(vadd(r, vmul(up, 0.5f)), vmul(lean, 0.45f));
            Vector3 Cc = vsub(mid, vmul(side, 0.7f)), Dd = vadd(mid, vmul(side, 0.7f));
            Vector3 T = vadd(vadd(r, up), lean);
            float r0 = cr * br * 0.55f, g0 = cg * br * 0.55f, b0 = cb * br * 0.55f;
            float r1 = cr * br * 0.95f, g1 = cg * br * 0.95f, b1 = cb * br * 0.95f;
            float r2 = cr * br * 1.30f + 12, g2 = cg * br * 1.30f + 14, b2 = cb * br * 1.20f;
            Vector3 z = {0, 0, 0};
            Vector3 cm = vsub(r, mid), ct = vsub(r, T);
            // trojúhelníky (A,B,D) (A,D,C) (C,D,T)
            push(A, z, r0, g0, b0, 0.0f); push(B, z, r0, g0, b0, 0.0f); push(Dd, cm, r1, g1, b1, 0.45f);
            push(A, z, r0, g0, b0, 0.0f); push(Dd, cm, r1, g1, b1, 0.45f); push(Cc, cm, r1, g1, b1, 0.45f);
            push(Cc, cm, r1, g1, b1, 0.45f); push(Dd, cm, r1, g1, b1, 0.45f); push(T, ct, r2, g2, b2, 1.0f);
            blades++;
        }
    }
    if (blades == 0) return;

    Mesh m = {};
    m.vertexCount = (int)(pos.size() / 3);
    m.triangleCount = m.vertexCount / 3;
    m.vertices = (float *)MemAlloc((unsigned)(pos.size() * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned)(nrm.size() * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned)col.size());
    memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
    memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
    memcpy(m.colors, col.data(), col.size());
    UploadMesh(&m, false);
    ch.grass = m;
    ch.hasGrass = true;
}

static void updateGrass(Vector3 pp, double budget) {
    struct GI { uint64_t key; float d; };
    static std::vector<GI> list;
    list.clear();
    for (auto &kv : chunkMap) {
        Chunk &ch = kv.second;
        int cx, cy, cz;
        decodeKey(kv.first, cx, cy, cz);
        float dx = (cx + 0.5f) * CHUNK_M - pp.x, dz = (cz + 0.5f) * CHUNK_M - pp.z;
        float d = sqrtf(dx * dx + dz * dz);
        if (ch.hasGrass && d > GRASS_UNLOAD) { freeGrass(ch); continue; }
        if (ch.has && !ch.grassDone && d < GRASS_BUILD) list.push_back({kv.first, d});
    }
    std::sort(list.begin(), list.end(), [](const GI &a, const GI &b) { return a.d < b.d; });
    double t0 = GetTime();
    size_t done = 0;
    for (const GI &g : list) {
        if (done > 0 && GetTime() - t0 > budget) break;
        auto it = chunkMap.find(g.key);
        if (it == chunkMap.end()) continue;
        int cx, cy, cz;
        decodeKey(g.key, cx, cy, cz);
        buildGrass(cx, cy, cz, it->second);
        done++;
    }
}

// ---------------------------------------------------------------- Streamování chunků
static int updateStreaming(Vector3 pp, double budget, int radius, bool unload) {
    int pcx = (int)floorf(pp.x / CHUNK_M), pcz = (int)floorf(pp.z / CHUNK_M);
    struct Need { int x, y, z; float dist; bool dirty; };
    static std::vector<Need> needs;
    needs.clear();
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dz * dz > radius * radius) continue;
            int cx = pcx + dx, cz = pcz + dz;
            Column &col = getColumn(cx, cz);
            int lo = floorDiv((int)floorf((col.hmin - 1.0f) / CELL) - 1, N);
            int hi = floorDiv((int)ceilf((col.hmax + 1.0f) / CELL) + 1, N);
            for (int cy = lo; cy <= hi; cy++) {
                auto it = chunkMap.find(ckey(cx, cy, cz));
                if (it == chunkMap.end()) needs.push_back({cx, cy, cz, (float)(dx * dx + dz * dz), false});
                else if (it->second.dirty) needs.push_back({cx, cy, cz, (float)(dx * dx + dz * dz), true});
            }
        }
    for (auto &kv : editedChunks) {
        IVec3 c = kv.second;
        int dx = c.x - pcx, dz = c.z - pcz;
        if (dx * dx + dz * dz > (radius + 1) * (radius + 1)) continue;
        auto it = chunkMap.find(kv.first);
        if (it == chunkMap.end()) needs.push_back({c.x, c.y, c.z, (float)(dx * dx + dz * dz), false});
        else if (it->second.dirty) needs.push_back({c.x, c.y, c.z, (float)(dx * dx + dz * dz), true});
    }
    std::sort(needs.begin(), needs.end(), [](const Need &a, const Need &b) {
        if (a.dirty != b.dirty) return a.dirty;
        return a.dist < b.dist;
    });

    double t0 = GetTime();
    size_t built = 0;
    for (const Need &n : needs) {
        if (built > 0 && GetTime() - t0 > budget) break;
        uint64_t key = ckey(n.x, n.y, n.z);
        auto it = chunkMap.find(key);
        if (it != chunkMap.end() && !it->second.dirty) { built++; continue; }
        Chunk &ch = chunkMap[key];
        buildChunk(n.x, n.y, n.z, ch);
        built++;
    }

    if (unload) {
        for (auto it = chunkMap.begin(); it != chunkMap.end();) {
            int cx, cy, cz;
            decodeKey(it->first, cx, cy, cz);
            int dx = cx - pcx, dz = cz - pcz;
            if (dx * dx + dz * dz > (VIEW_R + 2) * (VIEW_R + 2)) {
                if (it->second.has) UnloadMesh(it->second.mesh);
                freeGrass(it->second);
                it = chunkMap.erase(it);
            } else ++it;
        }
        for (auto it = columns.begin(); it != columns.end();) {
            int cx, cy, cz;
            decodeKey(it->first, cx, cy, cz);
            int dx = cx - pcx, dz = cz - pcz;
            if (dx * dx + dz * dz > (VIEW_R + 4) * (VIEW_R + 4)) it = columns.erase(it);
            else ++it;
        }
    }
    return (int)needs.size() - (int)std::min(built, needs.size());
}

static void markChunkDirty(int cx, int cy, int cz) {
    auto it = chunkMap.find(ckey(cx, cy, cz));
    if (it != chunkMap.end()) it->second.dirty = true;
}

// ---------------------------------------------------------------- Úpravy terénu
static void editSphere(Vector3 c, float r, bool add) {
    float m = r + CELL * 1.5f;
    int x0 = (int)floorf((c.x - m) / CELL), x1 = (int)ceilf((c.x + m) / CELL);
    int y0 = (int)floorf((c.y - m) / CELL), y1 = (int)ceilf((c.y + m) / CELL);
    int z0 = (int)floorf((c.z - m) / CELL), z1 = (int)ceilf((c.z + m) / CELL);
    int mn[3] = {INT_MAX, INT_MAX, INT_MAX}, mx[3] = {INT_MIN, INT_MIN, INT_MIN};
    bool any = false;
    for (int sx = x0; sx <= x1; sx++)
        for (int sy = y0; sy <= y1; sy++)
            for (int sz = z0; sz <= z1; sz++) {
                Vector3 p = {sx * CELL, sy * CELL, sz * CELL};
                float dist = vlen(vsub(p, c));
                if (dist > m) continue;
                float cur = sampleD(sx, sy, sz);
                float nd = add ? fmaxf(cur, r - dist) : fminf(cur, dist - r);
                if (fabsf(nd - cur) < 1e-4f) continue;
                edits[skey(sx, sy, sz)] = nd;
                mn[0] = std::min(mn[0], sx); mx[0] = std::max(mx[0], sx);
                mn[1] = std::min(mn[1], sy); mx[1] = std::max(mx[1], sy);
                mn[2] = std::min(mn[2], sz); mx[2] = std::max(mx[2], sz);
                any = true;
            }
    if (!any) return;
    for (int cx = floorDiv(mn[0] - 1, N); cx <= floorDiv(mx[0] + 1, N); cx++)
        for (int cy = floorDiv(mn[1] - 1, N); cy <= floorDiv(mx[1] + 1, N); cy++)
            for (int cz = floorDiv(mn[2] - 1, N); cz <= floorDiv(mx[2] + 1, N); cz++) {
                editedChunks.emplace(ckey(cx, cy, cz), IVec3{cx, cy, cz});
                markChunkDirty(cx, cy, cz);
            }
}

// ---------------------------------------------------------------- Částice a pracovní stoly
struct Particle {
    Vector3 p, v;
    float life, maxLife, size;
    Color col;
    uint8_t kind;   // 0 trosky, 1 kouř, 2 oheň, 3 jiskra
    bool still;
};
static std::vector<Particle> parts;
static float gShake = 0;
static Vector3 gEye = {0, 0, 0};
static float gDamage = 0;                 // poškození hráče od výbuchů (zpracuje main)
static Vector3 gKnock = {0, 0, 0};        // odhození hráče výbuchem

struct Table { Vector3 p; float yaw; int id; };
static std::vector<Table> tables;
static int tableCounter = 0;

static void addPart(Vector3 p, Vector3 v, float life, float size, Color c, uint8_t kind) {
    if (parts.size() >= MAX_PARTS) return;
    parts.push_back({p, v, life, life, size, c, kind, false});
}

static bool particleCollide(Vector3 &p, Vector3 &v, float dt, float rest, float fric) {
    Vector3 np = vadd(p, vmul(v, dt));
    Vector3 g;
    float d = densityGrad(np, &g);
    if (d <= 0) { p = np; return false; }
    float gl = vlen(g);
    Vector3 n = gl > 1e-5f ? vmul(g, -1.0f / gl) : vec3(0, 1, 0);
    float vn = vdot(v, n);
    if (vn < 0) {
        Vector3 vt = vsub(v, vmul(n, vn));
        v = vadd(vmul(vt, fric), vmul(n, -vn * rest));
    }
    float push = gl > 1e-5f ? d / gl : 0.0f;
    p = vadd(np, vmul(n, push + 0.02f));
    return true;
}

static void explosionFx(Vector3 c, float R, float amt) {
    int nf = (int)(26 * amt), ns = (int)(30 * amt), nsp = (int)(50 * amt);
    for (int i = 0; i < nf; i++) {
        Vector3 d = vnorm(vec3(frand(-1, 1), frand(-0.3f, 1), frand(-1, 1)));
        addPart(vadd(c, vmul(d, R * 0.25f)), vmul(d, frand(0.5f, 1.6f) * R * 1.8f), frand(0.3f, 0.75f),
                R * frand(1.0f, 1.8f), {255, 170, 60, 255}, 2);
    }
    for (int i = 0; i < ns; i++) {
        Vector3 d = vnorm(vec3(frand(-1, 1), frand(-0.2f, 1), frand(-1, 1)));
        float g = frand(45, 100);
        addPart(vadd(c, vmul(d, R * 0.4f)), vadd(vmul(d, frand(0.2f, 1.0f) * R), vec3(0, R * frand(0.5f, 1.4f), 0)),
                frand(2.0f, 4.5f), R * frand(1.2f, 2.4f), {clamp8f(g), clamp8f(g), clamp8f(g), 255}, 1);
    }
    for (int i = 0; i < nsp; i++) {
        Vector3 d = vnorm(vec3(frand(-1, 1), frand(-0.2f, 1), frand(-1, 1)));
        addPart(c, vmul(d, frand(10, 34)), frand(0.6f, 1.7f), 0.12f, {255, 205, 90, 255}, 3);
    }
}

static void spawnRocks(Vector3 c, float R, int count, float power) {
    for (int tries = 0, made = 0; tries < count * 3 && made < count; tries++) {
        Vector3 d = vnorm(vec3(frand(-1, 1), frand(-1, 1), frand(-1, 1)));
        Vector3 p = vadd(c, vmul(d, R * cbrtf(frand(0.05f, 1.0f))));
        if (densityGrad(p, nullptr) <= 0) continue;
        float r, g, b, rw, gw;
        groundColor(p.x, p.y, p.z, 0.7f, terrainH(p.x, p.z), r, g, b, rw, gw);
        Vector3 v = vadd(vmul(vnorm(vadd(vsub(p, c), vec3(0, 0.5f, 0))), power * frand(0.3f, 1.0f)),
                         vec3(frand(-1.5f, 1.5f), frand(0, 3), frand(-1.5f, 1.5f)));
        addPart(p, v, frand(10, 25), frand(0.12f, 0.45f), {clamp8f(r * 0.85f), clamp8f(g * 0.85f), clamp8f(b * 0.85f), 255}, 0);
        made++;
    }
}

static void killTrees(Vector3 c, float R) {
    int g0x = (int)floorf((c.x - R - 4) / 4.0f), g1x = (int)floorf((c.x + R + 4) / 4.0f);
    int g0z = (int)floorf((c.z - R - 4) / 4.0f), g1z = (int)floorf((c.z + R + 4) / 4.0f);
    for (int gx = g0x; gx <= g1x; gx++)
        for (int gz = g0z; gz <= g1z; gz++) {
            TreeInfo t;
            if (treeAt(gx, gz, t) && !deadTrees.count(treeId(gx, gz))) {
                Vector3 mid = vadd(t.p, vec3(0, t.h * 0.5f, 0));
                if (vlen(vsub(mid, c)) < R + t.rad) {
                    deadTrees.insert(treeId(gx, gz));
                    for (int i = 0; i < 14; i++)
                        addPart(vadd(mid, vec3(frand(-1, 1), frand(-t.h * 0.4f, t.h * 0.4f), frand(-1, 1))),
                                vadd(vmul(vnorm(vsub(mid, c)), frand(5, 14)), vec3(0, frand(2, 6), 0)),
                                frand(10, 20), frand(0.15f, 0.4f), {80, 58, 40, 255}, 0);
                    markChunkDirty(floorDiv((int)floorf(t.p.x / CELL), N), floorDiv((int)floorf(t.p.y / CELL), N),
                                   floorDiv((int)floorf(t.p.z / CELL), N));
                }
            }
            Vector3 rp; float rs;
            if (rockAt(gx, gz, rp, rs) && !deadTrees.count(treeId(gx, gz) ^ 0x5bd1e995ull) && vlen(vsub(rp, c)) < R + rs) {
                deadTrees.insert(treeId(gx, gz) ^ 0x5bd1e995ull);
                markChunkDirty(floorDiv((int)floorf(rp.x / CELL), N), floorDiv((int)floorf(rp.y / CELL), N),
                               floorDiv((int)floorf(rp.z / CELL), N));
            }
        }
}

static void explode(Vector3 c, float R, float power, float fx) {
    spawnRocks(c, R, (int)fminf(260.0f, R * R * 7.0f), power);
    killTrees(c, R);
    editSphere(c, R, false);
    // pracovní stoly v dosahu výbuchu se rozbijí
    for (size_t i = 0; i < tables.size();) {
        if (vlen(vsub(vadd(tables[i].p, vec3(0, 0.5f, 0)), c)) < R + 0.9f) {
            for (int k = 0; k < 20; k++)
                addPart(vadd(tables[i].p, vec3(frand(-0.6f, 0.6f), frand(0.2f, 1.0f), frand(-0.4f, 0.4f))),
                        vec3(frand(-6, 6), frand(2, 9), frand(-6, 6)), frand(10, 18), frand(0.1f, 0.3f), {130, 92, 56, 255}, 0);
            tables.erase(tables.begin() + i);
        } else i++;
    }
    if (fx > 0) {
        explosionFx(c, R, fx);
        float dist = vlen(vsub(gEye, c));
        float f = 1.0f - dist / (R * 14.0f);
        if (f > 0) gShake = fmaxf(gShake, f * R * 0.07f * fx);
    }
    // zranění a odhození hráče
    Vector3 pc = vadd(gEye, vec3(0, -0.7f, 0));
    float pd = vlen(vsub(pc, c)), reach = R * 1.7f;
    if (pd < reach) {
        float f = 1.0f - pd / reach;
        gDamage += 24.0f * f * f;
        gKnock = vadd(gKnock, vmul(vnorm(vadd(vsub(pc, c), vec3(0, 0.6f, 0))), 16.0f * f));
    }
}

static void updateParticles(float dt) {
    for (size_t i = 0; i < parts.size();) {
        Particle &q = parts[i];
        q.life -= dt;
        if (q.life <= 0) { parts[i] = parts.back(); parts.pop_back(); continue; }
        if (q.kind == 0 || q.kind == 3) {
            if (!q.still) {
                q.v.y -= G * dt;
                bool hit = particleCollide(q.p, q.v, dt, 0.35f, 0.7f);
                if (hit && vlen(q.v) < 1.0f) { q.v = vec3(0, 0, 0); q.still = true; }
            }
        } else if (q.kind == 1) {
            q.p = vadd(q.p, vmul(q.v, dt));
            q.v = vmul(q.v, 1.0f - 1.4f * dt);
            q.v.y += 0.9f * dt;
        } else {
            q.p = vadd(q.p, vmul(q.v, dt));
            q.v = vmul(q.v, 1.0f - 3.0f * dt);
        }
        i++;
    }
}

// ---------------------------------------------------------------- Projektily
struct Rocket { Vector3 p, v; float life; };
struct Grenade { Vector3 p, v; float fuse; float R, power; bool dyn; };
static std::vector<Rocket> rockets;
static std::vector<Grenade> grenades;

static void updateProjectiles(float dt) {
    for (size_t i = 0; i < rockets.size();) {
        Rocket &r = rockets[i];
        r.life -= dt;
        bool boom = r.life <= 0;
        int n = std::max(1, (int)ceilf(vlen(r.v) * dt / 0.4f));
        Vector3 sv = vmul(r.v, dt / n);
        for (int s = 0; s < n && !boom; s++) {
            r.p = vadd(r.p, sv);
            if (densityGrad(r.p, nullptr) > 0) boom = true;
        }
        if (boom) {
            explode(vsub(r.p, vmul(vnorm(r.v), 0.6f)), 5.0f, 22.0f, 1.0f);
            rockets[i] = rockets.back();
            rockets.pop_back();
            continue;
        }
        addPart(r.p, vec3(frand(-0.5f, 0.5f), frand(-0.5f, 0.5f), frand(-0.5f, 0.5f)), frand(0.8f, 1.5f), 0.8f, {170, 170, 170, 255}, 1);
        addPart(r.p, vec3(0, 0, 0), 0.15f, 0.6f, {255, 170, 70, 255}, 2);
        i++;
    }
    for (size_t i = 0; i < grenades.size();) {
        Grenade &g = grenades[i];
        g.fuse -= dt;
        g.v.y -= G * dt;
        for (int s = 0; s < 2; s++) particleCollide(g.p, g.v, dt * 0.5f, 0.45f, 0.75f);
        if (g.dyn && frand(0, 1) < 0.5f)
            addPart(vadd(g.p, vec3(0, 0.2f, 0)), vec3(frand(-0.5f, 0.5f), frand(1, 2), frand(-0.5f, 0.5f)), 0.25f, 0.1f, {255, 200, 90, 255}, 3);
        if (g.fuse <= 0) {
            explode(g.p, g.R, g.power, 1.2f);
            grenades[i] = grenades.back();
            grenades.pop_back();
            continue;
        }
        i++;
    }
}

// ---------------------------------------------------------------- Hráč: kolize a paprsky
static bool resolveSphere(Vector3 &c, float r, Vector3 &vel, float &groundNy) {
    bool hit = false;
    for (int it = 0; it < 3; it++) {
        Vector3 g;
        float d = densityGrad(c, &g);
        float gl = vlen(g);
        if (gl < 1e-4f) break;
        float pen = r + d / gl;       // sd = -d/|g|
        if (pen <= 0) break;
        Vector3 n = vmul(g, -1.0f / gl);
        c = vadd(c, vmul(n, pen));
        float vn = vdot(vel, n);
        if (vn < 0) vel = vsub(vel, vmul(n, vn));
        groundNy = fmaxf(groundNy, n.y);
        hit = true;
    }
    return hit;
}

static bool rayTerrain(Vector3 o, Vector3 d, float maxD, Vector3 &hit) {
    float prev = 0.0f;
    for (float t = 0.0f; t < maxD; t += 0.2f) {
        if (densityGrad(vadd(o, vmul(d, t)), nullptr) > 0) {
            float a = prev, b = t;
            for (int i = 0; i < 8; i++) {
                float m = 0.5f * (a + b);
                if (densityGrad(vadd(o, vmul(d, m)), nullptr) > 0) b = m; else a = m;
            }
            hit = vadd(o, vmul(d, b));
            return true;
        }
        prev = t;
    }
    return false;
}

// paprsek proti stromům (kmen + koruna)
static bool rayTree(Vector3 o, Vector3 d, float maxD, int &outGx, int &outGz, TreeInfo &outT, Vector3 &hitP) {
    Vector3 e = vadd(o, vmul(d, maxD));
    int gx0 = (int)floorf((fminf(o.x, e.x) - 4.0f) / 4.0f), gx1 = (int)floorf((fmaxf(o.x, e.x) + 4.0f) / 4.0f);
    int gz0 = (int)floorf((fminf(o.z, e.z) - 4.0f) / 4.0f), gz1 = (int)floorf((fmaxf(o.z, e.z) + 4.0f) / 4.0f);
    struct Cand { int gx, gz; TreeInfo t; };
    static std::vector<Cand> cands;
    cands.clear();
    for (int gx = gx0; gx <= gx1; gx++)
        for (int gz = gz0; gz <= gz1; gz++) {
            TreeInfo t;
            if (treeAt(gx, gz, t) && !deadTrees.count(treeId(gx, gz))) cands.push_back({gx, gz, t});
        }
    if (cands.empty()) return false;
    for (float s = 0.0f; s < maxD; s += 0.2f) {
        Vector3 p = vadd(o, vmul(d, s));
        for (const Cand &cd : cands) {
            float dy = p.y - cd.t.p.y;
            if (dy < 0 || dy > cd.t.h * 1.1f) continue;
            float dx = p.x - cd.t.p.x, dz = p.z - cd.t.p.z;
            float dxz = sqrtf(dx * dx + dz * dz);
            float hf = dy / cd.t.h, r;
            if (cd.t.type == 0) r = hf < 0.55f ? 0.45f : cd.t.rad * 0.85f;
            else r = hf < 0.25f ? 0.40f : cd.t.rad * (1.1f - 0.8f * (hf - 0.22f) / 0.78f);
            if (dxz < r) { outGx = cd.gx; outGz = cd.gz; outT = cd.t; hitP = p; return true; }
        }
    }
    return false;
}

static bool aimWater(Vector3 eye, Vector3 look, float maxD) {
    if (eye.y < SEA + 0.2f) return true;
    if (look.y > -0.02f) return false;
    float t = (SEA - eye.y) / look.y;
    if (t < 0 || t > maxD) return false;
    Vector3 p = vadd(eye, vmul(look, t));
    if (densityGrad(p, nullptr) > 0) return false;
    for (float s = 0.3f; s < t; s += 0.4f)
        if (densityGrad(vadd(eye, vmul(look, s)), nullptr) > 0) return false;
    return terrainH(p.x, p.z) < SEA - 0.05f;
}

// index stolu, na který se hráč dívá (nebo -1)
static int aimTable(Vector3 eye, Vector3 look, float maxD) {
    int best = -1;
    float bestT = maxD;
    for (size_t i = 0; i < tables.size(); i++) {
        Vector3 c = vadd(tables[i].p, vec3(0, 0.6f, 0));
        float tt = vdot(vsub(c, eye), look);
        if (tt < 0 || tt > bestT) continue;
        Vector3 cp = vadd(eye, vmul(look, tt));
        if (vlen(vsub(cp, c)) > 0.95f) continue;
        bool blocked = false;
        for (float s = 0.4f; s < tt - 0.6f; s += 0.4f)
            if (densityGrad(vadd(eye, vmul(look, s)), nullptr) > 0) { blocked = true; break; }
        if (blocked) continue;
        best = (int)i;
        bestT = tt;
    }
    return best;
}
static int findTable(int id) {
    for (size_t i = 0; i < tables.size(); i++) if (tables[i].id == id) return (int)i;
    return -1;
}

// ---------------------------------------------------------------- Survival: věci a recepty
enum Item { I_HAMMER = 0, I_STONE, I_WOOD, I_APPLE, I_GRENADE, I_ROCKET, I_DYNAMITE, I_TABLE, I_COUNT };
static const char *ITEM_NAME[I_COUNT] = {"Kladivko", "Kamen", "Drevo", "Jablko", "Granat", "Raketa", "Dynamit", "Stul"};
static const char *ITEM_HINT[I_COUNT] = {
    "LMB: kopat / kacet.  U stolu: bit a vyrabet", "PMB: polozit kamen", "Surovina na vyrobu", "PMB: snist",
    "PMB: hodit", "PMB: vystrelit", "PMB: hodit (velky vybuch)", "PMB: postavit pracovni stul"};
static int inv[I_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};

struct Recipe { const char *name; int out; int stone, wood; };
static const Recipe HAND_RECIPES[2] = {{"Pracovni stul", I_TABLE, 0, 8}, {"Kladivko", I_HAMMER, 2, 2}};
static const Recipe TABLE_RECIPES[3] = {{"Granat", I_GRENADE, 2, 1}, {"Raketa", I_ROCKET, 4, 2}, {"Dynamit", I_DYNAMITE, 6, 3}};
static bool canCraft(const Recipe &r) { return inv[I_STONE] >= r.stone && inv[I_WOOD] >= r.wood; }

struct Toast { char text[64]; float t; };
static std::vector<Toast> toasts;
static void toast(const char *fmt, ...) {
    Toast t;
    va_list a;
    va_start(a, fmt);
    vsnprintf(t.text, sizeof(t.text), fmt, a);
    va_end(a);
    t.t = 3.0f;
    toasts.push_back(t);
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}
static void addItem(int it, int n) {
    inv[it] += n;
    toast("+%d %s", n, ITEM_NAME[it]);
}

static void chopTree(int gx, int gz, const TreeInfo &t, Vector3 hitP) {
    uint64_t id = treeId(gx, gz);
    int &h = treeHits[id];
    h++;
    for (int i = 0; i < 5; i++)
        addPart(hitP, vec3(frand(-2, 2), frand(1, 4), frand(-2, 2)), frand(1.5f, 3.0f), frand(0.08f, 0.2f), {110, 80, 50, 255}, 0);
    if (h >= 4) {
        deadTrees.insert(id);
        treeHits.erase(id);
        int wood = 4 + GetRandomValue(0, 3);
        addItem(I_WOOD, wood);
        if (t.type == 0 && frand(0, 1) < 0.65f) addItem(I_APPLE, 1 + GetRandomValue(0, 2));
        else if (t.type == 1 && frand(0, 1) < 0.15f) addItem(I_APPLE, 1);
        Vector3 mid = vadd(t.p, vec3(0, t.h * 0.5f, 0));
        for (int i = 0; i < 16; i++)
            addPart(vadd(mid, vec3(frand(-1, 1), frand(-t.h * 0.4f, t.h * 0.4f), frand(-1, 1))),
                    vec3(frand(-3, 3), frand(1, 5), frand(-3, 3)), frand(8, 16), frand(0.12f, 0.35f), {80, 58, 40, 255}, 0);
        markChunkDirty(floorDiv((int)floorf(t.p.x / CELL), N), floorDiv((int)floorf(t.p.y / CELL), N),
                       floorDiv((int)floorf(t.p.z / CELL), N));
    }
}

// ---------------------------------------------------------------- Obloha / denní cyklus
// tod = hodina 0..24;  light = aktivní zdroj světla (slunce přes den, měsíc v noci)
static void skyParams(float tod, Vector3 &lightDir, Vector3 &lightCol, Vector3 &zen, Vector3 &hor, float &nightAmt) {
    float ang = (tod - 6.0f) / 12.0f * PI;
    Vector3 sun = vnorm(vec3(cosf(ang) * 0.85f, sinf(ang), 0.42f));
    float e = sun.y;
    float t = smoothstepf(0.04f, 0.40f, e);
    float dayAmt = smoothstepf(-0.02f, 0.10f, e);
    Vector3 sunCol = vmul(vlerp(vec3(1.00f, 0.46f, 0.20f), vec3(1.0f, 0.93f, 0.82f), t),
                          (0.35f + 0.65f * smoothstepf(0.0f, 0.25f, e)) * dayAmt);
    float moonAmt = smoothstepf(-0.02f, -0.25f, e);
    Vector3 moonCol = vmul(vec3(0.10f, 0.13f, 0.22f), moonAmt);
    if (e > -0.02f) { lightDir = sun; lightCol = sunCol; }
    else { lightDir = vmul(sun, -1.0f); lightCol = moonCol; }

    Vector3 zenDay = vlerp(vec3(0.10f, 0.13f, 0.28f), vec3(0.13f, 0.30f, 0.70f), t);
    Vector3 horDay = vlerp(vec3(0.95f, 0.50f, 0.28f), vec3(0.62f, 0.78f, 0.98f), t);
    float nf = smoothstepf(0.02f, -0.22f, e);
    zen = vlerp(zenDay, vec3(0.010f, 0.018f, 0.045f), nf);
    hor = vlerp(horDay, vec3(0.025f, 0.040f, 0.085f), nf);
    nightAmt = nf;
}

// ---------------------------------------------------------------- Shadery
static const char *kVertMesh = R"(
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char *kVertSimple = R"(
in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragPos;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// tráva: normála nese vektor "špička -> kořen" (zmenšování stébel v dálce), alfa = pohyblivost ve větru
static const char *kVertGrass = R"(
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 viewPos;
uniform float time;
out vec3 fragPos;
out vec4 fragColor;
void main() {
    vec3 wp0 = vec3(matModel * vec4(vertexPosition, 1.0));
    float tip = vertexColor.a;
    float d = length(wp0 - viewPos);
    float fade = 1.0 - smoothstep(GRASS_A, GRASS_B, d);
    vec3 local = vertexPosition + vertexNormal * (1.0 - fade);
    float ph = wp0.x * 0.35 + wp0.z * 0.27;
    float sway = sin(time * 1.9 + ph) * 0.55 + sin(time * 3.7 + ph * 2.3) * 0.25;
    local.x += sway * 0.11 * tip * fade;
    local.z += cos(time * 1.3 + ph * 1.1) * 0.07 * tip * fade;
    fragPos = vec3(matModel * vec4(local, 1.0));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(local, 1.0);
}
)";

static const char *kCommon = R"(
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 skyZenith;
uniform vec3 skyHorizon;
vec3 skyBase(vec3 d) {
    float h = clamp(d.y, 0.0, 1.0);
    vec3 col = mix(skyHorizon, skyZenith, pow(h, 0.5));
    float sd = max(dot(d, sunDir), 0.0);
    col += sunColor * (pow(sd, 6.0) * 0.20 + pow(sd, 40.0) * 0.25);
    if (d.y < 0.0) col = mix(col, skyHorizon * 0.8, clamp(-d.y * 4.0, 0.0, 1.0));
    return col;
}
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 finishColor(vec3 c) {
    c = aces(c);
    c = pow(c, vec3(1.0 / 2.2));
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.10);
    c = (c - 0.5) * 1.05 + 0.5;
    return clamp(c, 0.0, 1.0);
}
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x),
                   mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x),
                   mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm3(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; }
    return s;
}
)";

static const char *kFragScene = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec3 viewPos;
uniform vec3 groundAmb;
out vec4 finalColor;

void main() {
    vec3 V = viewPos - fragPos;
    float dist = length(V);
    V /= dist;
    vec3 n = normalize(fragNormal);
    float rock = fragColor.a;

#ifdef LOWQ
    vec3 albedo = pow(fragColor.rgb, vec3(2.2));
    albedo *= 0.78 + 0.44 * vnoise(fragPos * 0.9);
#else
    float detailFade = 1.0 - smoothstep(25.0, 70.0, dist);
    vec3 pp = fragPos * 2.7;
    vec3 pert = vec3(vnoise(pp), vnoise(pp + 17.0), vnoise(pp + 41.0)) - 0.5;
    n = normalize(n + pert * (0.45 + 0.4 * rock) * detailFade);

    vec3 albedo = pow(fragColor.rgb, vec3(2.2));
    float large = fbm3(fragPos * 0.9);
    float micro = vnoise(fragPos * 9.0);
    albedo *= (0.72 + 0.56 * large) * mix(1.0, 0.85 + 0.3 * micro, detailFade);
    float strata = sin(fragPos.y * 2.4 + fbm3(fragPos * 0.35) * 7.0);
    albedo *= 1.0 + rock * 0.14 * strata;
#endif

    float ndl = max(dot(n, sunDir), 0.0);
    vec3 direct = sunColor * 3.1 * ndl;
    vec3 hemi = mix(groundAmb, skyZenith * 0.55 + skyHorizon * 0.45, n.y * 0.5 + 0.5) + vec3(0.004, 0.005, 0.009);
    vec3 col = albedo * (direct + hemi * 0.85);

    vec3 H = normalize(V + sunDir);
    float spec = pow(max(dot(n, H), 0.0), 48.0) * (0.05 + 0.25 * rock * rock) * ndl;
    col += sunColor * spec * 3.1;
    col += skyHorizon * pow(1.0 - max(dot(n, V), 0.0), 4.0) * 0.06;

    float haze = 1.0 - exp(-dist * 0.0032);
    float edgeFog = smoothstep(FOG_A, FOG_B, dist);
    float fog = clamp(max(haze * 0.9, edgeFog), 0.0, 1.0);
    col = mix(col, skyBase(-V), fog);
    finalColor = vec4(finishColor(col), 1.0);
}
)";

static const char *kFragGrass = R"(
in vec3 fragPos;
in vec4 fragColor;
uniform vec3 viewPos;
uniform vec3 groundAmb;
out vec4 finalColor;

void main() {
    vec3 V = viewPos - fragPos;
    float dist = length(V);
    V /= dist;
    float tip = fragColor.a;
    vec3 albedo = pow(fragColor.rgb, vec3(2.2));
    float ndl = 0.45 + 0.55 * clamp(sunDir.y, 0.0, 1.0);
    vec3 direct = sunColor * 3.1 * ndl;
    float trans = pow(max(dot(-V, sunDir), 0.0), 4.0) * tip * 0.6;
    direct += sunColor * trans * 2.0;
    vec3 hemi = mix(groundAmb, skyZenith * 0.55 + skyHorizon * 0.45, 0.8) + vec3(0.004, 0.005, 0.009);
    vec3 col = albedo * (direct + hemi * 0.85);
    float haze = 1.0 - exp(-dist * 0.0032);
    float edgeFog = smoothstep(FOG_A, FOG_B, dist);
    float fog = clamp(max(haze * 0.9, edgeFog), 0.0, 1.0);
    col = mix(col, skyBase(-V), fog);
    finalColor = vec4(finishColor(col), 1.0);
}
)";

static const char *kFragWater = R"(
in vec3 fragPos;
uniform vec3 viewPos;
uniform float time;
out vec4 finalColor;

float hnoise2(vec2 p) { return vnoise(vec3(p, 0.0)); }

void main() {
    vec3 V = viewPos - fragPos;
    float dist = length(V);
    V /= dist;
    vec2 p = fragPos.xz;
    float t = time;
    vec2 w1 = vec2(hnoise2(p * 0.25 + vec2(t * 0.35, t * 0.2)), hnoise2(p * 0.25 + 31.0 - vec2(t * 0.3, t * 0.25))) - 0.5;
    vec2 w2 = vec2(hnoise2(p * 0.9 + vec2(-t * 0.6, t * 0.5)), hnoise2(p * 0.9 + 57.0 + vec2(t * 0.5, -t * 0.4))) - 0.5;
    vec3 n = normalize(vec3(w1.x * 0.5 + w2.x * 0.30, 1.0, w1.y * 0.5 + w2.y * 0.30));
    n = normalize(mix(vec3(0.0, 1.0, 0.0), n, 1.0 - smoothstep(60.0, 200.0, dist)));
    vec3 R = reflect(-V, n);
    R.y = abs(R.y);
    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 refl = skyBase(R);
    float sd = max(dot(R, sunDir), 0.0);
    refl += sunColor * (pow(sd, 300.0) * 8.0 + pow(sd, 30.0) * 0.25);
    vec3 deep = vec3(0.012, 0.06, 0.09) * (0.15 + 0.85 * clamp(sunColor.r + sunColor.g * 0.5, 0.0, 1.0)) + skyHorizon * 0.03;
    vec3 col = mix(deep, refl, clamp(fres + 0.08, 0.0, 1.0));
    float alpha = mix(0.78, 1.0, fres);
    float haze = 1.0 - exp(-dist * 0.0032);
    float edgeFog = smoothstep(FOG_A, FOG_B, dist);
    float fog = clamp(max(haze * 0.9, edgeFog), 0.0, 1.0);
    col = mix(col, skyBase(-V), fog);
    finalColor = vec4(finishColor(col), alpha);
}
)";

static const char *kVertSky = R"(
in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char *kFragSky = R"(
uniform vec3 camRight;
uniform vec3 camUp;
uniform vec3 camFwd;
uniform float tanFov;
uniform float aspect;
uniform vec2 resolution;
uniform float time;
uniform float nightAmt;
out vec4 finalColor;

float hnoise2(vec2 p) { return vnoise(vec3(p, 0.0)); }
float fbm2d(vec2 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < SKY_OCT; i++) { s += a * hnoise2(p); p *= 2.07; a *= 0.5; }
    return s;
}
void main() {
    vec2 uv = gl_FragCoord.xy / resolution * 2.0 - 1.0;
    vec3 d = normalize(camFwd + camRight * uv.x * tanFov * aspect + camUp * uv.y * tanFov);
    vec3 col = skyBase(d);
    float sd = dot(d, sunDir);
    col += sunColor * smoothstep(0.9995, 0.99985, sd) * 40.0;
    if (d.y > 0.0 && nightAmt > 0.01) {
        vec3 sp = d * 380.0;
        vec3 cell = floor(sp);
        float hs = hash13(cell);
        vec3 fr = fract(sp) - 0.5;
        float star = smoothstep(0.9965, 1.0, hs) * (1.0 - smoothstep(0.0, 0.45, length(fr)));
        star *= 0.5 + 0.5 * hash13(cell + 7.0);
        col += vec3(0.9, 0.95, 1.0) * star * nightAmt * smoothstep(0.0, 0.15, d.y) * 2.0;
    }
    if (d.y > 0.0) {
        vec2 q = d.xz / (d.y + 0.12) * 0.8 + vec2(time * 0.012, time * 0.004);
        float c = fbm2d(q);
        float cov = smoothstep(0.50, 0.78, c);
        float lit = 0.55 + 0.45 * clamp(sunDir.y + 0.3, 0.0, 1.0);
        vec3 cc = mix(vec3(0.45, 0.47, 0.55) * lit, sunColor * 1.2 + vec3(0.35) * lit, 1.0 - smoothstep(0.30, 0.55, c));
        cc *= 1.0 - 0.93 * nightAmt;
        col = mix(col, cc, cov * smoothstep(0.0, 0.14, d.y) * 0.92);
    }
    finalColor = vec4(finishColor(col), 1.0);
}
)";

struct CommonLocs { int sunDir, sunColor, zen, hor; };
static CommonLocs commonLocs(Shader s) {
    return {GetShaderLocation(s, "sunDir"), GetShaderLocation(s, "sunColor"),
            GetShaderLocation(s, "skyZenith"), GetShaderLocation(s, "skyHorizon")};
}
static void setCommon(Shader s, const CommonLocs &l, Vector3 sun, Vector3 sc, Vector3 zen, Vector3 hor) {
    SetShaderValue(s, l.sunDir, &sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, l.sunColor, &sc, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, l.zen, &zen, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, l.hor, &hor, SHADER_UNIFORM_VEC3);
}

// Převod GLSL 330 na GLSL ES 1.00 (OpenGL ES 2.0 na Androidu)
static std::string toEs100(const std::string &src, bool vertex) {
    std::string out;
    size_t pos = 0;
    while (pos <= src.size()) {
        size_t e = src.find('\n', pos);
        if (e == std::string::npos) e = src.size();
        std::string line = src.substr(pos, e - pos);
        pos = e + 1;
        if (line.compare(0, 3, "in ") == 0) line = std::string(vertex ? "attribute " : "varying ") + line.substr(3);
        else if (line.compare(0, 4, "out ") == 0) {
            if (vertex) line = "varying " + line.substr(4);
            else line.clear();                          // výstup fragment shaderu = gl_FragColor
        }
        out += line;
        out += '\n';
        if (e >= src.size()) break;
    }
    if (!vertex) {
        size_t q = 0;
        while ((q = out.find("finalColor", q)) != std::string::npos) { out.replace(q, 10, "gl_FragColor"); q += 12; }
    }
    return out;
}

static std::string shaderDefines() {
    char buf[256];
    float fogB = VIEW_R * CHUNK_M - 4.0f;
    snprintf(buf, sizeof(buf), "#define FOG_A %.1f\n#define FOG_B %.1f\n#define GRASS_A %.1f\n#define GRASS_B %.1f\n",
             fogB * 0.73f, fogB, GRASS_DRAW * 0.5f, GRASS_DRAW * 0.83f);
    std::string d = buf;
#if defined(PLATFORM_ANDROID)
    d += "#define LOWQ\n#define SKY_OCT 3\n";
#else
    d += "#define SKY_OCT 5\n";
#endif
    return d;
}

static Shader loadShader(const char *vs, const char *fs, bool withCommon) {
#if defined(PLATFORM_ANDROID)
    std::string v = "#version 100\n" + shaderDefines() + toEs100(vs, true);
    std::string f = "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" +
                    shaderDefines() + toEs100(std::string(withCommon ? kCommon : "") + fs, false);
#else
    std::string v = "#version 330\n" + shaderDefines() + vs;
    std::string f = "#version 330\n" + shaderDefines() + (withCommon ? kCommon : "") + fs;
#endif
    return LoadShaderFromMemory(v.c_str(), f.c_str());
}

// ---------------------------------------------------------------- Svět: reset a spawn
static void resetWorld(bool newSeed) {
    for (auto &kv : chunkMap) {
        if (kv.second.has) UnloadMesh(kv.second.mesh);
        freeGrass(kv.second);
    }
    chunkMap.clear();
    columns.clear();
    edits.clear();
    editedChunks.clear();
    deadTrees.clear();
    treeHits.clear();
    tables.clear();
    parts.clear(); rockets.clear(); grenades.clear();
    if (newSeed) {
        gSeedX = (float)GetRandomValue(0, 20000);
        gSeedZ = (float)GetRandomValue(0, 20000);
    }
}

static Vector3 findSpawn() {
    for (float r = 0; r <= 600; r += 8)
        for (int a = 0; a < 16; a++) {
            float x = r * cosf(a * PI / 8.0f), z = r * sinf(a * PI / 8.0f);
            float h = terrainH(x, z);
            if (h > 3.0f && h < 26.0f) {
                float sx = terrainH(x + 2, z) - terrainH(x - 2, z), sz = terrainH(x, z + 2) - terrainH(x, z - 2);
                if (fabsf(sx) + fabsf(sz) < 0.8f) return vec3(x, h + 0.6f, z);
            }
        }
    return vec3(0, terrainH(0, 0) + 1.0f, 0);
}

// ---------------------------------------------------------------- Diagnostika
static FILE *gLog = nullptr;
static void logCallback(int level, const char *text, va_list args) {
    static const char *lv[] = {"ALL", "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "NONE"};
    char buf[2048];
    vsnprintf(buf, sizeof(buf), text, args);
    if (!gLog) gLog = fopen("complex_log.txt", "w");
    const char *name = (level >= 0 && level < 8) ? lv[level] : "?";
    if (gLog) { fprintf(gLog, "[%s] %s\n", name, buf); fflush(gLog); }
    fprintf(stdout, "[%s] %s\n", name, buf);
    fflush(stdout);
}
static bool shaderOk(const Shader &s, const char *name) {
    if (s.id == 0 || s.locs == nullptr) {
        TraceLog(LOG_ERROR, "COMPLEX: shader '%s' se nepodarilo prelozit/slinkovat", name);
        return false;
    }
    return true;
}
static void showFatal(const char *msg) {
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground({30, 20, 20, 255});
        DrawText(msg, 40, 40, 24, RAYWHITE);
        DrawText("Podrobnosti jsou v souboru complex_log.txt (vedle hry).", 40, 80, 20, RAYWHITE);
        EndDrawing();
    }
    CloseWindow();
}

// ---------------------------------------------------------------- HUD: kreslení
static void drawTextS(const char *t, int x, int y, int size, Color c) {
    DrawText(t, x + 1, y + 1, size, {0, 0, 0, 170});
    DrawText(t, x, y, size, c);
}
static void panel(float x, float y, float w, float h, unsigned char alpha) {
    DrawRectangleRounded({x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f}, 0.16f, 8, {255, 255, 255, 38});
    DrawRectangleRounded({x, y, w, h}, 0.16f, 8, {12, 14, 19, alpha});
}
static void triBoth(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

// srdce jako vyplněná křivka (vějíř trojúhelníků), kreslí se v obou směrech vinutí
static void drawHeartShape(float x, float y, float s, Color c) {
    const int NP = 32;
    static Vector2 fwd[NP + 2], rev[NP + 2];
    static bool init = false;
    static Vector2 unit[NP + 1];
    if (!init) {
        for (int k = 0; k < NP; k++) {
            float t = 6.2831853f * k / NP;
            float hx = 16.0f * powf(sinf(t), 3.0f);
            float hy = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
            unit[k] = {0.5f - hx / 34.0f, 0.5f - (hy + 2.65f) / 34.0f};
        }
        unit[NP] = unit[0];
        init = true;
    }
    Vector2 center = {x + s * 0.5f, y + s * 0.46f};
    fwd[0] = center; rev[0] = center;
    for (int k = 0; k <= NP; k++) {
        Vector2 p = {x + unit[k].x * s, y + unit[k].y * s};
        fwd[1 + k] = p;
        rev[1 + (NP - k)] = p;
    }
    DrawTriangleFan(fwd, NP + 2, c);
    DrawTriangleFan(rev, NP + 2, c);
}
static void drawHeart(float x, float y, float s, Color c) {
    drawHeartShape(x - 1.5f, y - 1.5f, s + 3.0f, {20, 6, 8, 230});
    drawHeartShape(x, y, s, c);
    if (c.a > 0 && c.r > 150) DrawCircleV({x + s * 0.30f, y + s * 0.27f}, s * 0.07f, {255, 255, 255, 120});
}
static void drawApple(float x, float y, float s, Color c) {
    DrawCircleV({x + s * 0.5f, y + s * 0.58f}, s * 0.43f, {25, 12, 8, 230});
    DrawCircleV({x + s * 0.5f, y + s * 0.58f}, s * 0.38f, c);
    DrawRectangle((int)(x + s * 0.46f), (int)(y + s * 0.06f), (int)fmaxf(2.0f, s * 0.09f), (int)(s * 0.22f), {95, 62, 30, 255});
    DrawCircleV({x + s * 0.70f, y + s * 0.20f}, s * 0.12f, {70, 160, 55, 255});
    if (c.r > 150) DrawCircleV({x + s * 0.34f, y + s * 0.46f}, s * 0.07f, {255, 255, 255, 110});
}
static void drawDrop(float x, float y, float s, Color c) {
    DrawCircleV({x + s * 0.5f, y + s * 0.66f}, s * 0.34f, {6, 14, 24, 230});
    triBoth({x + s * 0.5f, y - s * 0.02f}, {x + s * 0.15f, y + s * 0.62f}, {x + s * 0.85f, y + s * 0.62f}, {6, 14, 24, 230});
    DrawCircleV({x + s * 0.5f, y + s * 0.66f}, s * 0.29f, c);
    triBoth({x + s * 0.5f, y + s * 0.06f}, {x + s * 0.21f, y + s * 0.60f}, {x + s * 0.79f, y + s * 0.60f}, c);
    if (c.b > 150) DrawCircleV({x + s * 0.40f, y + s * 0.62f}, s * 0.06f, {255, 255, 255, 130});
}
static void drawBubble(float x, float y, float s, Color c) {
    DrawCircleV({x + s * 0.5f, y + s * 0.5f}, s * 0.38f, {10, 20, 30, 200});
    DrawCircleV({x + s * 0.5f, y + s * 0.5f}, s * 0.33f, c);
    DrawCircleV({x + s * 0.4f, y + s * 0.4f}, s * 0.08f, {255, 255, 255, 200});
}

// kind: 0 srdce, 1 hlad, 2 žízeň, 3 dech
static void drawBar(float x, float y, float value, int kind, float s = 21.0f) {
    for (int i = 0; i < 10; i++) {
        float ix = x + i * (s + 2.0f);
        float frac = clamp01((value - i * 2.0f) / 2.0f);
        Color full, empty;
        switch (kind) {
            case 0: full = {232, 48, 62, 255}; empty = {70, 28, 34, 210}; break;
            case 1: full = {226, 128, 44, 255}; empty = {72, 46, 30, 210}; break;
            case 2: full = {64, 156, 240, 255}; empty = {26, 48, 74, 210}; break;
            default: full = {175, 220, 255, 255}; empty = {30, 52, 72, 170}; break;
        }
        auto icon = [&](Color c) {
            if (kind == 0) drawHeart(ix, y, s, c);
            else if (kind == 1) drawApple(ix, y, s, c);
            else if (kind == 2) drawDrop(ix, y, s, c);
            else drawBubble(ix, y, s, c);
        };
        icon(empty);
        if (frac > 0.0f) {
            BeginScissorMode((int)((ix - 2) * gUIS), (int)((y - 3) * gUIS), (int)((ceilf(s * frac) + 3) * gUIS), (int)((s + 8) * gUIS));
            icon(full);
            EndScissorMode();
        }
    }
}

static void drawItemIcon(int item, float x, float y, float s) {
    switch (item) {
        case I_HAMMER: {
            DrawLineEx({x + s * 0.25f, y + s * 0.88f}, {x + s * 0.68f, y + s * 0.30f}, s * 0.14f, {40, 28, 18, 255});
            DrawLineEx({x + s * 0.25f, y + s * 0.88f}, {x + s * 0.68f, y + s * 0.30f}, s * 0.09f, {165, 122, 78, 255});
            DrawRectanglePro({x + s * 0.70f, y + s * 0.26f, s * 0.52f, s * 0.26f}, {s * 0.26f, s * 0.13f}, -50.0f, {30, 30, 34, 255});
            DrawRectanglePro({x + s * 0.70f, y + s * 0.26f, s * 0.46f, s * 0.20f}, {s * 0.23f, s * 0.10f}, -50.0f, {150, 152, 160, 255});
            break;
        }
        case I_STONE:
            DrawRectangleRounded({x + s * 0.08f, y + s * 0.30f, s * 0.84f, s * 0.58f}, 0.4f, 6, {38, 38, 42, 255});
            DrawRectangleRounded({x + s * 0.12f, y + s * 0.34f, s * 0.76f, s * 0.50f}, 0.4f, 6, {128, 126, 122, 255});
            DrawRectangleRounded({x + s * 0.22f, y + s * 0.38f, s * 0.36f, s * 0.14f}, 0.5f, 6, {170, 168, 162, 255});
            break;
        case I_WOOD:
            DrawRectangleRounded({x + s * 0.06f, y + s * 0.34f, s * 0.82f, s * 0.36f}, 0.3f, 6, {40, 26, 16, 255});
            DrawRectangleRounded({x + s * 0.09f, y + s * 0.37f, s * 0.76f, s * 0.30f}, 0.3f, 6, {136, 96, 58, 255});
            DrawCircleV({x + s * 0.86f, y + s * 0.52f}, s * 0.17f, {40, 26, 16, 255});
            DrawCircleV({x + s * 0.86f, y + s * 0.52f}, s * 0.14f, {196, 158, 106, 255});
            DrawCircleV({x + s * 0.86f, y + s * 0.52f}, s * 0.07f, {160, 120, 78, 255});
            break;
        case I_APPLE: drawApple(x, y, s, {218, 56, 46, 255}); break;
        case I_GRENADE:
            DrawCircleV({x + s * 0.5f, y + s * 0.60f}, s * 0.34f, {20, 28, 18, 255});
            DrawCircleV({x + s * 0.5f, y + s * 0.60f}, s * 0.30f, {66, 92, 58, 255});
            DrawCircleV({x + s * 0.40f, y + s * 0.50f}, s * 0.07f, {130, 160, 120, 255});
            DrawRectangle((int)(x + s * 0.42f), (int)(y + s * 0.14f), (int)(s * 0.16f), (int)(s * 0.18f), {170, 170, 175, 255});
            DrawRectangle((int)(x + s * 0.58f), (int)(y + s * 0.18f), (int)(s * 0.22f), (int)(s * 0.07f), {200, 200, 205, 255});
            break;
        case I_ROCKET:
            DrawRectangleRounded({x + s * 0.36f, y + s * 0.20f, s * 0.28f, s * 0.62f}, 0.4f, 6, {30, 30, 34, 255});
            DrawRectangleRounded({x + s * 0.39f, y + s * 0.23f, s * 0.22f, s * 0.56f}, 0.4f, 6, {160, 162, 168, 255});
            triBoth({x + s * 0.5f, y + s * 0.02f}, {x + s * 0.34f, y + s * 0.26f}, {x + s * 0.66f, y + s * 0.26f}, {214, 62, 52, 255});
            triBoth({x + s * 0.36f, y + s * 0.62f}, {x + s * 0.18f, y + s * 0.92f}, {x + s * 0.36f, y + s * 0.82f}, {200, 60, 50, 255});
            triBoth({x + s * 0.64f, y + s * 0.62f}, {x + s * 0.82f, y + s * 0.92f}, {x + s * 0.64f, y + s * 0.82f}, {200, 60, 50, 255});
            break;
        case I_DYNAMITE:
            DrawRectangleRounded({x + s * 0.24f, y + s * 0.26f, s * 0.52f, s * 0.64f}, 0.25f, 6, {50, 14, 12, 255});
            DrawRectangleRounded({x + s * 0.28f, y + s * 0.30f, s * 0.44f, s * 0.56f}, 0.25f, 6, {204, 48, 40, 255});
            DrawRectangle((int)(x + s * 0.28f), (int)(y + s * 0.50f), (int)(s * 0.44f), (int)(s * 0.14f), {236, 220, 150, 255});
            DrawLineEx({x + s * 0.5f, y + s * 0.26f}, {x + s * 0.62f, y + s * 0.10f}, s * 0.05f, {60, 50, 40, 255});
            DrawCircleV({x + s * 0.64f, y + s * 0.08f}, s * 0.07f, {255, 190, 70, 255});
            break;
        case I_TABLE:
            DrawRectangleRounded({x + s * 0.06f, y + s * 0.30f, s * 0.88f, s * 0.18f}, 0.3f, 6, {36, 24, 14, 255});
            DrawRectangleRounded({x + s * 0.09f, y + s * 0.33f, s * 0.82f, s * 0.12f}, 0.3f, 6, {168, 124, 76, 255});
            DrawRectangle((int)(x + s * 0.14f), (int)(y + s * 0.48f), (int)(s * 0.10f), (int)(s * 0.40f), {110, 78, 46, 255});
            DrawRectangle((int)(x + s * 0.76f), (int)(y + s * 0.48f), (int)(s * 0.10f), (int)(s * 0.40f), {110, 78, 46, 255});
            DrawRectangle((int)(x + s * 0.16f), (int)(y + s * 0.20f), (int)(s * 0.22f), (int)(s * 0.10f), {140, 142, 150, 255});
            break;
    }
}

// kladivo v ruce (animace úderu)
static void drawHeldHammer(float swing, float W, float H) {
    float thetaDeg = 22.0f - 62.0f * swing;
    float th = thetaDeg * DEG2RAD;
    Vector2 P = {W - 150.0f, H + 10.0f};
    Vector2 d = {-sinf(th), -cosf(th)};
    float len = 250.0f;
    Vector2 tip = {P.x + d.x * len, P.y + d.y * len};
    DrawLineEx({P.x + 4, P.y + 4}, {tip.x + 4, tip.y + 4}, 26.0f, {0, 0, 0, 80});
    DrawLineEx(P, tip, 24.0f, {52, 36, 22, 255});
    DrawLineEx(P, tip, 16.0f, {158, 116, 72, 255});
    DrawLineEx({P.x - 4, P.y}, {tip.x - 4, tip.y}, 4.0f, {196, 154, 108, 255});
    DrawRectanglePro({tip.x, tip.y, 118.0f, 62.0f}, {59.0f, 31.0f}, -thetaDeg, {34, 34, 40, 255});
    DrawRectanglePro({tip.x, tip.y, 106.0f, 50.0f}, {53.0f, 25.0f}, -thetaDeg, {138, 140, 150, 255});
    DrawRectanglePro({tip.x, tip.y - 8, 90.0f, 12.0f}, {45.0f, 6.0f}, -thetaDeg, {190, 192, 200, 255});
}

// ---------------------------------------------------------------- Vstup (klávesnice/myš nebo dotyk)
struct RRect { float x, y, w, h; };
static RRect gRecipeRect[3];
static int gRecipeN = 0;
struct HotbarGeom { float x, y, slot, gap; };
static HotbarGeom gHotbar = {0, 0, 54, 6};
static bool gDeadScreen = false;

#if defined(PLATFORM_ANDROID)
static bool vDown[512], vPrev[512], vMDown[2], vMPrev[2];
static Vector2 vLook = {0, 0};
static bool vSprint = false;
static int gStickId = -1, gLookId = -1;
static Vector2 gStickO = {0, 0}, gStickP = {0, 0}, gLookLast = {0, 0};
static int gPrevIds[16], gPrevN = 0;

enum { B_DIG = 0, B_USE, B_JUMP, B_SPRINT, B_INV, B_E, B_F, B_COUNT };
struct TBtn { float x, y, r; const char *label; };
static TBtn gBtn[B_COUNT];

static bool vkDown(int k) { return k >= 0 && k < 512 && vDown[k]; }
static bool vkPressed(int k) { return k >= 0 && k < 512 && vDown[k] && !vPrev[k]; }
static bool vmDown(int b) { return b >= 0 && b < 2 && vMDown[b]; }
static bool vmPressed(int b) { return b >= 0 && b < 2 && vMDown[b] && !vMPrev[b]; }
static Vector2 vLookDelta() { return {vLook.x * 1.4f, vLook.y * 1.4f}; }
#define IN_KEY_DOWN(k) vkDown(k)
#define IN_KEY_PRESSED(k) vkPressed(k)
#define IN_MOUSE_DOWN(b) vmDown(b)
#define IN_MOUSE_PRESSED(b) vmPressed(b)
#define IN_MOUSE_DELTA() vLookDelta()
#define IN_WHEEL() 0.0f

static void layoutButtons(int W, int H) {
    float s = H / 720.0f;
    gBtn[B_DIG]    = {W - 150 * s, H - 170 * s, 66 * s, "KOP"};
    gBtn[B_USE]    = {W - 310 * s, H - 100 * s, 50 * s, "POUZIJ"};
    gBtn[B_JUMP]   = {W - 110 * s, H - 330 * s, 46 * s, "SKOK"};
    gBtn[B_SPRINT] = {110 * s, H - 400 * s, 40 * s, "BEH"};
    gBtn[B_INV]    = {W - 56 * s, 120 * s, 36 * s, "INV"};
    gBtn[B_E]      = {W - 56 * s, 210 * s, 36 * s, "E"};
    gBtn[B_F]      = {W - 56 * s, 300 * s, 36 * s, "F"};
}
static bool wasPrevId(int id) {
    for (int i = 0; i < gPrevN; i++) if (gPrevIds[i] == id) return true;
    return false;
}
static int hitButton(Vector2 p) {
    for (int i = 0; i < B_COUNT; i++) {
        float dx = p.x - gBtn[i].x, dy = p.y - gBtn[i].y;
        if (dx * dx + dy * dy <= gBtn[i].r * gBtn[i].r * 1.3f) return i;
    }
    return -1;
}
static void updateTouch(int W, int H) {
    memcpy(vPrev, vDown, sizeof(vDown));
    vMPrev[0] = vMDown[0]; vMPrev[1] = vMDown[1];
    memset(vDown, 0, sizeof(vDown));
    vMDown[0] = vMDown[1] = false;
    vLook = {0, 0};
    layoutButtons(W, H);
    float s = H / 720.0f;
    int n = GetTouchPointCount();
    if (n > 10) n = 10;
    bool stickAlive = false, lookAlive = false;
    int ids[16];
    for (int i = 0; i < n; i++) {
        int id = GetTouchPointId(i);
        Vector2 p = GetTouchPosition(i);
        ids[i] = id;
        bool fresh = !wasPrevId(id);
        if (id == gStickId) { stickAlive = true; gStickP = p; continue; }
        if (id == gLookId) {
            lookAlive = true;
            vLook = {p.x - gLookLast.x, p.y - gLookLast.y};
            gLookLast = p;
            continue;
        }
        int b = hitButton(p);
        if (b >= 0) {
            switch (b) {
                case B_DIG: vMDown[0] = true; break;
                case B_USE: vMDown[1] = true; break;
                case B_JUMP: vDown[KEY_SPACE] = true; break;
                case B_SPRINT: if (fresh) vSprint = !vSprint; break;
                case B_INV: if (fresh) vDown[KEY_TAB] = true; break;
                case B_E: if (fresh) vDown[KEY_E] = true; break;
                case B_F: vDown[KEY_F] = true; break;
                default: break;
            }
            continue;
        }
        if (!fresh) continue;                       // dotek, který začal jinde a sjel mimo tlačítko
        if (gDeadScreen) { vDown[KEY_SPACE] = true; continue; }
        // tap na recept (inventář / stůl)
        bool used = false;
        float vx = p.x / gUIS, vy = p.y / gUIS;
        for (int r = 0; r < gRecipeN && !used; r++)
            if (vx >= gRecipeRect[r].x && vx <= gRecipeRect[r].x + gRecipeRect[r].w &&
                vy >= gRecipeRect[r].y && vy <= gRecipeRect[r].y + gRecipeRect[r].h) {
                vDown[KEY_ONE + r] = true;
                used = true;
            }
        // tap na slot hotbaru
        if (!used && gRecipeN == 0) {
            for (int k = 0; k < 8 && !used; k++) {
                float x0 = gHotbar.x + k * (gHotbar.slot + gHotbar.gap);
                if (vx >= x0 && vx <= x0 + gHotbar.slot && vy >= gHotbar.y && vy <= gHotbar.y + gHotbar.slot) {
                    vDown[KEY_ONE + k] = true;
                    used = true;
                }
            }
        }
        if (used) continue;
        if (gStickId < 0 && p.x < W * 0.45f && p.y > H * 0.30f) {
            gStickId = id; gStickO = p; gStickP = p; stickAlive = true;
            continue;
        }
        if (gLookId < 0) { gLookId = id; gLookLast = p; lookAlive = true; }
    }
    if (!stickAlive) gStickId = -1;
    if (!lookAlive) gLookId = -1;
    if (gStickId >= 0) {
        float R = 90.0f * s;
        float ax = (gStickP.x - gStickO.x) / R, ay = (gStickP.y - gStickO.y) / R;
        if (ay < -0.35f) vDown[KEY_W] = true;
        if (ay > 0.35f) vDown[KEY_S] = true;
        if (ax > 0.35f) vDown[KEY_D] = true;
        if (ax < -0.35f) vDown[KEY_A] = true;
    }
    if (vSprint) vDown[KEY_LEFT_SHIFT] = true;
    gPrevN = n;
    for (int i = 0; i < n; i++) gPrevIds[i] = ids[i];
}
static void drawTouchUI(int W, int H) {
    float s = H / 720.0f;
    Vector2 o = (gStickId >= 0) ? gStickO : Vector2{150.0f * s, H - 150.0f * s};
    DrawCircleV(o, 90.0f * s, {255, 255, 255, gStickId >= 0 ? (unsigned char)40 : (unsigned char)22});
    Vector2 k = o;
    if (gStickId >= 0) {
        float dx = gStickP.x - o.x, dy = gStickP.y - o.y, l = sqrtf(dx * dx + dy * dy), R = 90.0f * s;
        if (l > R) { dx *= R / l; dy *= R / l; }
        k = {o.x + dx, o.y + dy};
    }
    DrawCircleV(k, 40.0f * s, {255, 255, 255, 110});
    for (int i = 0; i < B_COUNT; i++) {
        bool on = (i == B_DIG && vMDown[0]) || (i == B_USE && vMDown[1]) || (i == B_JUMP && vDown[KEY_SPACE]) ||
                  (i == B_SPRINT && vSprint) || (i == B_F && vDown[KEY_F]);
        DrawCircleV({gBtn[i].x, gBtn[i].y}, gBtn[i].r, {255, 255, 255, on ? (unsigned char)120 : (unsigned char)46});
        DrawCircleV({gBtn[i].x, gBtn[i].y}, gBtn[i].r * 0.9f, {20, 24, 30, on ? (unsigned char)150 : (unsigned char)110});
        int fs = (int)(gBtn[i].r * 0.42f);
        int tw = MeasureText(gBtn[i].label, fs);
        DrawText(gBtn[i].label, (int)(gBtn[i].x - tw / 2), (int)(gBtn[i].y - fs / 2), fs, WHITE);
    }
    (void)W;
}
#else
#define IN_KEY_DOWN(k) IsKeyDown(k)
#define IN_KEY_PRESSED(k) IsKeyPressed(k)
#define IN_MOUSE_DOWN(b) IsMouseButtonDown(b)
#define IN_MOUSE_PRESSED(b) IsMouseButtonPressed(b)
#define IN_MOUSE_DELTA() GetMouseDelta()
#define IN_WHEEL() GetMouseWheelMove()
#endif

// ---------------------------------------------------------------- Export textur
static float pnoise(float x, float z, int px, int pz) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    auto h = [&](int a, int b) {
        a = ((a % px) + px) % px;
        b = ((b % pz) + pz) % pz;
        return hash2(a, b);
    };
    float a = h(ix, iz), b = h(ix + 1, iz), c = h(ix, iz + 1), d = h(ix + 1, iz + 1);
    float t = a + (b - a) * fx, u = c + (d - c) * fx;
    return t + (u - t) * fz;
}
// opakující se (seamless) fraktální šum, u,v v <0,1)
static float pfbm(float u, float v, int fx, int fz, int oct) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < oct; i++) {
        sum += pnoise(u * fx, v * fz, fx, fz) * amp;
        norm += amp;
        amp *= 0.5f;
        fx *= 2;
        fz *= 2;
    }
    return sum / norm;
}

static Color texColor(int kind, float u, float v) {
    switch (kind) {
        case 0: {   // tráva
            float n1 = pfbm(u, v, 4, 4, 4), n2 = pfbm(u, v, 32, 32, 2), st = pnoise(u * 96, v * 16, 96, 16);
            float k = (0.72f + 0.5f * n2) * (0.9f + 0.25f * st);
            return {clamp8f((58 + 62 * n1) * k), clamp8f((92 + 50 * n1) * k), clamp8f((30 + 28 * n1) * k), 255};
        }
        case 1: {   // hlína
            float n = pfbm(u, v, 6, 6, 4), pb = pnoise(u * 48, v * 48, 48, 48);
            float k = (0.65f + 0.7f * n) * (pb > 0.83f ? 1.25f : 1.0f);
            return {clamp8f(106 * k), clamp8f(80 * k), clamp8f(58 * k), 255};
        }
        case 2: {   // skála
            float n = pfbm(u, v, 5, 5, 5);
            float rid = 1.0f - fabsf(2.0f * pfbm(u, v, 4, 4, 3) - 1.0f);
            float crack = smoothstepf(0.90f, 0.98f, rid);
            float strata = sinf(v * 6.2831853f * 6.0f + n * 5.0f) * 0.07f;
            float k = 0.72f + 0.55f * n + strata - 0.35f * crack;
            return {clamp8f(116 * k), clamp8f(112 * k), clamp8f(106 * k), 255};
        }
        case 3: {   // písek
            float n = pfbm(u, v, 8, 8, 4);
            float rip = sinf((u * 0.3f + v) * 6.2831853f * 14.0f + n * 4.0f) * 0.04f;
            float k = 0.88f + 0.22f * n + rip;
            return {clamp8f(198 * k), clamp8f(180 * k), clamp8f(130 * k), 255};
        }
        case 4: {   // sníh
            float n = pfbm(u, v, 8, 8, 3);
            float k = 0.94f + 0.08f * n;
            return {clamp8f(236 * k), clamp8f(241 * k), clamp8f(248 * k), 255};
        }
        case 5: case 9: {   // dřevěná prkna (5) / deska stolu (9)
            int boards = (kind == 5) ? 4 : 3;
            int b = (int)floorf(v * boards);
            float fv = v * boards - b;
            float uu = u + hash2(b, 7) * 0.37f;
            uu -= floorf(uu);
            float grain = pfbm(uu, v, 3, boards * 12, 3);
            float tint = 0.9f + 0.2f * hash2(b, 3);
            float k = (0.72f + 0.5f * grain) * tint;
            if (fv < 0.03f || fv > 0.97f) k *= 0.35f;
            if (kind == 5) return {clamp8f(164 * k), clamp8f(122 * k), clamp8f(72 * k), 255};
            return {clamp8f(154 * k), clamp8f(112 * k), clamp8f(66 * k), 255};
        }
        case 6: {   // kůra
            float n = pfbm(u, v, 24, 3, 4);
            float rid = 1.0f - fabsf(2.0f * pfbm(u, v, 12, 2, 3) - 1.0f);
            float k = 0.55f + 0.9f * n - 0.3f * smoothstepf(0.85f, 1.0f, rid);
            return {clamp8f(74 * k), clamp8f(54 * k), clamp8f(38 * k), 255};
        }
        case 7: {   // listí
            float n = pfbm(u, v, 10, 10, 4), sp = pnoise(u * 40, v * 40, 40, 40);
            float k = 0.7f + 0.6f * n + (sp > 0.8f ? 0.25f : 0.0f);
            return {clamp8f(56 * k), clamp8f(106 * k), clamp8f(38 * k), 255};
        }
        case 8: {   // jehličí
            float n = pfbm(u, v, 40, 6, 3);
            float k = 0.6f + 0.9f * n;
            return {clamp8f(34 * k), clamp8f(74 * k), clamp8f(46 * k), 255};
        }
        case 10: {  // šum vody (stupně šedi)
            float n = pfbm(u, v, 6, 6, 5);
            unsigned char g = clamp8f(n * 255.0f);
            return {g, g, g, 255};
        }
        default: {  // mraky (bílá s průhledností)
            float n = pfbm(u, v, 4, 4, 5);
            return {255, 255, 255, clamp8f(smoothstepf(0.45f, 0.75f, n) * 255.0f)};
        }
    }
}

static bool exportTexKind(const char *name, int kind, int size) {
    Color *px = (Color *)MemAlloc((unsigned)(size * size * sizeof(Color)));
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) px[y * size + x] = texColor(kind, (x + 0.5f) / size, (y + 0.5f) / size);
    Image img = {px, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    bool ok = ExportImage(img, TextFormat("textures/%s.png", name));
    UnloadImage(img);
    return ok;
}

static bool exportBladeTex() {
    const int w = 64, h = 256;
    Color *px = (Color *)MemAlloc((unsigned)(w * h * sizeof(Color)));
    for (int y = 0; y < h; y++) {
        float v = (y + 0.5f) / h;                       // 0 = špička, 1 = kořen
        float hw = 0.02f + 0.46f * powf(v, 0.9f);
        for (int x = 0; x < w; x++) {
            float u = (x + 0.5f) / w;
            float d = fabsf(u - 0.5f);
            float a = 1.0f - smoothstepf(hw - 0.03f, hw, d);
            float k = 0.85f + 0.3f * (1.0f - fabsf(u - 0.5f) * 2.0f);
            px[y * w + x] = {clamp8f((150 + (45 - 150) * v) * k), clamp8f((175 + (80 - 175) * v) * k),
                             clamp8f((70 + (28 - 70) * v) * k), clamp8f(a * 255.0f)};
        }
    }
    Image img = {px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    bool ok = ExportImage(img, "textures/grass_blade.png");
    UnloadImage(img);
    return ok;
}

static int exportAssets() {
    MakeDirectory("textures");
    int n = 0;
    struct TK { const char *name; int kind; int size; };
    static const TK list[] = {
        {"ground_grass", 0, 256}, {"ground_dirt", 1, 256}, {"ground_rock", 2, 256}, {"ground_sand", 3, 256},
        {"ground_snow", 4, 256}, {"wood_planks", 5, 256}, {"table_top", 9, 256}, {"tree_bark", 6, 256},
        {"tree_leaves", 7, 256}, {"tree_pine", 8, 256}, {"noise_water", 10, 256}, {"noise_clouds", 11, 256}};
    for (const TK &t : list) if (exportTexKind(t.name, t.kind, t.size)) n++;
    if (exportBladeTex()) n++;

    // částice (kouř/oheň)
    {
        Image pimg = GenImageGradientRadial(128, 128, 0.0f, WHITE, {255, 255, 255, 0});
        if (ExportImage(pimg, "textures/particle_puff.png")) n++;
        UnloadImage(pimg);
    }

    // ikony věcí a HUD
    {
        static const char *names[12] = {"icon_hammer", "icon_stone", "icon_wood", "icon_apple", "icon_grenade",
                                        "icon_rocket", "icon_dynamite", "icon_table", "ui_heart", "ui_hunger",
                                        "ui_thirst", "ui_air"};
        RenderTexture2D rt = LoadRenderTexture(128, 128);
        for (int k = 0; k < 12; k++) {
            BeginTextureMode(rt);
            ClearBackground({0, 0, 0, 0});
            if (k < I_COUNT) drawItemIcon(k, 10, 10, 108);
            else if (k == 8) drawHeart(14, 14, 100, {232, 48, 62, 255});
            else if (k == 9) drawApple(14, 14, 100, {226, 128, 44, 255});
            else if (k == 10) drawDrop(14, 14, 100, {64, 156, 240, 255});
            else drawBubble(14, 14, 100, {175, 220, 255, 255});
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            if (ExportImage(img, TextFormat("textures/%s.png", names[k]))) n++;
            UnloadImage(img);
        }
        UnloadRenderTexture(rt);
    }
    return n;
}

// ---------------------------------------------------------------- Main
int main() {
#if defined(PLATFORM_ANDROID)
    SetConfigFlags(FLAG_VSYNC_HINT);                     // log jde do logcat, bez MSAA kvůli výkonu
#else
    SetTraceLogCallback(logCallback);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
#endif
    InitWindow(1280, 720, "complex");
    TraceLog(LOG_INFO, "COMPLEX: okno vytvoreno");

    resetWorld(true);

    Shader shScene = loadShader(kVertMesh, kFragScene, true);
    Shader shWater = loadShader(kVertSimple, kFragWater, true);
    Shader shSky = loadShader(kVertSky, kFragSky, true);
    Shader shGrass = loadShader(kVertGrass, kFragGrass, true);
    if (!shaderOk(shScene, "scene") || !shaderOk(shWater, "water") || !shaderOk(shSky, "sky") ||
        !shaderOk(shGrass, "grass")) {
        showFatal("Nektery shader se nepodarilo prelozit.");
        return 1;
    }
    TraceLog(LOG_INFO, "COMPLEX: shadery OK");

    shScene.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shScene, "matModel");
    shScene.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shScene, "matNormal");
    shWater.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shWater, "matModel");
    shGrass.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shGrass, "matModel");

    Material matScene = LoadMaterialDefault();
    matScene.shader = shScene;
    Material matWater = LoadMaterialDefault();
    matWater.shader = shWater;
    Material matGrass = LoadMaterialDefault();
    matGrass.shader = shGrass;

    CommonLocs cScene = commonLocs(shScene), cWater = commonLocs(shWater), cSky = commonLocs(shSky),
               cGrass = commonLocs(shGrass);
    int locViewScene = GetShaderLocation(shScene, "viewPos");
    int locAmb = GetShaderLocation(shScene, "groundAmb");
    int locViewWater = GetShaderLocation(shWater, "viewPos");
    int locTimeWater = GetShaderLocation(shWater, "time");
    int locViewGrass = GetShaderLocation(shGrass, "viewPos");
    int locAmbGrass = GetShaderLocation(shGrass, "groundAmb");
    int locTimeGrass = GetShaderLocation(shGrass, "time");
    int locRight = GetShaderLocation(shSky, "camRight"), locUp = GetShaderLocation(shSky, "camUp");
    int locFwd = GetShaderLocation(shSky, "camFwd"), locTan = GetShaderLocation(shSky, "tanFov");
    int locAsp = GetShaderLocation(shSky, "aspect"), locRes = GetShaderLocation(shSky, "resolution");
    int locTimeSky = GetShaderLocation(shSky, "time");
    int locNight = GetShaderLocation(shSky, "nightAmt");

    Mesh waterMesh = GenMeshPlane(2600.0f, 2600.0f, 1, 1);
    Mesh tableMesh = buildTableMesh();

    Image pimg = GenImageGradientRadial(64, 64, 0.0f, WHITE, {255, 255, 255, 0});
    Texture2D puff = LoadTextureFromImage(pimg);
    UnloadImage(pimg);
    TraceLog(LOG_INFO, "COMPLEX: materialy OK");

    // --- stav hráče
    Vector3 spawnPos = findSpawn();
    Vector3 pos = spawnPos, vel = {0, 0, 0};
    bool onGround = false;
    float groundTimer = 0, yaw = 0, pitch = 0, cooldown = 0, timeSec = 0;
    float health = 20, hunger = 20, thirst = 20, air = 15;       // 20 = 10 ikon
    bool dead = false;
    int uiMode = 0;                  // 0 nic, 1 inventář, 2 pracovní stůl
    int uiTableId = -1, craftRecipe = -1;
    float craftProg = 0, swing = 0, helpTimer = 40.0f;
    float flash = 0, digAcc = 0, regenAcc = 0;
    int sel = I_HAMMER;
    float tod = 8.0f;
    int day = 1;
    inv[I_HAMMER] = 1;
    inv[I_APPLE] = 2;

    auto respawn = [&]() {
        pos = spawnPos; vel = {0, 0, 0};
        health = 20; hunger = 20; thirst = 20; air = 15;
        for (int i = 0; i < I_COUNT; i++) inv[i] = 0;
        inv[I_HAMMER] = 1;
        inv[I_APPLE] = 2;
        dead = false; uiMode = 0; flash = 0; toasts.clear();
        craftRecipe = -1; craftProg = 0;
        rockets.clear(); grenades.clear();
    };

    Camera3D cam = {};
    cam.up = {0, 1, 0};
    cam.fovy = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;
#if !defined(PLATFORM_ANDROID)
    DisableCursor();
#endif
    rlSetClipPlanes(CAM_NEAR, CAM_FAR);

    TraceLog(LOG_INFO, "COMPLEX: zacina nacitani chunku");
    while (!WindowShouldClose()) {
        int left = updateStreaming(pos, 0.04, 4, false);
        BeginDrawing();
        ClearBackground({24, 26, 30, 255});
        DrawText("Generuji svet...", 40, 40, 30, RAYWHITE);
        EndDrawing();
        if (left == 0) break;
    }
    for (int i = 0; i < 6; i++) updateGrass(pos, 0.05);
    TraceLog(LOG_INFO, "COMPLEX: chunky nacteny, start hlavni smycky");

    int frameNo = 0;
    bool skipMouse = false;
    while (!WindowShouldClose()) {
        frameNo++;
        const bool dbg = frameNo <= 2;
        float dt = GetFrameTime();
        if (dt > 1.0f / 20.0f) dt = 1.0f / 20.0f;
        timeSec += dt;
        cooldown -= dt;
        swing = fmaxf(0.0f, swing - dt * 4.5f);
        helpTimer -= dt;
#if defined(PLATFORM_ANDROID)
        gUIS = fmaxf(1.0f, GetScreenHeight() / 720.0f);
        gDeadScreen = dead;
        updateTouch(GetScreenWidth(), GetScreenHeight());
#endif

        // ---- denní cyklus
        tod += dt * 24.0f / DAY_SECONDS;
        if (tod >= 24.0f) { tod -= 24.0f; day++; }

        // ---- globální klávesy
        if (IN_KEY_PRESSED(KEY_R)) {
            resetWorld(true);
            spawnPos = findSpawn();
            respawn();
            tod = 8.0f; day = 1;
        }
        if (IN_KEY_PRESSED(KEY_F11) || (IN_KEY_DOWN(KEY_LEFT_ALT) && IN_KEY_PRESSED(KEY_ENTER))) {
#if !defined(PLATFORM_ANDROID)
            ToggleBorderlessWindowed();
            DisableCursor();
#endif
            skipMouse = true;
        }
        if (dead && IN_KEY_PRESSED(KEY_SPACE)) respawn();
        bool ctrlDown = IN_KEY_DOWN(KEY_LEFT_CONTROL) || IN_KEY_DOWN(KEY_RIGHT_CONTROL);
        bool shiftDown = IN_KEY_DOWN(KEY_LEFT_SHIFT) || IN_KEY_DOWN(KEY_RIGHT_SHIFT);
        bool exportKey = ctrlDown && shiftDown && IN_KEY_PRESSED(KEY_TAB);
        if (exportKey) {
            int cnt = exportAssets();
            toast("Ulozeno %d souboru do slozky textures", cnt);
            TraceLog(LOG_INFO, "COMPLEX: export textur: %d souboru", cnt);
        }
        if (!dead && !exportKey && IN_KEY_PRESSED(KEY_TAB)) {
            uiMode = (uiMode == 1) ? 0 : 1;
            craftRecipe = -1; craftProg = 0;
        }
        const bool uiBlock = (uiMode != 0) || dead;

        // ---- rozhlížení
        Vector2 md = IN_MOUSE_DELTA();
        if (skipMouse || uiBlock) { md = {0, 0}; skipMouse = false; }
        yaw -= md.x * 0.0025f;
        pitch -= md.y * 0.0025f;
        pitch = Clamp(pitch, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
        Vector3 fwdH = {sinf(yaw), 0, cosf(yaw)};
        Vector3 rightH = {-cosf(yaw), 0, sinf(yaw)};
        Vector3 look = {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};

        // ---- pohyb
        bool sprint = IN_KEY_DOWN(KEY_LEFT_SHIFT) && hunger > 3.0f && !dead;
        Vector3 wish = {0, 0, 0};
        if (!dead) {
            if (IN_KEY_DOWN(KEY_W)) wish = vadd(wish, fwdH);
            if (IN_KEY_DOWN(KEY_S)) wish = vsub(wish, fwdH);
            if (IN_KEY_DOWN(KEY_D)) wish = vadd(wish, rightH);
            if (IN_KEY_DOWN(KEY_A)) wish = vsub(wish, rightH);
        }
        float wl = vlen(wish);
        if (wl > 0) wish = vmul(wish, 1.0f / wl);
        bool moving = wl > 0;

        bool inWater = pos.y + 0.4f < SEA;
        float sp = (sprint ? 7.5f : 4.2f) * (inWater ? 0.55f : 1.0f);
        float acc = fminf(1.0f, (onGround ? 12.0f : 3.0f) * dt);
        vel.x += (wish.x * sp - vel.x) * acc;
        vel.z += (wish.z * sp - vel.z) * acc;
        vel = vadd(vel, gKnock);
        gKnock = {0, 0, 0};
        vel.y -= G * (inWater ? 0.25f : 1.0f) * dt;
        if (inWater) { vel.y *= (1.0f - 1.5f * dt); if (!dead && IN_KEY_DOWN(KEY_SPACE)) vel.y += 14.0f * dt; }
        if (!dead && groundTimer > 0 && IN_KEY_PRESSED(KEY_SPACE)) { vel.y = 4.8f; groundTimer = 0; }

        float vyBefore = vel.y;
        float groundNy = 0;
        for (int s = 0; s < 3; s++) {
            pos = vadd(pos, vmul(vel, dt / 3.0f));
            Vector3 c = vadd(pos, vec3(0, 0.42f, 0));
            resolveSphere(c, 0.42f, vel, groundNy);
            pos = vsub(c, vec3(0, 0.42f, 0));
            c = vadd(pos, vec3(0, 1.38f, 0));
            float dummy = 0;
            resolveSphere(c, 0.42f, vel, dummy);
            pos = vsub(c, vec3(0, 1.38f, 0));
        }
        onGround = groundNy > 0.62f;
        if (onGround) groundTimer = 0.12f; else groundTimer -= dt;

        // pracovní stoly jsou pevné překážky
        for (const Table &t : tables) {
            float dx = pos.x - t.p.x, dz = pos.z - t.p.z, dy = pos.y - t.p.y;
            if (dy > -1.8f && dy < 0.85f) {
                float dd = sqrtf(dx * dx + dz * dz), rr = 0.80f + 0.42f;
                if (dd < rr && dd > 1e-4f) { pos.x = t.p.x + dx / dd * rr; pos.z = t.p.z + dz / dd * rr; }
            }
        }

        // pád
        if (!dead && onGround && !inWater && vyBefore < -10.0f)
            gDamage += (-vyBefore - 9.5f) * 1.8f;

        Vector3 eye = vadd(pos, vec3(0, 1.65f, 0));
        gEye = eye;

        // ---- survival: hlad, žízeň, zdraví, dech
        if (!dead) {
            float act = (sprint && moving) ? 2.4f : (moving ? 1.2f : 1.0f);
            hunger = fmaxf(0.0f, hunger - dt * act / 45.0f);
            thirst = fmaxf(0.0f, thirst - dt * act * 1.4f / 45.0f);
            if (hunger <= 0.0f || thirst <= 0.0f) {
                health -= dt * 0.35f;
                regenAcc = 0;
            } else if (hunger >= 14.0f && thirst >= 14.0f && health < 20.0f) {
                regenAcc += dt;
                if (regenAcc >= 0.5f) { health = fminf(20.0f, health + 0.15f); regenAcc = 0; }
            }
            if (eye.y < SEA) {
                air -= dt;
                if (air <= 0.0f) { air = 0.0f; health -= dt * 2.0f; }
            } else air = fminf(15.0f, air + dt * 6.0f);

            if (gDamage > 0.0f) {
                health -= gDamage;
                flash = fminf(0.6f, 0.25f + gDamage * 0.03f);
            }
            if (health <= 0.0f) {
                health = 0;
                dead = true;
                uiMode = 0;
            }
        }
        gDamage = 0.0f;
        flash = fmaxf(0.0f, flash - dt * 1.2f);

        // ---- pracovní stoly: gravitace
        for (Table &t : tables) {
            if (densityGrad(vec3(t.p.x, t.p.y - 0.06f, t.p.z), nullptr) <= 0.0f) t.p.y -= 5.0f * dt;
            else if (densityGrad(vec3(t.p.x, t.p.y + 0.12f, t.p.z), nullptr) > 0.0f) t.p.y += 2.0f * dt;
        }

        // ---- E: pracovní stůl
        int aimT = (!uiBlock || uiMode == 2) ? aimTable(eye, look, 4.2f) : -1;
        if (!dead && IN_KEY_PRESSED(KEY_E)) {
            if (uiMode == 2) { uiMode = 0; craftRecipe = -1; craftProg = 0; }
            else if (uiMode == 0 && aimT >= 0) {
                if (inv[I_HAMMER] > 0) {
                    uiMode = 2;
                    uiTableId = tables[aimT].id;
                    sel = I_HAMMER;
                    craftRecipe = -1;
                    craftProg = 0;
                } else toast("Potrebujes kladivko (TAB)");
            }
        }
        if (uiMode == 2) {
            int ti = findTable(uiTableId);
            if (ti < 0 || vlen(vsub(tables[ti].p, pos)) > 6.0f) { uiMode = 0; craftRecipe = -1; craftProg = 0; }
        }

        // ---- výběr věci / inventář / stůl
        if (uiMode == 0 && !dead) {
            for (int i = 0; i < I_COUNT; i++)
                if (IN_KEY_PRESSED(KEY_ONE + i)) sel = i;
            float wheel = IN_WHEEL();
            if (wheel != 0) sel = (sel + (wheel > 0 ? -1 : 1) + I_COUNT) % I_COUNT;
        } else if (uiMode == 1) {
            for (int r = 0; r < 2; r++)
                if (IN_KEY_PRESSED(KEY_ONE + r)) {
                    if (canCraft(HAND_RECIPES[r])) {
                        inv[I_STONE] -= HAND_RECIPES[r].stone;
                        inv[I_WOOD] -= HAND_RECIPES[r].wood;
                        addItem(HAND_RECIPES[r].out, 1);
                    } else toast("Chybi suroviny");
                }
        } else if (uiMode == 2) {
            for (int r = 0; r < 3; r++)
                if (IN_KEY_PRESSED(KEY_ONE + r)) {
                    if (canCraft(TABLE_RECIPES[r])) { craftRecipe = r; craftProg = 0; }
                    else toast("Chybi suroviny");
                }
            if (IN_MOUSE_DOWN(MOUSE_BUTTON_LEFT) && cooldown <= 0.0f && craftRecipe >= 0) {
                int ti = findTable(uiTableId);
                cooldown = 0.38f;
                swing = 1.0f;
                craftProg += 0.25f;
                gShake = fmaxf(gShake, 0.03f);
                if (ti >= 0)
                    for (int k = 0; k < 7; k++)
                        addPart(vadd(tables[ti].p, vec3(frand(-0.3f, 0.3f), 1.0f, frand(-0.2f, 0.2f))),
                                vec3(frand(-3, 3), frand(1.5f, 4.5f), frand(-3, 3)), frand(0.3f, 0.8f), 0.07f, {255, 210, 110, 255}, 3);
                if (craftProg >= 0.999f) {
                    const Recipe &rc = TABLE_RECIPES[craftRecipe];
                    if (canCraft(rc)) {
                        inv[I_STONE] -= rc.stone;
                        inv[I_WOOD] -= rc.wood;
                        addItem(rc.out, 1);
                    } else toast("Chybi suroviny");
                    craftRecipe = -1;
                    craftProg = 0;
                }
            }
        }

        // ---- akce ve světě
        bool canDrink = false;
        if (uiMode == 0 && !dead) {
            canDrink = aimWater(eye, look, 6.0f);
            Vector3 hit;

            // LMB: kácet strom, jinak kopat
            if (IN_MOUSE_DOWN(MOUSE_BUTTON_LEFT) && cooldown <= 0.0f) {
                bool terrHit = rayTerrain(eye, look, 7.0f, hit);
                float terrDist = terrHit ? vlen(vsub(hit, eye)) : 7.0f;
                int tgx = 0, tgz = 0;
                TreeInfo ti;
                Vector3 tp;
                bool withHammer = (sel == I_HAMMER);
                if (rayTree(eye, look, fminf(terrDist, 6.0f), tgx, tgz, ti, tp)) {
                    chopTree(tgx, tgz, ti, tp);
                    cooldown = withHammer ? 0.38f : 0.5f;
                    swing = 1.0f;
                } else if (terrHit) {
                    spawnRocks(hit, 1.3f, 4, 5.0f);
                    editSphere(hit, 1.3f, false);
                    digAcc += withHammer ? 0.5f : 0.4f;
                    if (digAcc >= 1.0f) {
                        int n = (int)digAcc;
                        digAcc -= n;
                        inv[I_STONE] += n;
                    }
                    cooldown = withHammer ? 0.10f : 0.13f;
                    swing = 1.0f;
                }
            }

            // pití
            if (IN_KEY_DOWN(KEY_F) && canDrink && thirst < 20.0f) {
                thirst = fminf(20.0f, thirst + dt * 7.0f);
                if (frand(0, 1) < 0.1f)
                    addPart(vadd(eye, vmul(look, 1.2f)), vec3(frand(-1, 1), frand(1, 2), frand(-1, 1)), 0.6f, 0.08f, {150, 200, 255, 255}, 0);
            }

            // PMB: použít vybranou věc
            if (sel == I_STONE) {
                if (IN_MOUSE_DOWN(MOUSE_BUTTON_RIGHT) && cooldown <= 0.0f && inv[I_STONE] > 0) {
                    if (rayTerrain(eye, look, 8.0f, hit)) {
                        Vector3 pc = vsub(hit, vmul(look, 0.6f));
                        if (vlen(vsub(pc, vadd(pos, vec3(0, 0.9f, 0)))) > 2.0f) {
                            editSphere(pc, 1.0f, true);
                            inv[I_STONE]--;
                            cooldown = 0.2f;
                        }
                    }
                }
            } else if (IN_MOUSE_PRESSED(MOUSE_BUTTON_RIGHT)) {
                if (sel == I_APPLE) {
                    if (inv[I_APPLE] > 0 && hunger < 19.0f) {
                        inv[I_APPLE]--;
                        hunger = fminf(20.0f, hunger + 5.0f);
                        thirst = fminf(20.0f, thirst + 1.0f);
                        toast("Snedl jsi jablko");
                    } else if (inv[I_APPLE] > 0) toast("Nemas hlad");
                } else if (sel == I_GRENADE) {
                    if (inv[I_GRENADE] > 0) {
                        inv[I_GRENADE]--;
                        grenades.push_back({vadd(eye, vmul(look, 1.0f)), vadd(vmul(look, 18.0f), vec3(vel.x, 4.0f, vel.z)), 2.4f, 6.5f, 26.0f, false});
                    }
                } else if (sel == I_DYNAMITE) {
                    if (inv[I_DYNAMITE] > 0) {
                        inv[I_DYNAMITE]--;
                        grenades.push_back({vadd(eye, vmul(look, 1.0f)), vadd(vmul(look, 12.0f), vec3(vel.x, 3.0f, vel.z)), 3.5f, 9.5f, 34.0f, true});
                    }
                } else if (sel == I_ROCKET) {
                    if (inv[I_ROCKET] > 0) {
                        inv[I_ROCKET]--;
                        rockets.push_back({vadd(eye, vmul(look, 1.5f)), vmul(look, 65.0f), 6.0f});
                        gShake = fmaxf(gShake, 0.1f);
                    }
                } else if (sel == I_TABLE) {
                    if (inv[I_TABLE] > 0) {
                        if (rayTerrain(eye, look, 8.0f, hit)) {
                            Vector3 g;
                            densityGrad(vadd(hit, vec3(0, 0.1f, 0)), &g);
                            float gl = vlen(g);
                            float ny = gl > 1e-4f ? -g.y / gl : 1.0f;
                            bool near = false;
                            for (const Table &t : tables) if (vlen(vsub(t.p, hit)) < 1.8f) near = true;
                            if (ny < 0.75f) toast("Na svahu stul nepostavis");
                            else if (near) toast("Tady uz stul stoji");
                            else {
                                inv[I_TABLE]--;
                                tables.push_back({vadd(hit, vec3(0, 0.05f, 0)), yaw, ++tableCounter});
                                toast("Postavil jsi pracovni stul");
                            }
                        }
                    }
                } else if (sel == I_WOOD) {
                    toast("Drevo se hodi na vyrobu");
                }
            }
        }

        // ---- simulace
        updateProjectiles(dt);
        updateParticles(dt);
        gShake *= expf(-5.0f * dt);
        updateStreaming(pos, 0.006, VIEW_R, true);
        updateGrass(pos, 0.003);
        for (size_t i = 0; i < toasts.size();) {
            toasts[i].t -= dt;
            if (toasts[i].t <= 0) toasts.erase(toasts.begin() + i); else i++;
        }

        // ---- obloha a světlo podle denní doby
        Vector3 lightDir, lightCol, zen, hor;
        float nightAmt;
        skyParams(tod, lightDir, lightCol, zen, hor, nightAmt);
        setCommon(shScene, cScene, lightDir, lightCol, zen, hor);
        setCommon(shWater, cWater, lightDir, lightCol, zen, hor);
        setCommon(shSky, cSky, lightDir, lightCol, zen, hor);
        setCommon(shGrass, cGrass, lightDir, lightCol, zen, hor);
        Vector3 gAmb = vlerp(vec3(0.10f, 0.085f, 0.07f), vec3(0.012f, 0.013f, 0.02f), nightAmt);
        SetShaderValue(shScene, locAmb, &gAmb, SHADER_UNIFORM_VEC3);
        SetShaderValue(shGrass, locAmbGrass, &gAmb, SHADER_UNIFORM_VEC3);
        SetShaderValue(shSky, locNight, &nightAmt, SHADER_UNIFORM_FLOAT);

        // ---- kamera
        Vector3 shake = {frand(-1, 1) * gShake, frand(-1, 1) * gShake, frand(-1, 1) * gShake};
        cam.position = vadd(eye, shake);
        cam.target = vadd(cam.position, look);

        Vector3 camRight = vnorm(vec3(-cosf(yaw), 0, sinf(yaw)));
        Vector3 camUp = vnorm(vec3(-sinf(pitch) * sinf(yaw), cosf(pitch), -sinf(pitch) * cosf(yaw)));
        float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
        float tanFov = tanf(cam.fovy * 0.5f * DEG2RAD);
        Vector2 res = {(float)GetScreenWidth(), (float)GetScreenHeight()};
        SetShaderValue(shSky, locRight, &camRight, SHADER_UNIFORM_VEC3);
        SetShaderValue(shSky, locUp, &camUp, SHADER_UNIFORM_VEC3);
        SetShaderValue(shSky, locFwd, &look, SHADER_UNIFORM_VEC3);
        SetShaderValue(shSky, locTan, &tanFov, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shSky, locAsp, &aspect, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shSky, locRes, &res, SHADER_UNIFORM_VEC2);
        SetShaderValue(shSky, locTimeSky, &timeSec, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shWater, locTimeWater, &timeSec, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shGrass, locTimeGrass, &timeSec, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shScene, locViewScene, &cam.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(shWater, locViewWater, &cam.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(shGrass, locViewGrass, &cam.position, SHADER_UNIFORM_VEC3);

        // ---- kreslení
        BeginDrawing();
        ClearBackground({20, 24, 34, 255});

        if (dbg) TraceLog(LOG_INFO, "COMPLEX: frame %d: obloha", frameNo);
        BeginShaderMode(shSky);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), WHITE);
        EndShaderMode();

        if (dbg) TraceLog(LOG_INFO, "COMPLEX: frame %d: teren", frameNo);
        BeginMode3D(cam);
        for (auto &kv : chunkMap) {
            if (!kv.second.has) continue;
            int cx, cy, cz;
            decodeKey(kv.first, cx, cy, cz);
            Vector3 o = {cx * CHUNK_M, cy * CHUNK_M, cz * CHUNK_M};
            Vector3 ctr = {o.x + 8, o.y + 8, o.z + 8};
            Vector3 d = vsub(ctr, cam.position);
            float dist = vlen(d);
            if (dist > (VIEW_R + 1) * CHUNK_M) continue;
            if (dist > 30.0f && vdot(d, look) < -26.0f) continue;
            DrawMesh(kv.second.mesh, matScene, MatrixTranslate(o.x, o.y, o.z));
        }

        // pracovní stoly
        for (const Table &t : tables) {
            Matrix m = MatrixMultiply(MatrixRotateY(t.yaw), MatrixTranslate(t.p.x, t.p.y, t.p.z));
            DrawMesh(tableMesh, matScene, m);
        }

        // tráva
        if (dbg) TraceLog(LOG_INFO, "COMPLEX: frame %d: trava", frameNo);
        rlDisableBackfaceCulling();
        for (auto &kv : chunkMap) {
            Chunk &ch = kv.second;
            if (!ch.hasGrass) continue;
            int cx, cy, cz;
            decodeKey(kv.first, cx, cy, cz);
            Vector3 o = {cx * CHUNK_M, cy * CHUNK_M, cz * CHUNK_M};
            Vector3 ctr = {o.x + 8, o.y + 8, o.z + 8};
            Vector3 d = vsub(ctr, cam.position);
            float dist = vlen(d);
            if (dist > GRASS_DRAW + 14.0f) continue;
            if (dist > 24.0f && vdot(d, look) < -20.0f) continue;
            DrawMesh(ch.grass, matGrass, MatrixTranslate(o.x, o.y, o.z));
        }
        rlEnableBackfaceCulling();

        for (const Particle &q : parts) {
            if (q.kind == 0) {
                float s = q.size * fminf(1.0f, q.life / 1.5f);
                float lum = 0.35f + 0.65f * clamp01(lightCol.x * 1.2f + 0.15f) * (1.0f - 0.6f * nightAmt);
                Color c = {(unsigned char)(q.col.r * lum), (unsigned char)(q.col.g * lum), (unsigned char)(q.col.b * lum), 255};
                DrawCube(q.p, s, s, s, c);
            } else if (q.kind == 3) {
                float s = q.size * fminf(1.0f, q.life / q.maxLife * 2.0f);
                DrawCube(q.p, s, s, s, q.col);
            }
        }
        for (const Rocket &r : rockets) DrawSphere(r.p, 0.22f, {60, 60, 65, 255});
        for (const Grenade &g : grenades) {
            if (g.dyn) DrawCylinder(vadd(g.p, vec3(0, -0.15f, 0)), 0.1f, 0.1f, 0.32f, 8, {190, 40, 36, 255});
            else DrawSphere(g.p, 0.2f, {50, 70, 45, 255});
            if (fmodf(g.fuse, 0.3f) < 0.12f) DrawSphere(vadd(g.p, vec3(0, g.dyn ? 0.2f : 0.0f, 0)), 0.1f, RED);
        }

        if (dbg) TraceLog(LOG_INFO, "COMPLEX: frame %d: voda", frameNo);
        DrawMesh(waterMesh, matWater, MatrixTranslate(floorf(cam.position.x), SEA, floorf(cam.position.z)));

        rlDisableDepthMask();
        BeginBlendMode(BLEND_ALPHA);
        for (const Particle &q : parts)
            if (q.kind == 1) {
                float t = 1.0f - q.life / q.maxLife;
                Color c = q.col;
                float dim = 1.0f - 0.8f * nightAmt;
                c.r = (unsigned char)(c.r * dim); c.g = (unsigned char)(c.g * dim); c.b = (unsigned char)(c.b * dim);
                c.a = (unsigned char)(150.0f * (1.0f - t) * fminf(1.0f, t * 8.0f));
                DrawBillboard(cam, puff, q.p, q.size * (0.6f + 0.9f * t), c);
            }
        EndBlendMode();
        BeginBlendMode(BLEND_ADDITIVE);
        for (const Particle &q : parts)
            if (q.kind == 2) {
                float lf = q.life / q.maxLife;
                Color c = q.col;
                c.a = (unsigned char)(230.0f * lf);
                DrawBillboard(cam, puff, q.p, q.size * (1.3f - 0.5f * lf), c);
            }
        EndBlendMode();
        rlEnableDepthMask();
        EndMode3D();

        // ---- efekty přes obraz
        if (cam.position.y < SEA) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {20, 70, 110, 120});
        if (flash > 0.0f) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {200, 20, 20, (unsigned char)(flash * 230.0f)});

        // ---- HUD
        if (dbg) TraceLog(LOG_INFO, "COMPLEX: frame %d: HUD", frameNo);
        const int W = (int)(GetScreenWidth() / gUIS), H = (int)(GetScreenHeight() / gUIS);
        gRecipeN = 0;
        rlPushMatrix();
        rlScalef(gUIS, gUIS, 1.0f);
        const int cxs = W / 2, cys = H / 2;

        if (sel == I_HAMMER && !dead) drawHeldHammer(swing, (float)W, (float)H);

        // zaměřovač
        if (uiMode == 0 && !dead) {
            DrawCircleV({(float)cxs, (float)cys}, 2.2f, {255, 255, 255, 220});
            DrawRectangle(cxs - 14, cys - 1, 8, 2, {255, 255, 255, 170});
            DrawRectangle(cxs + 6, cys - 1, 8, 2, {255, 255, 255, 170});
            DrawRectangle(cxs - 1, cys - 14, 2, 8, {255, 255, 255, 170});
            DrawRectangle(cxs - 1, cys + 6, 2, 8, {255, 255, 255, 170});
        }

        // hotbar
        const float SLOT = 54.0f, SGAP = 6.0f;
        const float hbW = I_COUNT * SLOT + (I_COUNT - 1) * SGAP;
        const float hbX = cxs - hbW / 2.0f, hbY = H - SLOT - 16.0f;
        gHotbar = {hbX, hbY, SLOT, SGAP};
        panel(hbX - 10, hbY - 10, hbW + 20, SLOT + 20, 150);
        for (int i = 0; i < I_COUNT; i++) {
            float x = hbX + i * (SLOT + SGAP);
            bool selected = (i == sel);
            if (selected) DrawRectangleRounded({x - 3, hbY - 3, SLOT + 6, SLOT + 6}, 0.2f, 6, {255, 226, 120, 255});
            DrawRectangleRounded({x, hbY, SLOT, SLOT}, 0.16f, 6, selected ? Color{70, 66, 52, 255} : Color{30, 33, 40, 255});
            drawItemIcon(i, x + 8, hbY + 6, SLOT - 16);
            if (i != I_HAMMER) {
                const char *cnt = TextFormat("%d", inv[i]);
                int tw = MeasureText(cnt, 16);
                drawTextS(cnt, (int)(x + SLOT - tw - 5), (int)(hbY + SLOT - 19), 16, inv[i] > 0 ? WHITE : Color{150, 150, 150, 255});
            }
            drawTextS(TextFormat("%d", i + 1), (int)x + 5, (int)hbY + 3, 12, {230, 230, 230, 210});
        }

        // ukazatele nad hotbarem
        {
            const float ICON = 21.0f;
            float rowW = 10 * (ICON + 2.0f) - 2.0f;
            float y1 = hbY - 10 - ICON - 14, y2 = y1 - ICON - 8;
            drawBar(hbX, y1, health, 0, ICON);
            drawBar(hbX + hbW - rowW, y1, hunger, 1, ICON);
            drawBar(hbX + hbW - rowW, y2, thirst, 2, ICON);
            if (air < 14.9f) drawBar(hbX, y2, air / 15.0f * 20.0f, 3, ICON);
            // název vybrané věci
            const char *nm = ITEM_NAME[sel];
            const char *hint = ITEM_HINT[sel];
            int tw = MeasureText(nm, 22);
            drawTextS(nm, cxs - tw / 2, (int)(y2 - 56), 22, {255, 236, 160, 255});
            int hw = MeasureText(hint, 16);
            drawTextS(hint, cxs - hw / 2, (int)(y2 - 30), 16, {220, 224, 232, 255});
        }

        // čas a FPS
        {
            int hh = (int)tod, mm = (int)((tod - hh) * 60.0f);
            const char *tt = TextFormat("Den %d    %02d:%02d", day, hh, mm);
            int tw = MeasureText(tt, 22);
            panel((float)(W - tw - 34), 12, (float)(tw + 22), 38, 150);
            drawTextS(tt, W - tw - 23, 20, 22, WHITE);
            DrawFPS(10, 10);
        }

        // výzvy
        if (uiMode == 0 && !dead) {
            if (canDrink) {
                const char *m = "Drz F  -  pit";
                int mw = MeasureText(m, 22);
                panel((float)(cxs - mw / 2 - 12), (float)(cys + 46), (float)(mw + 24), 36, 150);
                drawTextS(m, cxs - mw / 2, cys + 52, 22, {200, 230, 255, 255});
            } else if (aimT >= 0) {
                const char *m = (inv[I_HAMMER] > 0) ? "E  -  pracovni stul" : "Potrebujes kladivko (TAB)";
                int mw = MeasureText(m, 22);
                panel((float)(cxs - mw / 2 - 12), (float)(cys + 46), (float)(mw + 24), 36, 150);
                drawTextS(m, cxs - mw / 2, cys + 52, 22, {255, 236, 160, 255});
            }
        }
        for (size_t i = 0; i < toasts.size(); i++) {
            unsigned char a = (unsigned char)(255.0f * fminf(1.0f, toasts[i].t));
            int tw = MeasureText(toasts[i].text, 20);
            float ty = (float)(H - 230 - (int)i * 38);
            DrawRectangleRounded({20.0f, ty, (float)(tw + 24), 32.0f}, 0.3f, 6, {12, 14, 19, (unsigned char)(a * 0.65f)});
            DrawText(toasts[i].text, 32, (int)ty + 6, 20, {255, 255, 255, a});
        }
        if (helpTimer > 0.0f && !dead) {
            unsigned char a = (unsigned char)(fminf(1.0f, helpTimer / 3.0f) * 215.0f);
            DrawText("WASD pohyb | LMB kopat/kacet | PMB pouzit | 1-8 veci | E stul | TAB inventar | F pit | F11 cela obrazovka",
                     10, 34, 16, {255, 255, 255, a});
        }

        // inventář (ruční výroba)
        if (uiMode == 1) {
            float pw = 600, ph = 400, px = cxs - pw / 2, py = cys - ph / 2;
            panel(px, py, pw, ph, 235);
            drawTextS("INVENTAR", (int)px + 24, (int)py + 18, 28, WHITE);
            for (int i = 0; i < I_COUNT; i++) {
                int col = i % 2, row = i / 2;
                float x = px + 24 + col * 150, y = py + 66 + row * 64;
                DrawRectangleRounded({x, y, 140, 54}, 0.2f, 6, {30, 34, 42, 255});
                drawItemIcon(i, x + 8, y + 7, 40);
                drawTextS(ITEM_NAME[i], (int)x + 56, (int)y + 8, 18, WHITE);
                drawTextS(TextFormat("x %d", inv[i]), (int)x + 56, (int)y + 30, 18, {255, 226, 130, 255});
            }
            drawTextS("RUCNI VYROBA", (int)px + 340, (int)py + 66, 22, {170, 215, 255, 255});
            for (int r = 0; r < 2; r++) {
                float y = py + 102 + r * 82;
                bool ok = canCraft(HAND_RECIPES[r]);
                gRecipeRect[r] = RRect{px + 340, y, 236, 72};
                gRecipeN = 2;
                DrawRectangleRounded({px + 340, y, 236, 72}, 0.2f, 6, ok ? Color{34, 62, 40, 255} : Color{40, 34, 34, 255});
                drawItemIcon(HAND_RECIPES[r].out, px + 348, y + 14, 44);
                drawTextS(TextFormat("[%d] %s", r + 1, HAND_RECIPES[r].name), (int)px + 400, (int)y + 10, 20, ok ? WHITE : Color{170, 170, 170, 255});
                const char *cost = (HAND_RECIPES[r].stone > 0)
                    ? TextFormat("%d Kamen + %d Drevo", HAND_RECIPES[r].stone, HAND_RECIPES[r].wood)
                    : TextFormat("%d Drevo", HAND_RECIPES[r].wood);
                drawTextS(cost, (int)px + 400, (int)y + 40, 16, {210, 210, 210, 255});
            }
            drawTextS("Granaty, rakety a dynamit se vyrabi u pracovniho stolu (E).", (int)px + 24, (int)py + ph - 78, 16, {190, 196, 206, 255});
            drawTextS("Kamen: kopej teren.   Drevo a jablka: kac stromy.   TAB zavrit", (int)px + 24, (int)py + ph - 50, 16, {190, 196, 206, 255});
        }

        // pracovní stůl (výroba kladívkem)
        if (uiMode == 2) {
            float pw = 440, ph = 330, px = cxs - pw / 2 + 150, py = cys - ph / 2 - 20;
            if (px + pw > W - 10) px = W - pw - 10;
            panel(px, py, pw, ph, 235);
            drawTextS("PRACOVNI STUL", (int)px + 22, (int)py + 16, 26, {255, 226, 130, 255});
            for (int r = 0; r < 3; r++) {
                float y = py + 58 + r * 70;
                bool ok = canCraft(TABLE_RECIPES[r]);
                bool act = (craftRecipe == r);
                if (act) DrawRectangleRounded({px + 16, y - 3, pw - 32, 66}, 0.2f, 6, {255, 226, 120, 255});
                gRecipeRect[r] = RRect{px + 19, y, pw - 38, 60};
                gRecipeN = 3;
                DrawRectangleRounded({px + 19, y, pw - 38, 60}, 0.2f, 6, ok ? Color{34, 58, 40, 255} : Color{44, 34, 34, 255});
                drawItemIcon(TABLE_RECIPES[r].out, px + 28, y + 8, 44);
                drawTextS(TextFormat("[%d] %s", r + 1, TABLE_RECIPES[r].name), (int)px + 84, (int)y + 8, 22, ok ? WHITE : Color{170, 170, 170, 255});
                drawTextS(TextFormat("%d Kamen + %d Drevo", TABLE_RECIPES[r].stone, TABLE_RECIPES[r].wood), (int)px + 84, (int)y + 34, 16, {210, 210, 210, 255});
            }
            // průběh
            float bx = px + 22, by = py + ph - 62, bw = pw - 44;
            DrawRectangleRounded({bx - 2, by - 2, bw + 4, 24}, 0.4f, 6, {255, 255, 255, 50});
            DrawRectangleRounded({bx, by, bw, 20}, 0.4f, 6, {22, 24, 30, 255});
            if (craftProg > 0.01f) DrawRectangleRounded({bx, by, bw * clamp01(craftProg), 20}, 0.4f, 6, {255, 196, 70, 255});
            const char *msg = (craftRecipe >= 0) ? "Klikej LEVYM tlacitkem - bij kladivkem!" : "Vyber recept klavesami 1-3";
            drawTextS(msg, (int)px + 22, (int)py + ph - 32, 16, {230, 232, 238, 255});
            drawTextS("E = zavrit", (int)(px + pw - 100), (int)py + 20, 16, {190, 196, 206, 255});
        }

        // smrt
        if (dead) {
            DrawRectangle(0, 0, W, H, {90, 0, 0, 170});
            const char *t1 = "ZEMREL JSI";
            const char *t2 = "Stiskni MEZERNIK pro znovuzrozeni";
            DrawText(t1, cxs - MeasureText(t1, 60) / 2, cys - 60, 60, WHITE);
            DrawText(t2, cxs - MeasureText(t2, 24) / 2, cys + 20, 24, WHITE);
            DrawText(TextFormat("Prezil jsi %d dni", day - 1), cxs - 80, cys + 60, 20, {230, 200, 200, 255});
        }
        rlPopMatrix();
#if defined(PLATFORM_ANDROID)
        drawTouchUI(GetScreenWidth(), GetScreenHeight());
#endif
        EndDrawing();
    }

    for (auto &kv : chunkMap) {
        if (kv.second.has) UnloadMesh(kv.second.mesh);
        freeGrass(kv.second);
    }
    UnloadMesh(waterMesh);
    UnloadMesh(tableMesh);
    UnloadTexture(puff);
    UnloadShader(shScene); UnloadShader(shWater); UnloadShader(shSky); UnloadShader(shGrass);
    CloseWindow();
    return 0;
}
