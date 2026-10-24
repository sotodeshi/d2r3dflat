// Flat first-person body picking. The scan reads the client's monster hash (NPCs live there too)
// and MonStats sizes. It never writes the game. The ray change is applied by d2rcam's ScreenToRay.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "flat_pick.h"
#include "sigscan.h"
#include "unit_scan.h"

namespace flatpick {
namespace {

constexpr uint64_t kMonStats = 0x975B0;
constexpr uint64_t kUnits = 0x2A23910;
constexpr int kBodyCap = 48;
constexpr int kObjCap = 32;
constexpr int kCap = kBodyCap + kObjCap;
constexpr float kReach = 55.0f;

const uint8_t kSigMonStats[18] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0x0F, 0xB6, 0xE9};

using StatsFn = uintptr_t (*)(uint8_t bank, int cls);
void (*g_log)(const char*) = nullptr;

struct Cap {
    float x = 0, y = 0, z = 0, height = 0, radius = 0;
};
struct Snap {
    Cap c[kCap]{};
    int nBody = 0;                 // bodies in c[0, nBody)
    int nObj = 0;                  // objects in c[kBodyCap, kBodyCap + nObj)
};
Snap g_snap[2];
std::atomic<int> g_published{0};
bool g_toldOn = false, g_toldMiss = false;

struct Row {
    uint32_t cls = 0xFFFFFFFFu;
    uint8_t bank = 0, fa = 0, fb = 0, h = 0, ok = 0;
};
Row g_rows[512];

bool ReadMem(void* dst, uintptr_t src, size_t n) noexcept {
    if (src < 0x10000 || n == 0 || n > 4096) return false;
    __try {
        memcpy(dst, reinterpret_cast<const void*>(src), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallStats(uintptr_t fn, uint8_t bank, int cls, uintptr_t* out) noexcept {
    __try {
        *out = reinterpret_cast<StatsFn>(fn)(bank, cls);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RowOf(uint8_t bank, uint32_t cls, uint8_t* fa, uint8_t* fb, uint8_t* h) {
    const unsigned slot = (cls * 3u + bank) & 511u;
    Row& row = g_rows[slot];
    if (row.ok && row.cls == cls && row.bank == bank) {
        *fa = row.fa;
        *fb = row.fb;
        *h = row.h;
        return true;
    }
    const uintptr_t fn = d2rsig::Addr(kMonStats);
    uintptr_t at = 0;
    uint8_t bytes[16] = {};
    if (!fn || cls > 4095 || bank > 3 || !CallStats(fn, bank, static_cast<int>(cls), &at) || !ReadMem(bytes, at, sizeof bytes)) return false;
    row.cls = cls;
    row.bank = bank;
    row.fa = bytes[8];
    row.fb = bytes[9];
    row.h = bytes[13];
    row.ok = 1;
    *fa = row.fa;
    *fb = row.fb;
    *h = row.h;
    return true;
}

void SizeOf(uint8_t fa, uint8_t fb, uint8_t h, bool known, float* height, float* radius) {
    *height = (known && h >= 20 && h <= 200) ? h * 0.10f : 7.0f;
    const int fp = known ? (fa > fb ? fa : fb) : 0;
    *radius = (fp >= 1 && fp <= 8) ? fp * 0.7f : 1.8f;
    if (*radius < 1.5f) *radius = 1.5f;
    if (*radius > 3.5f) *radius = 3.5f;
}

// Objects.txt ids from 1.13, which D2R kept. Packed byte: low 3 bits are the kind
// (1 door, 2 shrine, 3 chest, 4 portal, 5 waypoint, 6 warp, 7 well / stash / quest),
// high 5 bits are the footprint in subtiles. 0 is scenery, a torch, a barrel.
#include "flat_pick_obj.inc"

float ObjRadius(uint8_t packed) {
    int size = packed >> 3;
    if (size < 1) size = 2;
    float r = size * 0.72f;
    if (r < 1.6f) r = 1.6f;
    if (r > 4.8f) r = 4.8f;
    return r;
}

float ObjHeight(int kind) {
    switch (kind) {
        case 1: return 11.0f;   // a door, clicked on the frame
        case 2: return 8.0f;    // a shrine
        case 3: return 5.5f;    // a chest, urn, rack: low, so a body behind it still wins
        case 4: return 12.0f;   // a portal
        case 5: return 9.0f;    // a waypoint
        case 6: return 8.5f;    // stairs, a trap door, a level entrance
        default: return 7.0f;   // a well, the stash, a quest object
    }
}

// A door, shrine, chest or fixture that has already been used (mode 2) is not a target.
bool ObjClosedOnly(int kind) { return kind == 1 || kind == 2 || kind == 3 || kind == 7; }

// An object's place: the static path's tile, the same point the torch lights use.
bool ObjectPlace(const unitscan::Unit& u, float* x, float* z) {
    const uintptr_t path = unitscan::Field64(u, unitscan::kOffPath);
    uint32_t s[2] = {};
    if (!ReadMem(s, path + 0x10, sizeof s)) return false;
    if (s[0] > 200000u || s[1] > 200000u) return false;
    *x = 2.0f * s[0] + 1.0f;
    *z = 2.0f * s[1] + 1.0f;
    return std::isfinite(*x + *z);
}

// The slot this one takes in [base, base + count), or -1 when the list is full of nearer ones.
int KeepSlot(const Snap& dst, int base, int count, int cap, float d2, float heroX, float heroZ) {
    if (count < cap) return base + count;
    int worst = base;
    float worstD = -1.0f;
    for (int i = 0; i < count; ++i) {
        const float ex = dst.c[base + i].x - heroX, ez = dst.c[base + i].z - heroZ;
        const float ed = ex * ex + ez * ez;
        if (ed > worstD) {
            worstD = ed;
            worst = base + i;
        }
    }
    if (d2 >= worstD) return -1;
    return worst;
}

bool RayCapsule(const float o[3], const float d[3], const Cap& c, float* tHit) {
    const float bot = c.y;
    const float top = c.y + c.height;
    const float ax = o[0] - c.x, az = o[2] - c.z;
    if (ax * ax + az * az <= c.radius * c.radius && o[1] >= bot && o[1] <= top) {
        *tHit = 0.02f;
        return true;
    }
    float best = 1.0e30f;
    bool hit = false;
    auto take = [&](float t) {
        if (t > 1.0e-3f && t < best) {
            best = t;
            hit = true;
        }
    };
    const float A = d[0] * d[0] + d[2] * d[2];
    const float B = 2.0f * (ax * d[0] + az * d[2]);
    const float C = ax * ax + az * az - c.radius * c.radius;
    if (A > 1.0e-8f) {
        const float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            const float s = sqrtf(disc);
            const float ts[2] = {(-B - s) / (2.0f * A), (-B + s) / (2.0f * A)};
            for (float t : ts) {
                const float y = o[1] + t * d[1];
                if (y >= bot && y <= top) take(t);
            }
        }
    }
    auto sphere = [&](float sy) {
        const float px = o[0] - c.x, py = o[1] - sy, pz = o[2] - c.z;
        const float bb = px * d[0] + py * d[1] + pz * d[2];
        const float cc = px * px + py * py + pz * pz - c.radius * c.radius;
        const float disc = bb * bb - cc;
        if (disc < 0.0f) return;
        const float s = sqrtf(disc);
        take(-bb - s);
        take(-bb + s);
    };
    sphere(bot);
    sphere(top);
    if (!hit) return false;
    *tHit = best;
    return true;
}

void AimFeet(float o[3], float d[3], const Cap& c) {
    const float v[3] = {c.x - o[0], c.y - o[1], c.z - o[2]};
    const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(len > 0.05f)) return;
    d[0] = v[0] / len;
    d[1] = v[1] / len;
    d[2] = v[2] / len;
}

}  // namespace

void SetLog(void (*fn)(const char*)) { g_log = fn; }

void Refresh(bool active, float heroX, float heroY, float heroZ) {
    const int cur = g_published.load(std::memory_order_relaxed);
    Snap& dst = g_snap[1 - cur];
    dst.nBody = 0;
    dst.nObj = 0;
    if (!active || !std::isfinite(heroX + heroY + heroZ)) {
        g_published.store(1 - cur, std::memory_order_release);
        g_toldOn = false;
        return;
    }
    // LookAt and path Place share one world frame (camera look-at). F4's absolute
    // ground shear (pose is sheared) must NOT be applied here - v15 did, capsules
    // missed the ray, hover/labels never appeared (2026-10-19).
    const uintptr_t table = d2rsig::Addr(kUnits);
    if (!table) {
        g_published.store(1 - cur, std::memory_order_release);
        return;
    }
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    if (active && !d2rsig::Check(kMonStats, kSigMonStats, sizeof kSigMonStats)) {
        if (!g_toldMiss && g_log) {
            g_toldMiss = true;
            g_log("flat pick: MonStats signature missing, monster aim stays off");
        }
    } else if (active) {
        unitscan::EachUnit(read, table, unitscan::kMonster, [&](uintptr_t, const unitscan::Unit& u) {
            const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
            if (mode == 0 || mode == 12) return true;
            float x, z;
            if (!unitscan::Place(read, u, &x, &z)) return true;
            const float dx = x - heroX, dz = z - heroZ;
            const float d2 = dx * dx + dz * dz;
            if (d2 > kReach * kReach) return true;
            const int slot = KeepSlot(dst, 0, dst.nBody, kBodyCap, d2, heroX, heroZ);
            if (slot < 0) return true;
            if (slot == dst.nBody) ++dst.nBody;
            uint8_t fa = 0, fb = 0, h = 0;
            const bool known = RowOf(u.b[unitscan::kOffBank], unitscan::Field32(u, unitscan::kOffClass), &fa, &fb, &h);
            Cap& c = dst.c[slot];
            c.x = x;
            c.y = heroY;
            c.z = z;
            SizeOf(fa, fb, h, known, &c.height, &c.radius);
            return true;
        });
    }
    unitscan::EachUnit(read, table, 2, [&](uintptr_t, const unitscan::Unit& u) {
        const uint32_t cls = unitscan::Field32(u, unitscan::kOffClass);
        if (cls >= static_cast<uint32_t>(kObjN)) return true;
        const uint8_t packed = kObj[cls];
        const int kind = packed & 7;
        if (!kind) return true;
        const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
        if (ObjClosedOnly(kind) && mode > 1) return true;
        float x, z;
        if (!ObjectPlace(u, &x, &z)) return true;
        const float dx = x - heroX, dz = z - heroZ;
        const float d2 = dx * dx + dz * dz;
        if (d2 > kReach * kReach) return true;
        const int slot = KeepSlot(dst, kBodyCap, dst.nObj, kObjCap, d2, heroX, heroZ);
        if (slot < 0) return true;
        if (slot == kBodyCap + dst.nObj) ++dst.nObj;
        Cap& c = dst.c[slot];
        c.x = x;
        c.y = heroY;
        c.z = z;
        c.height = ObjHeight(kind);
        c.radius = ObjRadius(packed);
        return true;
    });
    g_published.store(1 - cur, std::memory_order_release);
    if (!g_toldOn && (dst.nBody > 0 || dst.nObj > 0) && g_log) {
        g_toldOn = true;
        char b[96];
        snprintf(b, sizeof b, "flat pick: body aim on, %d bodies, %d objects in reach", dst.nBody, dst.nObj);
        g_log(b);
    }
}

void Adjust(float origin[3], float dir[3]) {
    const Snap& snap = g_snap[g_published.load(std::memory_order_acquire)];
    if (snap.nBody <= 0 && snap.nObj <= 0) return;
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 0.5f)) return;
    dir[0] /= len;
    dir[1] /= len;
    dir[2] /= len;
    int best = -1;
    float bestT = 1.0e30f;
    auto consider = [&](int i) {
        float t = 0;
        if (RayCapsule(origin, dir, snap.c[i], &t) && t < bestT) {
            bestT = t;
            best = i;
        }
    };
    for (int i = 0; i < snap.nBody; ++i) consider(i);
    for (int i = 0; i < snap.nObj; ++i) consider(kBodyCap + i);
    if (best >= 0) AimFeet(origin, dir, snap.c[best]);
}

bool MonsterPose(uintptr_t unit, float floorY, float* x, float* y, float* z, float* height) {
    if (!unit || !x || !y || !z || !height || !std::isfinite(floorY)) return false;
    unitscan::Unit u{};
    if (!ReadMem(u.b, unit, sizeof u.b)) return false;
    if (unitscan::Field32(u, unitscan::kOffType) != static_cast<uint32_t>(unitscan::kMonster)) return false;
    const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
    if (mode == 0 || mode == 12) return false;
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    float px = 0, pz = 0;
    if (!unitscan::Place(read, u, &px, &pz)) return false;
    uint8_t fa = 0, fb = 0, h = 0;
    const bool known = RowOf(u.b[unitscan::kOffBank], unitscan::Field32(u, unitscan::kOffClass), &fa, &fb, &h);
    float rad = 0;
    SizeOf(fa, fb, h, known, height, &rad);
    *x = px;
    *y = floorY;
    *z = pz;
    return std::isfinite(*x + *y + *z + *height) && *height > 0.5f;
}

uintptr_t NearestMonster(float hx, float hy, float hz, float reach) {
    (void)hy;   // look-at Y unused: Place and LookAt share one frame (not F4 sheared pose)
    if (!std::isfinite(hx + hz + reach) || reach < 0.0f) return 0;
    const uintptr_t table = d2rsig::Addr(kUnits);
    if (!table) return 0;
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    uintptr_t best = 0;
    float bestD2 = reach * reach;
    unitscan::EachUnit(read, table, unitscan::kMonster, [&](uintptr_t at, const unitscan::Unit& u) {
        const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
        if (mode == 0 || mode == 12) return true;
        float x = 0, z = 0;
        if (!unitscan::Place(read, u, &x, &z)) return true;
        const float d2 = (x - hx) * (x - hx) + (z - hz) * (z - hz);
        if (d2 <= bestD2) {
            bestD2 = d2;
            best = at;
        }
        return true;
    });
    return best;
}

}  // namespace flatpick
